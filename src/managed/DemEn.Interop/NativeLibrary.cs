// =============================================================================
// NativeLibrary.cs — single point of contact with demen.dll.
// LibraryImport (source-generated) across the board for AOT compatibility.
// Every import mirrors the matching DEMEN_API entry in src/native/include/.
// =============================================================================
using System.Runtime.InteropServices;

namespace DemEn.Interop;

public static partial class NativeLibrary
{
    private const string Lib = "demen";

    // ---- ABI handshake (invariant #7) ---------------------------------------
    [LibraryImport(Lib, EntryPoint = "demen_abi_version")]
    public static partial uint AbiVersion();

    public const uint ExpectedAbiVersion = 0x00000007u;

    // ---- Legacy Phase 0 window — kept as a compat shim ----------------------
    [LibraryImport(Lib, EntryPoint = "demen_run_phase0_window")]
    public static partial int RunPhase0Window(uint width, uint height);

    // ---- Render graph (Phase 3) ---------------------------------------------
    [LibraryImport(Lib, EntryPoint = "demen_render_graph_create")]
    public static partial int RgCreate(uint width, uint height, out ulong outHandle);

    [LibraryImport(Lib, EntryPoint = "demen_render_graph_destroy")]
    public static partial int RgDestroy(ulong rg);

    [LibraryImport(Lib, EntryPoint = "demen_render_graph_set_atlas")]
    public static partial int RgSetAtlas(ulong rg, ulong atlas);

    [LibraryImport(Lib, EntryPoint = "demen_render_graph_upload_mesh")]
    public static partial int RgUploadMesh(ulong rg, ulong mesh, out uint outSlot);

    [LibraryImport(Lib, EntryPoint = "demen_render_graph_drop_mesh")]
    public static partial int RgDropMesh(ulong rg, uint slot);

    [LibraryImport(Lib, EntryPoint = "demen_render_graph_poll")]
    public static partial int RgPoll(ulong rg, out int outShouldClose);

    [StructLayout(LayoutKind.Sequential)]
    public struct Camera
    {
        public float PosX, PosY, PosZ;
        public float FwdX, FwdY, FwdZ;
        public float UpX,  UpY,  UpZ;
        public float Fov, Aspect, Near, Far;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FrameInput
    {
        public Camera Camera;
        public ulong  Tick;
        public float  TimeSeconds;
        public uint   Width, Height, Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct MeshInstance
    {
        public uint MeshSlot;
        public uint AtlasOverride;
        public fixed float World[16];
    }

    [LibraryImport(Lib, EntryPoint = "demen_render_graph_submit_frame")]
    public static unsafe partial int RgSubmitFrame(
        ulong rg,
        FrameInput* input,
        MeshInstance* instances,
        uint instanceCount);

    // ---- voxel_store (Phase 1) ----------------------------------------------
    [StructLayout(LayoutKind.Sequential)]
    public struct WorldParams
    {
        public ulong WorldId;
        public uint  RngSeed;
        public int   Scale;             // 0 = FINITE_BOUNDED, 1 = STREAMING
        public int   BoundsMinCx, BoundsMinCz, BoundsMaxCx, BoundsMaxCz;
        public int   MinCy, MaxCy;
    }

    [LibraryImport(Lib, EntryPoint = "demen_world_create", StringMarshalling = StringMarshalling.Utf8)]
    public static partial int WorldCreate(string dir, ref WorldParams p, out ulong outWorld);

    [LibraryImport(Lib, EntryPoint = "demen_world_open", StringMarshalling = StringMarshalling.Utf8)]
    public static partial int WorldOpen(string dir, out ulong outWorld);

    [LibraryImport(Lib, EntryPoint = "demen_world_close")]
    public static partial int WorldClose(ulong world);

    [LibraryImport(Lib, EntryPoint = "demen_world_fill_box")]
    public static partial int WorldFillBox(ulong world,
        int x0, int y0, int z0, int x1, int y1, int z1, ushort blockId);

    [LibraryImport(Lib, EntryPoint = "demen_world_set_voxel")]
    public static partial int WorldSetVoxel(ulong world, int x, int y, int z, ushort blockId);

    [LibraryImport(Lib, EntryPoint = "demen_world_get_voxel")]
    public static partial int WorldGetVoxel(ulong world, int x, int y, int z, out ushort outBlockId);

    [LibraryImport(Lib, EntryPoint = "demen_chunk_acquire")]
    public static partial int ChunkAcquire(ulong world, int cx, int cy, int cz, out ulong outChunk);

    [LibraryImport(Lib, EntryPoint = "demen_chunk_release")]
    public static partial int ChunkRelease(ulong chunk);

    // ---- texture_composition (Phase 2) --------------------------------------
    [LibraryImport(Lib, EntryPoint = "demen_atlas_create", StringMarshalling = StringMarshalling.Utf8)]
    public static partial int AtlasCreate(string assetsDir, out ulong outAtlas);

    [LibraryImport(Lib, EntryPoint = "demen_atlas_reload")]
    public static partial int AtlasReload(ulong atlas);

    [LibraryImport(Lib, EntryPoint = "demen_atlas_release")]
    public static partial int AtlasRelease(ulong atlas);

    // ---- meshing (Phase 2) --------------------------------------------------
    [LibraryImport(Lib, EntryPoint = "demen_mesh_build_region")]
    public static unsafe partial int MeshBuildRegion(
        ulong world,
        int cxMin, int cyMin, int czMin,
        int cxMax, int cyMax, int czMax,
        int pass,
        ulong* outMeshes, uint capacity, out uint outCount);

    [LibraryImport(Lib, EntryPoint = "demen_mesh_release")]
    public static partial int MeshRelease(ulong mesh);

    // ---- spatial (Phase 4) --------------------------------------------------
    [StructLayout(LayoutKind.Sequential)]
    public struct Aabb
    {
        public float MinX, MinY, MinZ;
        public float MaxX, MaxY, MaxZ;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct SweepHit
    {
        public int   Hit;
        public float Time;
        public float Nx, Ny, Nz;
        public int   Vx, Vy, Vz;
    }

    [LibraryImport(Lib, EntryPoint = "demen_sweep_aabb")]
    public static unsafe partial int SweepAabb(ulong world, Aabb* box, float* vel, out SweepHit outHit);

    // ---- fluid (Phase 5 — air queries; Phase 6 water; Phase 7 weather) ------
    [LibraryImport(Lib, EntryPoint = "demen_fluid_create")]
    public static partial int FluidCreate(ulong world, out ulong outFluid);

    [LibraryImport(Lib, EntryPoint = "demen_fluid_destroy")]
    public static partial int FluidDestroy(ulong fluid);

    [LibraryImport(Lib, EntryPoint = "demen_fluid_step")]
    public static partial int FluidStep(ulong fluid, float dt, float playerX, float playerY, float playerZ);

    [LibraryImport(Lib, EntryPoint = "demen_fluid_query_wind")]
    public static unsafe partial int FluidQueryWind(ulong fluid, float x, float y, float z, float* outVec3);

    [LibraryImport(Lib, EntryPoint = "demen_fluid_query_temperature")]
    public static partial int FluidQueryTemperature(ulong fluid, float x, float y, float z, out float outKelvin);

    [LibraryImport(Lib, EntryPoint = "demen_fluid_query_rainfall")]
    public static partial int FluidQueryRainfall(ulong fluid, float x, float z, out float outMmPerSec);

    [LibraryImport(Lib, EntryPoint = "demen_fluid_query_water_surface")]
    public static partial int FluidQueryWaterSurface(ulong fluid, float x, float z, out float outY);

    public const int MeshPassOpaque = 0;
}
