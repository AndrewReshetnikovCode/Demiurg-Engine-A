// Per-voxel wind interpolation, drag, drift (§1.5).
using System;
using DemEn.Interop;

namespace DemEn.Game.Toys;

public sealed class FallingLeaf : IToy
{
    public string Name => "leaf";
    public Vec3 Position { get; private set; }
    private Vec3 _velocity;
    private readonly float _mass;
    private readonly Random _rng;

    public FallingLeaf(Vec3 at, int seed) { Position = at; _mass = 0.01f; _rng = new Random(seed); }

    public void Tick(ulong world, ulong fluid, float dt, float time)
    {
        unsafe
        {
            float* w = stackalloc float[3];
            NativeLibrary.FluidQueryWind(fluid, Position.X, Position.Y, Position.Z, w);
            Vec3 wind = new(w[0], w[1], w[2]);
            Vec3 gravity = new(0, -9.81f, 0);
            Vec3 drag = (wind - _velocity) * 0.8f;   // aerodynamic drag
            _velocity = _velocity + (gravity + drag) * dt;
            _velocity = _velocity + new Vec3((float)_rng.NextDouble() * 0.1f - 0.05f, 0,
                                             (float)_rng.NextDouble() * 0.1f - 0.05f);
            Position = Position + _velocity * dt;
            // Respawn when below ground.
            NativeLibrary.WorldGetVoxel(world, (int)MathF.Floor(Position.X / 2), (int)MathF.Floor(Position.Y / 2), (int)MathF.Floor(Position.Z / 2), out var id);
            if (id != 0 && id != 2)
            {
                Position = new Vec3(Position.X, Position.Y + 40, Position.Z);
                _velocity = Vec3.Zero;
            }
        }
    }

    public void WriteWorldXform(Span<float> m) => Sailboat.MakeTranslationYaw(m, Position, 0);
}
