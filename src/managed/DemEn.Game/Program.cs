// =============================================================================
// Program.cs — Layer 1 game loop.
// Fixed-tick sim + variable-rate render; invariant #1 (zero managed allocs
// in the game loop after warmup) and #2 (deterministic at fixed tick rate).
// =============================================================================
using System;
using System.IO;
using DemEn.Interop;
using DemEn.Game.Toys;
using System.Formats.Asn1;

namespace DemEn.Game;

internal static class Program
{
    private const uint DefaultWidth  = 1280;
    private const uint DefaultHeight = 720;
    private const float FixedDt = 1.0f / 60.0f;

    private static unsafe int Main(string[] args)
    {
        uint abi = NativeLibrary.AbiVersion();
        if (abi != NativeLibrary.ExpectedAbiVersion)
        {
            Console.Error.WriteLine($"DemEn: ABI mismatch 0x{abi:X8} != 0x{NativeLibrary.ExpectedAbiVersion:X8}");
            return 2;
        }
        Console.WriteLine("DemEn Layer 1 — voxel simulation engine");
        Console.WriteLine("(Phase 0..7 landed in sandbox. First real build is the debug pass.)");
        Console.WriteLine($"DemEn Layer 1. ABI = 0x{abi:X8}");

        // --- Render graph --------------------------------------------------
        int rgRc = NativeLibrary.RgCreate(DefaultWidth, DefaultHeight, out ulong rg);
        if (rgRc != 0 || rg == 0)
        {
            Console.Error.WriteLine($"DemEn: render graph init failed rc={rgRc}");
            return 3;
        }

        // --- World + atlas -------------------------------------------------
        var worldDir = Path.Combine(AppContext.BaseDirectory, "world_layer1");
        Directory.CreateDirectory(worldDir);
        NativeLibrary.WorldParams wp = new()
        {
            WorldId = 0xDE_AE_01ul,
            RngSeed = 0xCAFE_F00Du,
            Scale = 0,           // FINITE_BOUNDED
            BoundsMinCx = -4, BoundsMinCz = -4,
            BoundsMaxCx =  4, BoundsMaxCz =  4,
            MinCy = 0, MaxCy = 3,
        };
        NativeLibrary.WorldCreate(worldDir, ref wp, out ulong world);

        // A simple starting ground: stone slab y in [0, 2 voxels], water y in
        // [3, 5 voxels] around the origin to seed the sailboat.
        NativeLibrary.WorldFillBox(world, -64, 0, -64,   64, 8, 64, 1);    // stone
        NativeLibrary.WorldFillBox(world, -32, 8, -32,   32, 12, 32, 0);   // air above
        NativeLibrary.WorldFillBox(world, -16, 8, -16,   16, 11, 16, 2);   // shallow water

        // Atlas: point at the already-built assets/textures/backgrounds/.
        var assets = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "..", "assets", "textures", "backgrounds");
        if (Directory.Exists(assets) &&
            NativeLibrary.AtlasCreate(assets, out ulong atlas) == 0)
        {
            NativeLibrary.RgSetAtlas(rg, atlas);
        }

        // --- Mesh the region ---------------------------------------------
        const int rad = 4, cyMin = 0, cyMax = 3;
        int chunkCount = (2*rad+1) * (2*rad+1) * (cyMax-cyMin+1);
        ulong* meshArr = stackalloc ulong[chunkCount];
        uint produced = 0;
        NativeLibrary.MeshBuildRegion(
            world, -rad, cyMin, -rad, rad, cyMax, rad,
            NativeLibrary.MeshPassOpaque,
            meshArr, (uint)chunkCount, out produced);

        uint[] meshSlots = new uint[produced];
        for (uint i = 0; i < produced; ++i)
        {
            NativeLibrary.RgUploadMesh(rg, meshArr[i], out meshSlots[i]);
            NativeLibrary.MeshRelease(meshArr[i]);
        }

        // --- Fluid sim + toys -------------------------------------------
        NativeLibrary.FluidCreate(world, out ulong fluid);

        var player = new Player(new Vec3(0, 24, 0));
        IToy[] toys = new IToy[]
        {
            new Sailboat(new Vec3(  0, 22,   0)),
            new Flag    (new Vec3( 20, 20,   0)),
            new Smoke   (new Vec3(-20, 20,   0)),
            new Windmill(new Vec3(  0, 20,  20)),
            new FallingLeaf(new Vec3( 10, 30, 10), seed: 1),
            new FallingLeaf(new Vec3(-10, 30,-10), seed: 2),
            new FallingLeaf(new Vec3(  6, 30, -6), seed: 3),
        };

        // Instance buffer — pre-sized, reused every frame (invariant #1).
        NativeLibrary.MeshInstance[] instances =
            new NativeLibrary.MeshInstance[meshSlots.Length + toys.Length];

        ulong tick = 0;
        float t = 0;
        DateTime started = DateTime.UtcNow;

        while (true)
        {
            int close = 0;
            NativeLibrary.RgPoll(rg, out close);
            if (close != 0) break;

            // --- Sim step at fixed dt (invariant #2) -------------------
            InputState input = Input.Sample(FixedDt);
            if (input.Quit) break;
            player.Tick(world, input, FixedDt);
            NativeLibrary.FluidStep(fluid, FixedDt, player.Position.X, player.Position.Y, player.Position.Z);
            foreach (var toy in toys) toy.Tick(world, fluid, FixedDt, t);
            tick++;
            t += FixedDt;

            // --- Frame submission --------------------------------------
            NativeLibrary.FrameInput fi = new()
            {
                Camera = new NativeLibrary.Camera
                {
                    PosX = player.Position.X, PosY = player.Position.Y + 1.6f, PosZ = player.Position.Z,
                    FwdX = player.Forward.X, FwdY = player.Forward.Y, FwdZ = player.Forward.Z,
                    UpX = 0, UpY = 1, UpZ = 0,
                    Fov = 1.3f, Aspect = (float)DefaultWidth / DefaultHeight,
                    Near = 0.1f, Far = 2048.0f,
                },
                Tick = tick, TimeSeconds = t,
                Width = DefaultWidth, Height = DefaultHeight,
            };

            // Chunk instances at identity transform.
            for (int i = 0; i < meshSlots.Length; ++i)
            {
                instances[i].MeshSlot      = meshSlots[i];
                instances[i].AtlasOverride = uint.MaxValue;
                unsafe { fixed (NativeLibrary.MeshInstance* p = &instances[i])WriteIdentity(p->World); }
            }
            // Toy instances — these currently render as the chunk-slot mesh
            // at the toy's transform, because Phase 4 does not yet ship the
            // toy meshes. That's a known scope cut; the Phase 8 polish pass
            // replaces the chunk-mesh placeholder with proper toy GLTF meshes.
            for (int i = 0; i < toys.Length; ++i)
            {
                int idx = meshSlots.Length + i;
                instances[idx].MeshSlot = meshSlots.Length > 0 ? meshSlots[0] : 0;
                instances[idx].AtlasOverride = uint.MaxValue;
                unsafe
                {
                    fixed (NativeLibrary.MeshInstance* p = &instances[idx])
                    {
                        Span<float> m = new Span<float>(p->World, 16);
                        toys[i].WriteWorldXform(m);
                    }
                }
            }

            fixed (NativeLibrary.MeshInstance* inst = instances)
            {
                NativeLibrary.RgSubmitFrame(rg, &fi, inst, (uint)instances.Length);
            }
        }

        Console.WriteLine($"DemEn: ran {tick} ticks in {(DateTime.UtcNow - started).TotalSeconds:F1}s");

        for (uint i = 0; i < produced; ++i) NativeLibrary.RgDropMesh(rg, meshSlots[i]);
        NativeLibrary.FluidDestroy(fluid);
        NativeLibrary.WorldClose(world);
        NativeLibrary.RgDestroy(rg);
        return 0;
    }

    private static unsafe void WriteIdentity(float* m)
    {
        for (int i = 0; i < 16; ++i) m[i] = 0;
        m[0] = m[5] = m[10] = m[15] = 1;
    }
}
