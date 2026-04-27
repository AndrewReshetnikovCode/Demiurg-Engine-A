// Atmospheric advection + hot-air-rises temperature coupling (§1.5).
using System;
using DemEn.Interop;

namespace DemEn.Game.Toys;

public sealed class Smoke : IToy
{
    public string Name => "smoke";
    public Vec3 Position { get; }

    // A ring buffer of tracer particles that get advected by the wind field.
    private readonly Vec3[] _particles = new Vec3[64];
    private readonly float[] _temps = new float[64];
    private int _nextSpawn;
    private float _spawnTimer;

    public Smoke(Vec3 at) { Position = at; for (int i=0;i<_particles.Length;++i) _particles[i] = at; }

    public void Tick(ulong world, ulong fluid, float dt, float time)
    {
        _spawnTimer += dt;
        if (_spawnTimer > 0.1f)
        {
            _spawnTimer = 0;
            _particles[_nextSpawn] = Position;
            _temps[_nextSpawn] = 800;  // K — hot
            _nextSpawn = (_nextSpawn + 1) % _particles.Length;
        }

        for (int i = 0; i < _particles.Length; ++i)
        {
            unsafe
            {
                float* w = stackalloc float[3];
                NativeLibrary.FluidQueryWind(fluid, _particles[i].X, _particles[i].Y, _particles[i].Z, w);
                float rise = MathF.Max(0, (_temps[i] - 300) / 500.0f);
                _particles[i] = _particles[i] + new Vec3(w[0], w[1] + rise * 2.0f, w[2]) * dt;
                _temps[i] = MathF.Max(290, _temps[i] - 60 * dt);  // cool
            }
        }
    }

    public void WriteWorldXform(Span<float> m)
    {
        // Smoke doesn't have a mesh — Phase 6+ replaces this with a particle
        // renderer path. For now we place the emitter as a marker.
        Sailboat.MakeTranslationYaw(m, Position, 0);
    }
}
