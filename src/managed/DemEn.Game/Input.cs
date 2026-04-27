// GLFW input is fetched natively by the render graph poll. The managed side
// reads a flat buffer updated each frame. For Phase 4 the sandbox provides
// a stub: a constant forward-and-rotate-a-bit input so there is *something*
// to render when the operator first runs the build.
//
// Real input wiring (keyboard/mouse read-back via a demen_input_state ABI
// call) lands in the polish pass. The game loop references only this
// interface so swapping it out is mechanical.
namespace DemEn.Game;

public struct InputState
{
    public Vec3  MoveWish;         // x = strafe, y = up/down, z = forward
    public float YawRadians;
    public float PitchRadians;
    public bool  FlyMode;
    public bool  Jump;
    public bool  Quit;
}

public static class Input
{
    private static float _t;

    public static InputState Sample(float dt)
    {
        _t += dt;
        // Default: slowly orbit while staring at the origin.
        return new InputState
        {
            MoveWish     = Vec3.Zero,
            YawRadians   = _t * 1f,
            PitchRadians = -0.15f,
            FlyMode      = true,
            Jump         = false,
            Quit         = false,
        };
    }
}
