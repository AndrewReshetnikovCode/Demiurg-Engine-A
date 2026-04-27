// Directional wind sampling, visible at distance (§1.5).
using System;
using DemEn.Interop;

namespace DemEn.Game.Toys;

public sealed class Flag : IToy
{
    public string Name => "flag";
    public Vec3 Position { get; }
    private float _windAngle;

    public Flag(Vec3 at) { Position = at; }

    public void Tick(ulong world, ulong fluid, float dt, float time)
    {
        unsafe
        {
            float* w = stackalloc float[3];
            NativeLibrary.FluidQueryWind(fluid, Position.X, Position.Y, Position.Z, w);
            float target = MathF.Atan2(w[2], w[0]);
            // Critically-damped rotation toward wind direction.
            float diff = WrapPi(target - _windAngle);
            _windAngle += diff * MathF.Min(1.0f, dt * 4.0f);
        }
    }

    public void WriteWorldXform(Span<float> m) => Sailboat.MakeTranslationYaw(m, Position, _windAngle);

    private static float WrapPi(float r)
    {
        while (r >  MathF.PI) r -= 2 * MathF.PI;
        while (r < -MathF.PI) r += 2 * MathF.PI;
        return r;
    }
}
