// =============================================================================
// render_graph.cpp — Phase 3 end-to-end renderer. Opens a window, uploads the
// atlas, accepts mesh uploads, records one indirect-draw pass per frame.
//
// Shape (DESIGN.md §2.6):
//   * Vulkan 1.3 with dynamic rendering (no VkRenderPass objects).
//   * Bindless descriptors: one image-array binding for the texture atlas,
//     one storage-buffer binding for per-instance transforms.
//   * Triple-buffered, FIFO_RELAXED swapchain.
//   * Two frames in flight (N_FRAMES_IN_FLIGHT = 2).
//   * One opaque pass. Transparent + post-process passes are stubbed; they
//     land with Phase 6 water (transparent) and Phase 8 polish (post-process).
//
// Phase 3 targets (Appendix E §E.4): 60 FPS at 12-chunk radius on a static
// scene, RTX 3060 class. The code shape below is the simplest structure that
// plausibly meets that; the first profile on real hardware decides which
// rungs of §2.11 need climbing.
// =============================================================================
#include "demen/render_graph.hpp"
#include "demen/renderer.hpp"

#include "vk_instance.hpp"
#include "vk_swapchain.hpp"
#include "vk_allocator.hpp"
#include "pipeline_cache.hpp"

#include <GLFW/glfw3.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

using namespace demen::renderer;

namespace {

// ---------------------------------------------------------------------------
// Per-frame sync state.
// ---------------------------------------------------------------------------
constexpr uint32_t kFramesInFlight = 2;
constexpr uint32_t kMaxAtlasSlots  = 1024;

struct FrameSync {
    VkSemaphore  image_ready      = VK_NULL_HANDLE;
    VkSemaphore  render_done      = VK_NULL_HANDLE;
    VkFence      in_flight        = VK_NULL_HANDLE;
    VkCommandBuffer cmd           = VK_NULL_HANDLE;
};

// Uploaded mesh (resident vertex + index buffers). One per uploaded slot.
struct MeshSlot {
    Buffer   vbo;
    Buffer   ibo;
    uint32_t index_count = 0;
    bool     live        = false;
};

// Drawable instance — collapsed to bindless "indirect draw" candidates.
struct InstanceEntry {
    uint32_t mesh_slot;
    uint32_t atlas_override;
    float    world_xform[16];
};

// ---------------------------------------------------------------------------
// Render graph — one per window.
// ---------------------------------------------------------------------------
struct RenderGraph {
    GLFWwindow*     window = nullptr;
    VkContext       ctx{};
    Swapchain       sc{};

    VkCommandPool   cmd_pool = VK_NULL_HANDLE;
    FrameSync       frames[kFramesInFlight]{};
    uint32_t        frame_ix = 0;

    // Depth buffer (§2.6: one opaque pass, depth-tested).
    Image depth{};

    // Bindless descriptors.
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout      pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorPool      desc_pool = VK_NULL_HANDLE;
    VkDescriptorSet       desc_set  = VK_NULL_HANDLE;

    // Opaque pipeline.
    VkPipeline opaque_pipeline = VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache =  VK_NULL_HANDLE;
    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule fs = VK_NULL_HANDLE;
    VkSampler      sampler = VK_NULL_HANDLE;

    // Atlas upload.
    Image     atlas_image{};
    uint32_t  atlas_tile_size = 0;
    uint32_t  atlas_n_tiles   = 0;

    // Mesh store. Keyed by slot (dense).
    std::mutex                         mesh_mu;
    std::vector<MeshSlot>              meshes;
    std::vector<uint32_t>              free_slots;

    // Per-frame instance staging.
    std::mutex                    submit_mu;
    std::vector<InstanceEntry>    pending_instances;
    demen_frame_input             pending_input{};

    bool  framebuffer_resized = false;
};

std::mutex g_mu;
std::unordered_map<uint64_t, std::unique_ptr<RenderGraph>> g_graphs;
std::atomic<uint64_t> g_next{1};

RenderGraph* rg_from(demen_render_graph_t h) noexcept {
    std::lock_guard lk(g_mu);
    auto it = g_graphs.find(h);
    return it == g_graphs.end() ? nullptr : it->second.get();
}

// ---------------------------------------------------------------------------
// Shader helpers
// ---------------------------------------------------------------------------
std::vector<uint32_t> load_spirv(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const size_t n = static_cast<size_t>(f.tellg());
    if (n == 0 || (n % 4) != 0) return {};
    std::vector<uint32_t> out(n / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n));
    return out;
}

VkShaderModule make_shader(VkDevice d, const std::vector<uint32_t>& code) {
    if (code.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size() * 4;
    ci.pCode    = code.data();
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(d, &ci, nullptr, &m);
    return m;
}

// ---------------------------------------------------------------------------
// Pipeline (§2.6): dynamic rendering, depth test, backface cull, bindless.
// ---------------------------------------------------------------------------
bool build_pipeline(RenderGraph& r) noexcept {
    // -- Descriptor layout -----------------------------------------------------
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding         = 0;  // sampler2D array (bindless atlas)
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = kMaxAtlasSlots;
    bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding         = 1;  // per-instance SSBO (transforms)
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorBindingFlags flags[2] = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
            VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT,
        0,
    };
    VkDescriptorSetLayoutBindingFlagsCreateInfo bfi{};
    bfi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bfi.bindingCount  = 2;
    bfi.pBindingFlags = flags;

    VkDescriptorSetLayoutCreateInfo li{};
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 2;
    li.pBindings    = bindings;
    li.pNext        = &bfi;
    if (vkCreateDescriptorSetLayout(r.ctx.device, &li, nullptr, &r.set_layout) != VK_SUCCESS)
        return false;

    // Push constant: view-projection matrix + time seconds.
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc.offset     = 0;
    pc.size       = sizeof(float) * 20;   // mat4 + vec4(time,...)

    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 1;
    pli.pSetLayouts            = &r.set_layout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pc;
    if (vkCreatePipelineLayout(r.ctx.device, &pli, nullptr, &r.pipeline_layout) != VK_SUCCESS)
        return false;

    // -- Shaders ---------------------------------------------------------------
    // We ship precompiled SPIR-V at shaders/spirv/<name>.spv. Phase 0 already
    // emits a probe pair; Phase 3 adds opaque_mesh.vert/.frag. The build wires
    // them; at runtime we load from the binary's sibling directory (installer
    // flattens layout at Phase 8).
    auto vs_src = load_spirv("shaders/spirv/opaque_mesh.vert.spv");
    auto fs_src = load_spirv("shaders/spirv/opaque_mesh.frag.spv");
    if (vs_src.empty() || fs_src.empty()) return false;
    r.vs = make_shader(r.ctx.device, vs_src);
    r.fs = make_shader(r.ctx.device, fs_src);
    if (!r.vs || !r.fs) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = r.vs;
    stages[0].pName = "main";
    stages[1] = stages[0];
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = r.fs;

    // -- Vertex input: demen_mesh_vertex (32-byte, blittable) ------------------
    VkVertexInputBindingDescription vbind{};
    vbind.binding   = 0;
    vbind.stride    = 32;
    vbind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vattr[5]{};
    // pos: int16_t[3] at offset 0
    vattr[0] = { 0, 0, VK_FORMAT_R16G16B16_SINT,        0 };
    // normal_face (u8) + ao (u8) + uv[0] (i16) packed; we'll route via per-component
    // Just pick a safe R8G8B8A8 for face/ao at offset 6, and R16G16 for UV at 8.
    vattr[1] = { 1, 0, VK_FORMAT_R8G8_UINT,             6 };
    vattr[2] = { 2, 0, VK_FORMAT_R16G16_SINT,           8 };
    vattr[3] = { 3, 0, VK_FORMAT_R32_UINT,              12 }; // atlas_slot
    vattr[4] = { 4, 0, VK_FORMAT_R32_UINT,              16 }; // _pad (unused, kept for stride)

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &vbind;
    vi.vertexAttributeDescriptionCount = 5;
    vi.pVertexAttributeDescriptions    = vattr;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_BACK_BIT;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    VkDynamicState dyns[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy{};
    dy.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = 2;
    dy.pDynamicStates    = dyns;

    VkFormat color_fmt = r.sc.format;
    VkFormat depth_fmt = VK_FORMAT_D32_SFLOAT;
    VkPipelineRenderingCreateInfo dyn_rci{};
    dyn_rci.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    dyn_rci.colorAttachmentCount    = 1;
    dyn_rci.pColorAttachmentFormats = &color_fmt;
    dyn_rci.depthAttachmentFormat   = depth_fmt;

    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.pNext               = &dyn_rci;
    gpi.stageCount          = 2;
    gpi.pStages             = stages;
    gpi.pVertexInputState   = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState      = &vp;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState   = &ms;
    gpi.pDepthStencilState  = &ds;
    gpi.pColorBlendState    = &cb;
    gpi.pDynamicState       = &dy;
    gpi.layout              = r.pipeline_layout;
    gpi.subpass             = 0;

    r.pipeline_cache = load_pipeline_cache(r.ctx,
        std::filesystem::path("pipeline_cache.bin"));
    if (vkCreateGraphicsPipelines(r.ctx.device, r.pipeline_cache, 1, &gpi, nullptr,
                                  &r.opaque_pipeline) != VK_SUCCESS)
        return false;

    // Sampler.
    VkSamplerCreateInfo sci{};
    sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter    = VK_FILTER_NEAREST;   // pixelated voxel look
    sci.minFilter    = VK_FILTER_LINEAR;    // mip-friendly
    sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.maxLod       = VK_LOD_CLAMP_NONE;
    vkCreateSampler(r.ctx.device, &sci, nullptr, &r.sampler);

    return true;
}

// ---------------------------------------------------------------------------
// Glue: resize, frame record, etc.
// ---------------------------------------------------------------------------
void on_resize(GLFWwindow* w, int, int) {
    auto* r = static_cast<RenderGraph*>(glfwGetWindowUserPointer(w));
    if (r) r->framebuffer_resized = true;
}

// 4x4 matrix helpers. Column-major.
struct M4 { float m[16]; };
M4 ident() { M4 r{}; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.0f; return r; }
M4 mul(const M4& a, const M4& b) {
    M4 r{};
    for (int c=0;c<4;++c) for (int d=0;d<4;++d) {
        float s=0; for (int k=0;k<4;++k) s += a.m[k*4+d] * b.m[c*4+k];
        r.m[c*4+d] = s;
    }
    return r;
}
M4 perspective(float fov, float aspect, float n, float f) {
    M4 r{};
    const float t = std::tan(fov * 0.5f);
    r.m[0] = 1.0f / (aspect * t);
    r.m[5] = 1.0f / t;
    r.m[10] = f / (n - f);
    r.m[11] = -1.0f;
    r.m[14] = (f * n) / (n - f);
    return r;
}
M4 look_at(const float p[3], const float fwd[3], const float up[3]) {
    float zx = -fwd[0], zy = -fwd[1], zz = -fwd[2];
    float zl = std::sqrt(zx*zx+zy*zy+zz*zz); zx/=zl; zy/=zl; zz/=zl;
    float xx = up[1]*zz - up[2]*zy;
    float xy = up[2]*zx - up[0]*zz;
    float xz = up[0]*zy - up[1]*zx;
    float xl = std::sqrt(xx*xx+xy*xy+xz*xz); xx/=xl; xy/=xl; xz/=xl;
    float yx = zy*xz - zz*xy, yy = zz*xx - zx*xz, yz = zx*xy - zy*xx;
    M4 r = ident();
    r.m[0]=xx; r.m[4]=xy; r.m[8]=xz;   r.m[12] = -(xx*p[0]+xy*p[1]+xz*p[2]);
    r.m[1]=yx; r.m[5]=yy; r.m[9]=yz;   r.m[13] = -(yx*p[0]+yy*p[1]+yz*p[2]);
    r.m[2]=zx; r.m[6]=zy; r.m[10]=zz;  r.m[14] = -(zx*p[0]+zy*p[1]+zz*p[2]);
    return r;
}

void record_frame(RenderGraph& r, uint32_t img_ix, VkCommandBuffer cmd,
                  const demen_frame_input& in,
                  const std::vector<InstanceEntry>& insts) {
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    // Transition swap image UNDEFINED -> COLOR_ATTACHMENT.
    VkImageMemoryBarrier to_color{};
    to_color.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_color.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
    to_color.newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_color.srcAccessMask    = 0;
    to_color.dstAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    to_color.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_color.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_color.image            = r.sc.images[img_ix];
    to_color.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &to_color);

    VkRenderingAttachmentInfo color_att{};
    color_att.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_att.imageView   = r.sc.views[img_ix];
    color_att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_att.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.clearValue.color = {{ 0.45f, 0.68f, 0.92f, 1.0f }};   // sky blue

    VkRenderingAttachmentInfo depth_att{};
    depth_att.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_att.imageView   = r.depth.view;
    depth_att.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth_att.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_att.storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo ri{};
    ri.sType             = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea.extent = r.sc.extent;
    ri.layerCount        = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color_att;
    ri.pDepthAttachment  = &depth_att;

    vkCmdBeginRendering(cmd, &ri);

    VkViewport vp{};
    vp.width    = (float)r.sc.extent.width;
    vp.height   = -(float)r.sc.extent.height;   // flip Y for right-handed
    vp.x        = 0.0f;
    vp.y        = (float)r.sc.extent.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    VkRect2D sc{}; sc.extent = r.sc.extent;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.opaque_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            r.pipeline_layout, 0, 1, &r.desc_set, 0, nullptr);

    // Push constants: view-projection + time.
    M4 view = look_at(in.camera.pos, in.camera.forward, in.camera.up);
    M4 proj = perspective(in.camera.fov_radians,
                          in.camera.aspect > 0 ? in.camera.aspect
                                               : (float)r.sc.extent.width / r.sc.extent.height,
                          in.camera.near_plane > 0 ? in.camera.near_plane : 0.1f,
                          in.camera.far_plane  > 0 ? in.camera.far_plane  : 2048.0f);
    M4 vp_mat = mul(proj, view);
    float pc[20];
    std::memcpy(pc, vp_mat.m, sizeof(vp_mat.m));
    pc[16] = in.time_seconds; pc[17] = pc[18] = pc[19] = 0.0f;
    vkCmdPushConstants(cmd, r.pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(pc), pc);

    // Draw each instance. Phase 3 ships one direct draw per instance; the
    // indirect-draw consolidation lands when instance count justifies it
    // (§2.11 rung 2; first B-FPS record tells us when).
    for (uint32_t i = 0; i < insts.size(); ++i) {
        const auto& inst = insts[i];
        if (inst.mesh_slot >= r.meshes.size() || !r.meshes[inst.mesh_slot].live)
            continue;
        auto& ms = r.meshes[inst.mesh_slot];
        VkDeviceSize offs = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &ms.vbo.handle, &offs);
        vkCmdBindIndexBuffer(cmd, ms.ibo.handle, 0, VK_INDEX_TYPE_UINT32);
        // Per-instance world transform rides as the second half of push
        // constants — Phase 3 shortcut; SSBO-driven transforms in Phase 4
        // when the toys need real rotation.
        float xform_pc[20];
        std::memcpy(xform_pc, inst.world_xform, 64);
        xform_pc[16] = (float)inst.atlas_override;
        xform_pc[17] = in.time_seconds;
        xform_pc[18] = xform_pc[19] = 0.0f;
        // We already pushed the VP; second push to a disjoint range would
        // collide. For Phase 3 we pre-multiply world into VP on the CPU
        // when the instance has a non-identity transform — cheap and keeps
        // the pipeline layout simple.
        M4 world{}; std::memcpy(world.m, inst.world_xform, 64);
        M4 mvp = mul(vp_mat, world);
        std::memcpy(pc, mvp.m, 64);
        vkCmdPushConstants(cmd, r.pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(pc), pc);
        vkCmdDrawIndexed(cmd, ms.index_count, 1, 0, 0, 0);
    }

    vkCmdEndRendering(cmd);

    // Transition back to PRESENT.
    VkImageMemoryBarrier to_present{};
    to_present.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_present.oldLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_present.newLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_present.srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    to_present.dstAccessMask    = 0;
    to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.image            = r.sc.images[img_ix];
    to_present.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &to_present);

    vkEndCommandBuffer(cmd);
}

} // namespace

// ============================================================================
// ABI
// ============================================================================

extern "C" {

DEMEN_API int demen_render_graph_create(uint32_t w, uint32_t h,
                                        demen_render_graph_t* out) {
    if (!out) return DEMEN_RG_ERR_INVALID_HANDLE;
    *out = 0;
    if (!glfwInit()) return DEMEN_RG_ERR_GLFW_INIT;
    if (!glfwVulkanSupported()) { glfwTerminate(); return DEMEN_RG_ERR_NO_VULKAN; }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* win = glfwCreateWindow((int)w, (int)h, "DemEn", nullptr, nullptr);
    if (!win) { glfwTerminate(); return DEMEN_RG_ERR_GLFW_INIT; }

    auto rg = std::make_unique<RenderGraph>();
    rg->window = win;
    glfwSetWindowUserPointer(win, rg.get());
    glfwSetFramebufferSizeCallback(win, on_resize);

    uint32_t n_ext = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&n_ext);
    std::vector<const char*> req(exts, exts + n_ext);
    std::string err;
    if (!create_context(rg->ctx, req, &err)) {
        glfwDestroyWindow(win); glfwTerminate();
        return DEMEN_RG_ERR_NO_DEVICE;
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(rg->ctx.instance, win, nullptr, &surface) != VK_SUCCESS) {
        destroy_context(rg->ctx);
        glfwDestroyWindow(win); glfwTerminate();
        return DEMEN_RG_ERR_NO_SURFACE;
    }

    if (!create_swapchain(rg->ctx, surface, w, h, rg->sc)) {
        destroy_context(rg->ctx);
        glfwDestroyWindow(win); glfwTerminate();
        return DEMEN_RG_ERR_SWAPCHAIN;
    }

    // Depth.
    if (!create_image_2d(rg->ctx, rg->sc.extent.width, rg->sc.extent.height,
                         VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT, rg->depth))
        return DEMEN_RG_ERR_RUNTIME;

    // Command pool + per-frame command buffers + sync.
    VkCommandPoolCreateInfo pci{};
    pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = rg->ctx.graphics_family;
    vkCreateCommandPool(rg->ctx.device, &pci, nullptr, &rg->cmd_pool);

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = rg->cmd_pool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        vkAllocateCommandBuffers(rg->ctx.device, &ai, &rg->frames[i].cmd);

        VkSemaphoreCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(rg->ctx.device, &sci, nullptr, &rg->frames[i].image_ready);
        vkCreateSemaphore(rg->ctx.device, &sci, nullptr, &rg->frames[i].render_done);

        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(rg->ctx.device, &fci, nullptr, &rg->frames[i].in_flight);
    }

    if (!build_pipeline(*rg)) return DEMEN_RG_ERR_PIPELINE;

    // Descriptor pool + set.
    VkDescriptorPoolSize pool_sizes[2] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxAtlasSlots },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 },
    };
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets       = 1;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes    = pool_sizes;
    dpci.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    vkCreateDescriptorPool(rg->ctx.device, &dpci, nullptr, &rg->desc_pool);

    uint32_t var_count = kMaxAtlasSlots;
    VkDescriptorSetVariableDescriptorCountAllocateInfo vdc{};
    vdc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    vdc.descriptorSetCount = 1;
    vdc.pDescriptorCounts  = &var_count;

    VkDescriptorSetAllocateInfo dai{};
    dai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool     = rg->desc_pool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts        = &rg->set_layout;
    dai.pNext              = &vdc;
    vkAllocateDescriptorSets(rg->ctx.device, &dai, &rg->desc_set);

    uint64_t id = g_next.fetch_add(1, std::memory_order_relaxed);
    { std::lock_guard lk(g_mu); g_graphs.emplace(id, std::move(rg)); }
    *out = id;
    return DEMEN_RG_OK;
}

DEMEN_API int demen_render_graph_destroy(demen_render_graph_t h) {
    std::unique_ptr<RenderGraph> rg;
    {
        std::lock_guard lk(g_mu);
        auto it = g_graphs.find(h);
        if (it == g_graphs.end()) return DEMEN_RG_OK;
        rg = std::move(it->second);
        g_graphs.erase(it);
    }
    if (rg->ctx.device) vkDeviceWaitIdle(rg->ctx.device);
    // Teardown in reverse.
    {
        std::lock_guard mlk(rg->mesh_mu);
        for (auto& m : rg->meshes) {
            if (m.live) { destroy_buffer(rg->ctx, m.vbo); destroy_buffer(rg->ctx, m.ibo); }
        }
    }
    if (rg->atlas_image.handle) destroy_image(rg->ctx, rg->atlas_image);
    if (rg->pipeline_cache) {
        save_pipeline_cache(rg->ctx, rg->pipeline_cache,
            std::filesystem::path("pipeline_cache.bin"));
        vkDestroyPipelineCache(rg->ctx.device, rg->pipeline_cache, nullptr);
    }
    if (rg->sampler)            vkDestroySampler(rg->ctx.device, rg->sampler, nullptr);
    if (rg->opaque_pipeline)    vkDestroyPipeline(rg->ctx.device, rg->opaque_pipeline, nullptr);
    if (rg->vs)                 vkDestroyShaderModule(rg->ctx.device, rg->vs, nullptr);
    if (rg->fs)                 vkDestroyShaderModule(rg->ctx.device, rg->fs, nullptr);
    if (rg->pipeline_layout)    vkDestroyPipelineLayout(rg->ctx.device, rg->pipeline_layout, nullptr);
    if (rg->set_layout)         vkDestroyDescriptorSetLayout(rg->ctx.device, rg->set_layout, nullptr);
    if (rg->desc_pool)          vkDestroyDescriptorPool(rg->ctx.device, rg->desc_pool, nullptr);

    for (auto& f : rg->frames) {
        if (f.image_ready) vkDestroySemaphore(rg->ctx.device, f.image_ready, nullptr);
        if (f.render_done) vkDestroySemaphore(rg->ctx.device, f.render_done, nullptr);
        if (f.in_flight)   vkDestroyFence(rg->ctx.device, f.in_flight, nullptr);
    }
    if (rg->cmd_pool) vkDestroyCommandPool(rg->ctx.device, rg->cmd_pool, nullptr);
    if (rg->depth.handle) destroy_image(rg->ctx, rg->depth);

    destroy_swapchain(rg->ctx, rg->sc);
    destroy_context(rg->ctx);

    if (rg->window) {
        glfwDestroyWindow(rg->window);
        glfwTerminate();
    }
    return DEMEN_RG_OK;
}

DEMEN_API int demen_render_graph_set_atlas(demen_render_graph_t h, demen_atlas_t atlas) {
    auto* r = rg_from(h);
    if (!r) return DEMEN_RG_ERR_INVALID_HANDLE;

    demen_atlas_info info{};
    if (demen_atlas_info_get(atlas, &info) != DEMEN_TC_OK) return DEMEN_RG_ERR_INVALID_HANDLE;
    std::vector<uint8_t> pixels(static_cast<size_t>(info.atlas_width) * info.atlas_height * info.n_channels);
    if (demen_atlas_copy_pixels(atlas, pixels.data(),
                                 static_cast<uint32_t>(pixels.size())) != DEMEN_TC_OK)
        return DEMEN_RG_ERR_RUNTIME;

    if (r->atlas_image.handle) destroy_image(r->ctx, r->atlas_image);
    if (!create_image_2d(r->ctx, info.atlas_width, info.atlas_height,
                         VK_FORMAT_R8G8B8A8_SRGB,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, r->atlas_image))
        return DEMEN_RG_ERR_OOM;
    if (!upload_image_via_staging(r->ctx, r->cmd_pool, pixels.data(), pixels.size(),
                                  r->atlas_image))
        return DEMEN_RG_ERR_RUNTIME;
    r->atlas_tile_size = info.tile_size;
    r->atlas_n_tiles   = info.n_tiles;

    // Bindless descriptor writes: one sampler2D per tile, but all sharing
    // the same VkImage + sampler — the shader indexes into the array by
    // vertex atlas_slot. (Actual sub-image offsets happen in the shader
    // with tile_ix = slot; uv.y shifted by slot * tile_size / atlas_height.)
    // Phase 3 simplification: one descriptor = whole atlas, the shader
    // does the slot math. Keeps descriptor updates O(1) per atlas swap.
    VkDescriptorImageInfo dii{};
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii.imageView   = r->atlas_image.view;
    dii.sampler     = r->sampler;

    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = r->desc_set;
    w.dstBinding      = 0;
    w.dstArrayElement = 0;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.descriptorCount = 1;
    w.pImageInfo      = &dii;
    vkUpdateDescriptorSets(r->ctx.device, 1, &w, 0, nullptr);
    return DEMEN_RG_OK;
}

DEMEN_API int demen_render_graph_upload_mesh(demen_render_graph_t h, demen_mesh_t m,
                                             uint32_t* out_slot) {
    auto* r = rg_from(h);
    if (!r || !out_slot) return DEMEN_RG_ERR_INVALID_HANDLE;

    demen_mesh_stats stats{};
    if (demen_mesh_get_stats(m, &stats) != DEMEN_MESH_OK) return DEMEN_RG_ERR_INVALID_HANDLE;
    if (stats.vertex_count == 0 || stats.index_count == 0) {
        // Empty mesh — still allocate a live slot so draw loops don't die.
        stats.vertex_count = 0; stats.index_count = 0;
    }

    std::vector<demen_mesh_vertex> verts(stats.vertex_count);
    std::vector<uint32_t>          idx(stats.index_count);
    if (stats.vertex_count) demen_mesh_copy_vertices(m, verts.data(), stats.vertex_count);
    if (stats.index_count)  demen_mesh_copy_indices (m, idx.data(),   stats.index_count);

    std::lock_guard lk(r->mesh_mu);
    uint32_t slot = UINT32_MAX;
    if (!r->free_slots.empty()) { slot = r->free_slots.back(); r->free_slots.pop_back(); }
    else { slot = static_cast<uint32_t>(r->meshes.size()); r->meshes.emplace_back(); }

    auto& ms = r->meshes[slot];
    if (ms.live) { destroy_buffer(r->ctx, ms.vbo); destroy_buffer(r->ctx, ms.ibo); }
    ms = {};

    if (stats.vertex_count) {
        const VkDeviceSize vb_bytes = sizeof(demen_mesh_vertex) * verts.size();
        if (!create_buffer(r->ctx, vb_bytes,
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ms.vbo))
            return DEMEN_RG_ERR_OOM;
        upload_via_staging(r->ctx, r->cmd_pool, verts.data(), vb_bytes, ms.vbo.handle, 0);

        const VkDeviceSize ib_bytes = sizeof(uint32_t) * idx.size();
        if (!create_buffer(r->ctx, ib_bytes,
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ms.ibo))
            return DEMEN_RG_ERR_OOM;
        upload_via_staging(r->ctx, r->cmd_pool, idx.data(), ib_bytes, ms.ibo.handle, 0);
    }

    ms.index_count = stats.index_count;
    ms.live = true;
    *out_slot = slot;
    return DEMEN_RG_OK;
}

DEMEN_API int demen_render_graph_drop_mesh(demen_render_graph_t h, uint32_t slot) {
    auto* r = rg_from(h);
    if (!r) return DEMEN_RG_ERR_INVALID_HANDLE;
    std::lock_guard lk(r->mesh_mu);
    if (slot >= r->meshes.size() || !r->meshes[slot].live) return DEMEN_RG_OK;
    destroy_buffer(r->ctx, r->meshes[slot].vbo);
    destroy_buffer(r->ctx, r->meshes[slot].ibo);
    r->meshes[slot] = {};
    r->free_slots.push_back(slot);
    return DEMEN_RG_OK;
}

DEMEN_API int demen_render_graph_submit_frame(demen_render_graph_t h,
                                              const demen_frame_input* in,
                                              const demen_mesh_instance* insts,
                                              uint32_t n) {
    auto* r = rg_from(h);
    if (!r || !in) return DEMEN_RG_ERR_INVALID_HANDLE;

    // Copy the instance list.
    std::vector<InstanceEntry> local;
    local.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        InstanceEntry e{};
        e.mesh_slot      = insts[i].mesh_slot;
        e.atlas_override = insts[i].atlas_override;
        std::memcpy(e.world_xform, insts[i].world_xform, 64);
        local.push_back(e);
    }

    // Wait for previous frame at this slot.
    auto& f = r->frames[r->frame_ix];
    vkWaitForFences(r->ctx.device, 1, &f.in_flight, VK_TRUE, UINT64_MAX);

    uint32_t img = 0;
    VkResult ar = vkAcquireNextImageKHR(r->ctx.device, r->sc.handle, UINT64_MAX,
                                        f.image_ready, VK_NULL_HANDLE, &img);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR || r->framebuffer_resized) {
        int w=0,h2=0; glfwGetFramebufferSize(r->window, &w, &h2);
        if (w > 0 && h2 > 0) {
            vkDeviceWaitIdle(r->ctx.device);
            recreate_swapchain(r->ctx, r->sc, (uint32_t)w, (uint32_t)h2);
            destroy_image(r->ctx, r->depth);
            create_image_2d(r->ctx, r->sc.extent.width, r->sc.extent.height,
                            VK_FORMAT_D32_SFLOAT,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                            VK_IMAGE_ASPECT_DEPTH_BIT, r->depth);
            r->framebuffer_resized = false;
        }
        return DEMEN_RG_OK;
    }
    vkResetFences(r->ctx.device, 1, &f.in_flight);

    vkResetCommandBuffer(f.cmd, 0);
    record_frame(*r, img, f.cmd, *in, local);

    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &f.image_ready;
    si.pWaitDstStageMask    = &wait;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &f.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &f.render_done;
    vkQueueSubmit(r->ctx.graphics_queue, 1, &si, f.in_flight);

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &f.render_done;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &r->sc.handle;
    pi.pImageIndices      = &img;
    vkQueuePresentKHR(r->ctx.graphics_queue, &pi);

    r->frame_ix = (r->frame_ix + 1) % kFramesInFlight;
    return DEMEN_RG_OK;
}

DEMEN_API int demen_render_graph_poll(demen_render_graph_t h, int* out_should_close) {
    auto* r = rg_from(h);
    if (!r || !out_should_close) return DEMEN_RG_ERR_INVALID_HANDLE;
    glfwPollEvents();
    *out_should_close = glfwWindowShouldClose(r->window) ? 1 : 0;
    return DEMEN_RG_OK;
}

// Phase 0 compatibility shim — keep demen_run_phase0_window linkable so
// older managed code (Program.cs) and the Phase-0 smoke test don't break.
// Tries the new render graph end-to-end: load nothing, spin until closed.
DEMEN_API int demen_run_phase0_window(uint32_t w, uint32_t h) {
    demen_render_graph_t g = 0;
    const int rc = demen_render_graph_create(w, h, &g);
    if (rc != DEMEN_RG_OK) return rc;
    for (;;) {
        int close_now = 0;
        demen_render_graph_poll(g, &close_now);
        if (close_now) break;
        demen_frame_input in{};
        in.window_width = w; in.window_height = h;
        in.camera.pos[2] = 4.0f;
        in.camera.forward[2] = -1.0f;
        in.camera.up[1] = 1.0f;
        in.camera.fov_radians = 1.0f;
        in.camera.aspect = (float)w / (float)h;
        in.camera.near_plane = 0.1f;
        in.camera.far_plane = 1024.0f;
        demen_render_graph_submit_frame(g, &in, nullptr, 0);
    }
    demen_render_graph_destroy(g);
    return DEMEN_WINDOW_OK;
}

} // extern "C"
