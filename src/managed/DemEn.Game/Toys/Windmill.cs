// Wind force -> mechanical feedback (§1.5).
using System;
using DemEn.Interop;

namespace DemEn.Game.Toys;

public sealed class Windmill : IToy
{
    public string Name => "windmill";
    public Vec3 Position { get; }
    public float BladeAngle { get; private set; }
    private float _bladeSpeed;

    public Windmill(Vec3 at) { Position = at; }

    public void Tick(ulong world, ulong fluid, float dt, float time)
    {
        unsafe
        {
            float* w = stackalloc float[3];
            NativeLibrary.FluidQueryWind(fluid, Position.X, Position.Y + 6.0f, Position.Z, w);
            float speed = MathF.Sqrt(w[0]*w[0] + w[2]*w[2]);
            // Torque proportional to wind speed; first-order drag.
            _bladeSpeed += speed * 0.3f * dt;
            _bladeSpeed *= MathF.Exp(-0.5f * dt);
            BladeAngle += _bladeSpeed * dt;
        }
    }

    public void WriteWorldXform(Span<float> m) => Sailboat.MakeTranslationYaw(m, Position, BladeAngle);
}
