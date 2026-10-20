#pragma once
#include <string>

struct Config
{
    std::string scene = "models/Sponza/glTF/Sponza.gltf";
    std::string mode;
    // benchmark settings:
    uint32_t width = 1920, height = 1080;
    bool bench = false;
    int warmup = 120;
    int frames = 1000;
    int repeats = 5;
    int framesInFlight = 3;
};
