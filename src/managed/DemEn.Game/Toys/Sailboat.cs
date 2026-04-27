// Proves wave buoyancy + wind-force-on-sail coupling (§1.5).
using System;
using DemEn.Interop;

namespace DemEn.Game.Toys;

public sealed class Sailboat : IToy
{
    public string Name => "sailboat";
    public Vec3 Position { get; private set; }
    public float Heading { get; private set; }
    private Vec3 _velocity;

    public Sailboat(Vec3 start) { Position = start; }

    public void Tick(ulong world, ulong fluid, float dt, float time)
    {
        // Water surface height under the boat (Phase 6 fills this in; returns
        // y = 0 for now via the fluid stub).
        NativeLibrary.FluidQueryWaterSurface(fluid, Position.X, Position.Z, out float waterY);

        // Wind vector at the mast top (Phase 5 fills this in).
        unsafe
        {
            float* wind = stackalloc float[3];
            NativeLibrary.FluidQueryWind(fluid, Position.X, Position.Y + 2.0f, Position.Z, wind);
            // Simple sail force: project wind onto sail plane and accelerate
            // along the boat's forward axis.
            Vec3 w = new(wind[0], 0, wind[2]);
            Vec3 fwd = new(MathF.Cos(Heading), 0, MathF.Sin(Heading));
            float dot = Vec3.Dot(w, fwd);
            _velocity = _velocity * 0.95f + fwd * (dot * 0.1f * dt);
        }

        // Buoyancy: snap Y toward the water surface.
        var p = Position + _velocity * dt;
        Position = new Vec3(p.X, waterY + 0.2f, p.Z);

        // Very gentle yaw wobble so the boat doesn't look dead when wind is 0.
        Heading += MathF.Sin(time * 0.3f) * 0.01f;

    }

    public void WriteWorldXform(Span<float> m)
    {
        MakeTranslationYaw(m, Position, Heading);
    }

    internal static void MakeTranslationYaw(Span<float> m, Vec3 p, float yaw)
    {
        // Column-major.
        float c = MathF.Cos(yaw), s = MathF.Sin(yaw);
        m[0] =  c; m[1] = 0; m[2] =  s; m[3]  = 0;
        m[4] =  0; m[5] = 1; m[6] =  0; m[7]  = 0;
        m[8] = -s; m[9] = 0; m[10]=  c; m[11] = 0;
        m[12]=p.X; m[13]=p.Y; m[14]=p.Z; m[15] = 1;
    }
}
