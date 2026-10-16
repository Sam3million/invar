#include <chrono>
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <filesystem>
#include <semaphore>
#include <iostream>

#include "Camera.h"
#include "PerformanceCounter.h"
#include "shared_types.h"
#include "scene/CameraController.h"
#include "scene/Scene.h"

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL Init Failed");
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Invar", 1280, 720,
        SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window)
    {
        SDL_Log("SDL Window Creation Failed");
        return 1;
    }

    SDL_MetalView view = SDL_Metal_CreateView(window);
    auto* layer = static_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(view));

    MTL::Device* device = MTL::CreateSystemDefaultDevice();

    layer->setDevice(device);
    layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
    // keep vsync off for benchmarking purposes:
    layer->setDisplaySyncEnabled(false);

    MTL::CommandQueue* queue = device->newCommandQueue();

    NS::Error* err = nullptr;
    MTL::Library* lib = device->newLibrary(NS::String::string(SHADER_LIB_PATH, NS::UTF8StringEncoding), &err);
    if (!lib)
    {
        std::cout << "Failed to load shader library: " << err->localizedDescription()->utf8String() << "\n";
        return 1;
    }
    MTL::Function* vs = lib->newFunction(NS::String::string("vs_main", NS::UTF8StringEncoding));
    MTL::Function* fs = lib->newFunction(NS::String::string("fs_main", NS::UTF8StringEncoding));

    // Create render pipeline:
    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vs);
    desc->setFragmentFunction(fs);
    desc->colorAttachments()->object(0)->setPixelFormat(layer->pixelFormat());
    desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float_Stencil8);

    MTL::RenderPipelineState* pso = device->newRenderPipelineState(desc, &err);
    if (!pso)
    {
        std::cout << "PSO Creation Failed: " << err->localizedDescription()->utf8String() << "\n";
        return 1;
    }

    MTL::DepthStencilDescriptor* depthStencilDescriptor = MTL::DepthStencilDescriptor::alloc()->init();
    depthStencilDescriptor->setDepthCompareFunction(MTL::CompareFunctionGreater);
    depthStencilDescriptor->setDepthWriteEnabled(true);

    MTL::DepthStencilState* depthStencilState = device->newDepthStencilState(depthStencilDescriptor);
    if (!depthStencilState)
    {
        std::cout << "Depth Stencil State Creation Failed\n";
    }

    desc->release();
    depthStencilDescriptor->release();
    vs->release();
    fs->release();
    lib->release();

    constexpr int framesInFlight = 3;
    std::counting_semaphore<3> inFlight(framesInFlight);
    MTL::Buffer* uniformBuffers[framesInFlight];
    for (int i = 0; i < framesInFlight; i++)
    {
        uniformBuffers[i] = device->newBuffer(sizeof(FrameData), MTL::ResourceStorageModeShared);
    }

    // Render the scene:
    Camera camera
    {
        .transform = Transform
        {
            .position = float3 {9.85929, 1.04801, 0.109808},
            .rotation = quatf {-0.0219285, -0.693247, -0.0211121, 0.720057},
            .scale = float3 {1, 1, 1}
        }
    };
    CameraController controller;
    controller.syncFromCamera(camera);

    MTL::TextureDescriptor* depthTextureDesc = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatDepth32Float_Stencil8, 1280*2, 720*2, false);
    depthTextureDesc->setUsage(MTL::TextureUsageRenderTarget);
    depthTextureDesc->setStorageMode(MTL::StorageModePrivate);
    MTL::Texture* depthTexture = device->newTexture(depthTextureDesc);
    depthTextureDesc->release();

    Scene scene;
    //loadGltf(std::string(ASSET_DIR) + "/models/Box/glTF-Binary/Box.glb", scene);
    //loadGltf(std::string(ASSET_DIR) + "/models/Duck/glTF-Binary/Duck.glb", scene);
    loadGltf(std::string(ASSET_DIR) + "/models/Sponza/glTF/Sponza.gltf", scene);

    auto makeBuf = [&](const void* p, size_t bytes, const char* label) {
        MTL::Buffer* b = device->newBuffer(p, bytes, MTL::ResourceStorageModeShared);
        b->setLabel(NS::String::string(label, NS::UTF8StringEncoding));
        return b;
    };
    MTL::Buffer* vertexBuf   = makeBuf(scene.vertices.data(),  scene.vertices.size()  * sizeof(Vertex),       "Vertices");
    MTL::Buffer* indexBuf    = makeBuf(scene.indices.data(),   scene.indices.size()   * sizeof(uint32_t),     "Indices");
    MTL::Buffer* instanceBuf = makeBuf(scene.instances.data(), scene.instances.size() * sizeof(InstanceData), "Instances");

    bool running = true;
    PerformanceCounter gpuMs("gpu_frame", "ms", 1024);
    PerformanceCounter deltaTimeMs("deltaTime", "ms", 1024);
    PerformanceCounter iterMs("iter", "ms", 1024);
    PerformanceCounter encodeMs("encode", "ms", 1024);
    float gpuMsSlots[3] = {};
    uint64_t frameIndex = 0;
    uint64_t lastTime = SDL_GetPerformanceCounter();
    uint64_t frameStartTime;
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t lastTitleTick = 0;
    while (running)
    {
        frameStartTime = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(frameStartTime - lastTime) / static_cast<float>(freq);

        CameraInput input;

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
                // recreate screen-sized buffers (eg depth)
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

        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);
        controller.update(camera, input, std::min(dt, 0.1f), static_cast<float>(winH));

        if (bool wantCapture = controller.captureMouse(); wantCapture != SDL_GetWindowRelativeMouseMode(window))
        {
            SDL_SetWindowRelativeMouseMode(window, wantCapture);
        }

        auto* pool = NS::AutoreleasePool::alloc()->init();

        inFlight.acquire();
        CA::MetalDrawable* drawable = layer->nextDrawable();
        if (!drawable)
        {
            inFlight.release();
            pool->release();
            std::cout << "nextDrawable null";
            continue;
        }
        if (frameIndex >= 3)
        {
            gpuMs.add(gpuMsSlots[frameIndex % 3]);
        }

        uint64_t encodeStartTime = SDL_GetPerformanceCounter();

        MTL::CommandBuffer* cmd = queue->commandBuffer();
        cmd->setLabel(NS::String::string("Invar Command Buffer", NS::UTF8StringEncoding));
        cmd->addCompletedHandler([&inFlight, &gpuMsSlots, slot = frameIndex % 3](MTL::CommandBuffer* cb)
        {
            double gpuSeconds = cb->GPUEndTime() - cb->GPUStartTime();    // subtract in double
            gpuMsSlots[slot] = static_cast<float>(gpuSeconds * 1000.0);
            inFlight.release();
        });

        MTL::RenderPassDescriptor* rp = MTL::RenderPassDescriptor::renderPassDescriptor();

        auto* depth = rp->depthAttachment();
        depth->setTexture(depthTexture);
        depth->setLoadAction(MTL::LoadActionClear);
        depth->setClearDepth(0.0);
        depth->setStoreAction(MTL::StoreActionDontCare);

        auto* colorAttachment = rp->colorAttachments()->object(0);
        colorAttachment->setTexture(drawable->texture());
        colorAttachment->setLoadAction(MTL::LoadActionClear);
        colorAttachment->setStoreAction(MTL::StoreActionStore);
        colorAttachment->setClearColor(MTL::ClearColor(0.10, 0.12, 0.16, 1.0));

        MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(rp);
        enc->setLabel(NS::String::string("Invar Command Encoder", NS::UTF8StringEncoding));
        enc->setDepthStencilState(depthStencilState);
        enc->setCullMode(MTL::CullModeBack);
        enc->setVertexBuffer(uniformBuffers[frameIndex % 3], 0, BufferIndexFrameData);
        enc->setVertexBuffer(vertexBuf,   0, BufferIndexVertices);
        enc->setVertexBuffer(instanceBuf, 0, BufferIndexInstanceData);

        auto* uniformBuffer = static_cast<FrameData*>(uniformBuffers[frameIndex % 3]->contents());
        uniformBuffer->cameraPos = camera.transform.position;
        uniformBuffer->viewProj = camera.projection() * camera.view();
        uniformBuffer->time = 0;
        uniformBuffer->viewportSize = float2 {static_cast<float>(camera.pixelWidth), static_cast<float>(camera.pixelHeight)};

        enc->setRenderPipelineState(pso);
        // add draws here:
        //enc->drawPrimitives(MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(0), static_cast<NS::UInteger>(3));
        for (uint32_t i = 0; i < scene.instances.size(); i++)
        {
            const MeshInfo& m = scene.meshes[scene.instances[i].meshId];
            enc->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle,
                m.indexCount, MTL::IndexTypeUInt32,
                indexBuf, m.firstIndex * sizeof(uint32_t),
                1,                 // instance count
                m.baseVertex,
                i);                // baseInstance = index into the instance buffer
        }

        enc->endEncoding();
        cmd->presentDrawable(drawable);
        cmd->commit();

        pool->release();

        lastTime = frameStartTime;
        // Frame Statistics:
        deltaTimeMs.add(dt * 1000.0f);
        uint64_t encodeEndTime = SDL_GetPerformanceCounter();
        if ((encodeEndTime - lastTitleTick) > freq / 4)
        {
            float meanCpuMs = deltaTimeMs.mean();
            lastTitleTick = encodeEndTime;
            std::string titleWithPerf = std::format("Invar - CPU Time: {:.2f} ms ({} FPS), GPU Time {:.2f} ms", meanCpuMs, static_cast<int>(std::round(1000.0f / meanCpuMs)), gpuMs.mean());
            if (!SDL_SetWindowTitle(window, titleWithPerf.c_str()))
            {
                SDL_Log("SDL SetWindowTitle failed: %s", SDL_GetError());
            }
        }
        encodeMs.add(float(double(encodeEndTime - encodeStartTime) * 1000.0 / double(freq)));
        iterMs.add(float(double(encodeEndTime - frameStartTime) * 1000.0 / double(freq)));
        frameIndex++;
    }

    // write out benchmark statistics
    deltaTimeMs.writeCsv("perf.csv", "classic");
    iterMs.writeCsv("perf.csv", "classic");
    gpuMs.writeCsv("perf.csv", "classic");
    encodeMs.writeCsv("perf.csv", "classic");

    pso->release();
    depthStencilState->release();
    queue->release();
    device->release();
    SDL_Metal_DestroyView(view);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
