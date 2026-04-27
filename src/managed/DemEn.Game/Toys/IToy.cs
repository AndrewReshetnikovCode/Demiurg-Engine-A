using System;
namespace DemEn.Game.Toys;

public interface IToy
{
    string Name { get; }
    Vec3 Position { get; }

    // Each toy reads from whichever queries it needs: wind, water, etc.
    void Tick(ulong world, ulong fluid, float dt, float time);

    // Produces the world-transform the renderer uses. Column-major 4x4.
    void WriteWorldXform(Span<float> world16);
}
