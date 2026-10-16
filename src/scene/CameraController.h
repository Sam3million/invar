#pragma once

#include "Camera.h"

struct CameraInput
{
    float2 mouseDelta = {0, 0};
    float scroll = 0;
    float3 move = {0, 0, 0};
    bool lmb = false, mmb = false, rmb = false;
    bool alt = false, shift = false, gui = false;
};

struct CameraController
{
    enum class Mode { None, Fly, Orbit, Pan, Zoom };

    Mode mode = Mode::None;
    float yaw = 0, pitch = 0;
    float distance = 5;
    float flySpeed = 5;
    float lookSens = 0.0025f;

    void syncFromCamera(const Camera& cam);
    void update(Camera& cam, const CameraInput& input, float dt, float viewportHeightPts);
    void frame(Camera& cam, float3 center, float radius);
    [[nodiscard]] bool captureMouse() const { return mode != Mode::None; }

private:
    void applyRotation(Camera& cam) const;
    void dolly(Camera& cam, float fractionOfDistance);
};