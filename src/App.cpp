#include "App.h"

#include <filesystem>
#include <iostream>

#include "stb_image_write.h"

App::App(const Config& runConfig) :
    config(runConfig), renderer(ctx, runConfig.framesInFlight), inFlight(runConfig.framesInFlight),
    gpuSlots(runConfig.framesInFlight), perf(runConfig.bench ? runConfig.frames : 2048)
{
    if (!config.bench)
    {
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "%s", SDL_GetError());
            return;
        }
        window = SDL_CreateWindow("Invar", 1280, 720,
            SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window)
        {
            SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "%s", SDL_GetError());
        }

        view = SDL_Metal_CreateView(window);
        layer = static_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(view));
        layer->setDevice(ctx.device);
        layer->setPixelFormat(MetalContext::kColorFormat);
        layer->setDisplaySyncEnabled(false);
    }
    else
    {
        MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(
            MetalContext::kColorFormat, config.width, config.height, false);
        desc->setUsage(MTL::TextureUsageRenderTarget);
        desc->setStorageMode(MTL::StorageModePrivate);
        benchmarkTexture = ctx.device->newTexture(desc);
        desc->release();
    }

    loadGltf(std::string(ASSET_DIR) + config.scene, scene);
    renderer.setScene(scene);

    camera =
    {
        .transform = Transform
        {
            .position = float3 {9.85929, 1.04801, 0.109808},
            .rotation = quatf {-0.0219285, -0.693247, -0.0211121, 0.720057},
            .scale = float3 {1, 1, 1}
        }
    };
    if (config.bench)
    {
        camera.pixelWidth = config.width;
        camera.pixelHeight = config.height;
    }
    controller.syncFromCamera(camera);
}

App::~App()
{
    waitIdle();
    if (benchmarkTexture) benchmarkTexture->release();
    SDL_Metal_DestroyView(view);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void App::run()
{
    uint64_t last = SDL_GetPerformanceCounter();
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t lastTitleTick = last;
    while (running)
    {
        NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

        const uint64_t now = SDL_GetPerformanceCounter();
        const auto dt = static_cast<float>(static_cast<double>(now - last) / static_cast<double>(freq));
        last = now;

        CameraInput input;
        pollEvents(input);
        bool frameSuccessful;
        if (config.bench)
        {
            frameSuccessful = benchmarkFrame();
        }
        else
        {
            update(dt, input);
            frameSuccessful = interactiveFrame();
        }

        uint64_t encodeEndTime = SDL_GetPerformanceCounter();
        if (frameSuccessful && frameIndex >= warmupFrames())
        {
            perf.iteration.add(static_cast<float>(static_cast<double>(encodeEndTime - now) * 1000.0 / static_cast<double>(freq)));
        }

        if (!config.bench && encodeEndTime - lastTitleTick > freq / 4)
        {
            float meanCpuMs = perf.iteration.mean();
            lastTitleTick = encodeEndTime;
            std::string titleWithPerf = std::format("Invar - CPU Time: {:.2f} ms ({} FPS), GPU Time {:.2f} ms",
                meanCpuMs, static_cast<int>(1000.0f / meanCpuMs), perf.gpu.mean());
            if (!SDL_SetWindowTitle(window, titleWithPerf.c_str()))
            {
                SDL_Log("SDL SetWindowTitle failed: %s", SDL_GetError());
            }
        }

        if (config.bench && frameIndex >= warmupFrames() + config.frames)
        {
            running = false;
        }

        pool->release();
    }
    if (config.bench)
    {
        waitIdle();
        std::filesystem::create_directories(BENCH_OUT_DIR);
        saveTexturePng(benchmarkTexture, std::string(BENCH_OUT_DIR) + "/last_frame.png");
        writeResults();
    }
}

void App::pollEvents(CameraInput& input)
{
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        if (e.type == SDL_EVENT_QUIT)
        {
            running = false;
            break;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat)
        {
            if (e.key.key == SDLK_F)
            {
                controller.frame(camera, 0, 10);
            }
            if (e.key.key == SDLK_P)
            {
                std::cout << "Camera Transform: (" << camera.transform.position.x << " " << camera.transform.position.y << " " << camera.transform.position.z << ")\n"
                << camera.transform.rotation.vector.x << " " << camera.transform.rotation.vector.y << " " << camera.transform.rotation.vector.z << " " << camera.transform.rotation.vector.w << ")\n";
            }
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION)
        {
            input.mouseDelta.x += e.motion.xrel;
            input.mouseDelta.y += e.motion.yrel;
        }
        if (e.type == SDL_EVENT_MOUSE_WHEEL)
        {
            input.scroll += e.wheel.y;
        }
        if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        {
            if (!SDL_GetWindowSizeInPixels(window, &camera.pixelWidth, &camera.pixelHeight))
            {
                SDL_Log("SDL Failed to get window pixel size on change.");
            }
            SDL_Log("SDL Window Pixel Size Changed: %i, %i", camera.pixelWidth, camera.pixelHeight);
            layer->setDrawableSize(CGSize(camera.pixelWidth, camera.pixelHeight));
        }
    }

    SDL_MouseButtonFlags buttons = SDL_GetMouseState(nullptr, nullptr);
    input.lmb = buttons & SDL_BUTTON_LMASK;
    input.mmb = buttons & SDL_BUTTON_MMASK;
    input.rmb = buttons & SDL_BUTTON_RMASK;

    SDL_Keymod mods = SDL_GetModState();
    input.alt   = mods & SDL_KMOD_ALT;
    input.shift = mods & SDL_KMOD_SHIFT;
    input.gui   = mods & SDL_KMOD_GUI;

    const bool* keys = SDL_GetKeyboardState(nullptr);
    input.move = float3
    {
        static_cast<float>(keys[SDL_SCANCODE_D]) - static_cast<float>(keys[SDL_SCANCODE_A]),
        static_cast<float>(keys[SDL_SCANCODE_E]) - static_cast<float>(keys[SDL_SCANCODE_Q]),
        static_cast<float>(keys[SDL_SCANCODE_W]) - static_cast<float>(keys[SDL_SCANCODE_S])
    };
}

void App::update(float dt, const CameraInput& input)
{
    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);
    controller.update(camera, input, std::min(dt, 0.1f), static_cast<float>(winH));

    if (bool wantCapture = controller.captureMouse(); wantCapture != SDL_GetWindowRelativeMouseMode(window))
    {
        SDL_SetWindowRelativeMouseMode(window, wantCapture);
    }
}

bool App::interactiveFrame()
{
    inFlight.acquire();
    CA::MetalDrawable* drawable = layer->nextDrawable();
    if (!drawable)
    {
        inFlight.release();
        return false;
    }
    submitFrame(drawable->texture(), drawable);
    return true;
}

void App::submitFrame(MTL::Texture* color, const CA::MetalDrawable* drawable)
{
    const uint32_t slot = frameIndex % config.framesInFlight;

    if (frameIndex >= warmupFrames() + config.framesInFlight) perf.gpu.add(gpuSlots[slot]);

    MTL::CommandBuffer* cmd = ctx.commandQueue->commandBuffer();
    cmd->setLabel(NS::String::string("Invar Command Buffer", NS::UTF8StringEncoding));
    cmd->addCompletedHandler([this, slot](const MTL::CommandBuffer* cb)
    {
        gpuSlots[slot] = static_cast<float>((cb->GPUEndTime() - cb->GPUStartTime()) * 1000.0);
        inFlight.release();
    });

    const uint64_t t0 = SDL_GetPerformanceCounter();

    renderer.encode(cmd, color, makeFrameData(), slot);
    if (drawable) cmd->presentDrawable(drawable);
    cmd->commit();

    const uint64_t t1 = SDL_GetPerformanceCounter();
    const uint64_t freq = SDL_GetPerformanceFrequency();
    if (frameIndex >= warmupFrames())
        perf.encode.add(static_cast<float>(static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(freq)));

    frameIndex++;
}

FrameData App::makeFrameData() const
{
    const FrameData frameData =
    {
        .viewProj = camera.projection() * camera.view(),
        .cameraPos = camera.transform.position,
        .viewportSize = {static_cast<float>(camera.pixelWidth), static_cast<float>(camera.pixelHeight)},
        .time = 0
    };
    return frameData;
}

void App::waitIdle()
{
    // this just gets rid of any processing frames
    for (uint32_t i = 0; i < config.framesInFlight; i++) inFlight.acquire();
    for (uint32_t i = 0; i < config.framesInFlight; i++) inFlight.release();
}

bool App::benchmarkFrame()
{
    inFlight.acquire();
    submitFrame(benchmarkTexture, nullptr);
    return true;
}

uint64_t App::warmupFrames() const
{
    return config.bench ? config.warmup : 0;
}

void App::writeResults() const
{
    const std::string path = std::string(BENCH_OUT_DIR) + "/perf.csv";
    const auto pathStr = path.c_str();
    const auto labelStr = config.label.c_str();
    perf.gpu.writeCsv(pathStr, labelStr);
    perf.encode.writeCsv(pathStr, labelStr);
    perf.iteration.writeCsv(pathStr, labelStr);
}

void App::saveTexturePng(const MTL::Texture* tex, const std::string& path) const
{
    const NS::UInteger w = tex->width(), h = tex->height();
    const NS::UInteger bytesPerRow = w * 4;
    MTL::Buffer* buffer = ctx.device->newBuffer(bytesPerRow * h, MTL::ResourceStorageModeShared);

    MTL::CommandBuffer* cmd = ctx.commandQueue->commandBuffer();
    MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
    blit->copyFromTexture(tex, 0, 0, MTL::Origin(0, 0, 0),
        MTL::Size(w, h, 1), buffer, 0, bytesPerRow, bytesPerRow * h);
    blit->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();

    auto* bufferData = static_cast<uint8_t*>(buffer->contents());
    for (NS::UInteger i = 0; i < w * h; i++)
    {
        // switch from bgra to rgba for png
        std::swap(bufferData[i * 4 + 0], bufferData[i * 4 + 2]);
    }
    stbi_write_png(path.c_str(), static_cast<int>(w), static_cast<int>(h), 4, bufferData, static_cast<int>(bytesPerRow));
    buffer->release();
}