// Tiny value-typed vec3. No allocations; keeps the game loop GC-free
// (invariant #1). A full math library is not needed for Layer 1.
using System;
namespace DemEn.Game;

public readonly struct Vec3
{
    public readonly float X, Y, Z;
    public Vec3(float x, float y, float z) { X = x; Y = y; Z = z; }
    public static Vec3 Zero => new(0, 0, 0);
    public static Vec3 UnitY => new(0, 1, 0);

    public static Vec3 operator +(Vec3 a, Vec3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static Vec3 operator -(Vec3 a, Vec3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static Vec3 operator *(Vec3 a, float s) => new(a.X * s, a.Y * s, a.Z * s);
    public static Vec3 operator *(float s, Vec3 a) => a * s;

    public static float Dot(Vec3 a, Vec3 b) => a.X*b.X + a.Y*b.Y + a.Z*b.Z;
    public static Vec3 Cross(Vec3 a, Vec3 b) =>
        new(a.Y*b.Z - a.Z*b.Y, a.Z*b.X - a.X*b.Z, a.X*b.Y - a.Y*b.X);
    public float Length => MathF.Sqrt(X*X + Y*Y + Z*Z);
    public Vec3 Normalized
    {
        get { float l = Length; return l > 1e-6f ? new Vec3(X/l, Y/l, Z/l) : Zero; }
    }
}
