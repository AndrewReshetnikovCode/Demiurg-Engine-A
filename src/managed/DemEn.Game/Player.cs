// Fly/walk player controller (§2.7). Swept-AABB against the voxel grid.
// No GC: InputState is a struct, MoveWish is read into locals, collision
// result is a struct, Position is cached.
using System;
using DemEn.Interop;

namespace DemEn.Game;

public sealed class Player
{
    // Eye-centered AABB: 1.6 m tall, 0.6 m wide.
    private static readonly Vec3 HalfExtents = new(0.3f, 0.8f, 0.3f);

    public Vec3  Position { get; private set; }
    public float Yaw   { get; private set; }
    public float Pitch { get; private set; }
    public bool  FlyMode { get; set; } = true;

    private Vec3 _velocity;
    private bool _onGround;

    public Player(Vec3 start) { Position = start; }

    public Vec3 Forward
    {
        get
        {
            float cp = MathF.Cos(Pitch), sp = MathF.Sin(Pitch);
            float cy = MathF.Cos(Yaw),   sy = MathF.Sin(Yaw);
            return new Vec3(cy * cp, sp, sy * cp);
        }
    }

    public void Tick(ulong world, in InputState input, float dt)
    {
        Yaw   = input.YawRadians;
        Pitch = input.PitchRadians;
        FlyMode = input.FlyMode;

        Vec3 fwd = Forward;
        Vec3 right = Vec3.Cross(fwd, Vec3.UnitY).Normalized;
        Vec3 up    = FlyMode ? Vec3.UnitY : Vec3.Zero;

        const float kMove = 10.0f;      // m/s
        Vec3 wish = (right * input.MoveWish.X) + (up * input.MoveWish.Y) + (fwd * input.MoveWish.Z);
        if (FlyMode) _velocity = wish * kMove;
        else
        {
            const float kGrav  = -25.0f;
            _velocity = new Vec3(wish.X * kMove, _velocity.Y + kGrav * dt, wish.Z * kMove);
            if (input.Jump && _onGround) _velocity = new Vec3(_velocity.X, 8.0f, _velocity.Z);
        }

        // Swept-AABB step; apply axis-by-axis so we slide along walls.
        Vec3 step = _velocity * dt;
        Position = SweptMoveAxis(world, Position, new Vec3(step.X, 0, 0));
        Position = SweptMoveAxis(world, Position, new Vec3(0, step.Y, 0));
        Position = SweptMoveAxis(world, Position, new Vec3(0, 0, step.Z));

        _onGround = !FlyMode && _velocity.Y <= 0 &&
                    SolidBelow(world, Position, 0.05f);
    }

    private Vec3 SweptMoveAxis(ulong world, Vec3 pos, Vec3 delta)
    {
        if (delta.X == 0 && delta.Y == 0 && delta.Z == 0) return pos;
        NativeLibrary.Aabb box = new()
        {
            MinX = pos.X - HalfExtents.X, MinY = pos.Y - HalfExtents.Y, MinZ = pos.Z - HalfExtents.Z,
            MaxX = pos.X + HalfExtents.X, MaxY = pos.Y + HalfExtents.Y, MaxZ = pos.Z + HalfExtents.Z,
        };
        unsafe
        {
            float* vel = stackalloc float[3] { delta.X, delta.Y, delta.Z };
            NativeLibrary.SweepAabb(world, &box, vel, out var hit);
            if (hit.Hit == 0)
                return pos + delta;
            float t = MathF.Max(0, hit.Time - 0.001f); // pull back to avoid tunnelling
            return pos + delta * t;
        }
    }

    private static bool SolidBelow(ulong world, Vec3 pos, float epsilon)
    {
        int vx = (int)MathF.Floor((pos.X)             / 2.0f);
        int vy = (int)MathF.Floor((pos.Y - HalfExtents.Y - epsilon) / 2.0f);
        int vz = (int)MathF.Floor((pos.Z)             / 2.0f);
        NativeLibrary.WorldGetVoxel(world, vx, vy, vz, out ushort id);
        return id != 0 && id != 2;
    }
}
