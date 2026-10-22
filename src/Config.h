#pragma once
#include <string>

struct Config
{
    std::string scene = "models/Sponza/glTF/Sponza.gltf";
    std::string mode;
    std::string label;
    // benchmark settings:
    uint32_t width = 1920, height = 1080;
    bool bench = false;
    int warmup = 120;
    int frames = 1000;
    int repeats = 5;
    int framesInFlight = 3;
    int captureFrame = -1;
    float3 cameraStartPos = float3 {9.85929, 1.04801, 0.109808};
    quatf cameraStartRot = quatf {-0.0219285, -0.693247, -0.0211121, 0.720057};
};
