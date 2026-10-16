#include "CameraController.h"
#include <algorithm>
#include <cmath>

static constexpr float kMaxPitch = 1.55f;
static constexpr float kMinDistance = 0.05f;

static CameraController::Mode pickMode(const CameraInput& in)
{
    using M = CameraController::Mode;
    if (in.alt && in.gui && in.lmb) return M::Pan;
    if (in.alt && in.lmb)           return M::Orbit;
    if (in.alt && in.rmb)           return M::Zoom;
    if (in.rmb)                     return M::Fly;
    if (in.mmb)                     return M::Pan;
    return M::None;
}

void CameraController::applyRotation(Camera& cam) const
{
    cam.transform.rotation = simd_mul(simd_quaternion(yaw, float3{0, 1, 0}),
        simd_quaternion(pitch, float3{1, 0, 0}));
}

void CameraController::syncFromCamera(const Camera& cam)
{
    float3 f = cam.forward();
    yaw = atan2f(f.x, f.z);
    pitch = asinf(std::clamp(-f.y, -1.0f, 1.0f));
}

void CameraController::dolly(Camera& cam, float fraction)
{
    float step = fraction * distance;
    step = std::min(step, distance - kMinDistance);
    cam.transform.position += cam.forward();
    distance -= step;
}

void CameraController::update(Camera& cam, const CameraInput& input, float dt, float viewportHeightPts)
{
    Mode newMode = pickMode(input);
    float2 d = input.mouseDelta;
    if (newMode != mode)
    {
        mode = newMode;
        d = float2 {0, 0};
    }
    // Axes before any rotation this frame (used by pan, which should follow the current view)
    const float3 right = cam.right();
    const float3 up    = cam.up();

    // Scroll dollies, except while flying, where it changes speed instead
    if (mode != Mode::Fly && input.scroll != 0.0f)
        dolly(cam, input.scroll * 0.15f);

    switch (mode) {
    case Mode::Fly: {
        yaw  += d.x * lookSens;
        pitch = std::clamp(pitch + d.y * lookSens, -kMaxPitch, kMaxPitch);
        applyRotation(cam);

        flySpeed = std::clamp(flySpeed * powf(1.15f, input.scroll), 0.05f, 1000.0f);
        float speed = flySpeed * (input.shift ? 3.0f : 1.0f);

        float3 dir = cam.forward() * input.move.z
                   + cam.right()   * input.move.x
                   + float3{0, 1, 0} * input.move.y;      // Q/E along world up
        cam.transform.position += dir * (speed * dt);
        break;
    }
    case Mode::Orbit: {
        float3 pivot = cam.transform.position + cam.forward() * distance;
        yaw  += d.x * lookSens;
        pitch = std::clamp(pitch + d.y * lookSens, -kMaxPitch, kMaxPitch);
        applyRotation(cam);
        cam.transform.position = pivot - cam.forward() * distance;
        break;
    }
    case Mode::Pan: {
        // World size of one screen point at the pivot's depth
        float worldPerPoint = 2.0f * distance * tanf(cam.fovY * 0.5f) / viewportHeightPts;
        cam.transform.position += (-right * d.x + up * d.y) * worldPerPoint;
        break;
    }
    case Mode::Zoom:
        dolly(cam, (d.x - d.y) * 0.005f);       // flip the sign if it feels backwards
        break;
    case Mode::None:
        break;
    }
}

void CameraController::frame(Camera& cam, float3 center, float radius)
{
    distance = std::max(radius / sinf(cam.fovY * 0.5f) * 1.1f, kMinDistance);
    cam.transform.position = center - cam.forward() * distance;
}
