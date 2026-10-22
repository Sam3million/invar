#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <QuartzCore/QuartzCore.hpp>
#include <semaphore>

#include "Camera.h"
#include "Config.h"
#include "PerformanceCounter.h"
#include "gfx/MetalContext.h"
#include "gfx/Renderer.h"
#include "scene/CameraController.h"

class App
{
public:
    explicit App(const Config& runConfig);
    ~App();
    void run();

private:
    struct PerfSet
    {
        PerformanceCounter gpu, encode, iteration;
        explicit PerfSet(const int frameCount) :
            gpu("gpu_frame", "ms", frameCount),
            encode("encode", "ms", frameCount),
            iteration("iteration", "ms", frameCount) {}
    };

    static RenderMode parseRenderMode(const std::string& configString)
    {
        if (configString == "classic") return RenderMode::Classic;
        if (configString == "meshlet") return RenderMode::Meshlet;
        if (configString == "meshlet-culling") return RenderMode::MeshletCulling;
        return RenderMode::Meshlet;
    }

    void pollEvents(CameraInput& input);
    void update(float dt, const CameraInput& input);
    bool interactiveFrame();
    void submitFrame(MTL::Texture* color, const CA::MetalDrawable* drawable);
    void waitIdle();
    FrameData makeFrameData() const;

    // Benchmark methods:
    bool benchmarkFrame();
    uint64_t warmupFrames() const;
    void writeResults() const;
    void saveTexturePng(const MTL::Texture* tex, const std::string& path) const;

    // For metal capture:
    bool beginCapture();
    void endCapture();
    bool captureRequested_ = false;
    std::string capturePath_;

    MetalContext ctx;
    SDL_Window* window;
    SDL_MetalView view;
    CA::MetalLayer* layer;
    MTL::Texture* benchmarkTexture = nullptr;
    Renderer renderer;
    Scene scene;
    Camera camera;
    CameraController controller;

    std::counting_semaphore<kMaxFramesInFlight> inFlight;
    uint64_t frameIndex = 0;
    bool running = true;

    const Config& config;
    std::vector<float>gpuSlots;
    PerfSet perf;
};
