#pragma once
#include "shared_types.h"
#include "MetalContext.h"
#include "scene/Scene.h"

struct SceneDataGpu
{
    MTL::SamplerState* samplerState = nullptr;
    MTL::Buffer* vertexBuffer = nullptr;
    MTL::Buffer* indexBuffer = nullptr;
    MTL::Buffer* instanceBuffer = nullptr;
    MTL::Buffer* materialBuffer = nullptr;
    MTL::Buffer* globalTextureBuffer = nullptr;
    std::vector<MTL::Texture*> textures;
};

struct RenderOptions
{
    bool freezeCulling = false;
    bool cullFrustum = true;
    bool cullCone = true;
    // only for benchmarks:
    bool cullStats = true;
};

struct FrameBindings
{
    MTL::Buffer* frameData;
    uint32_t frameSlot;
    const FrameData* frame;
    const RenderOptions* options;
};

class RenderPath
{
public:
    virtual ~RenderPath()
    {
        m_pso->release();
    }

    [[nodiscard]]
    virtual const char* name() const = 0;

    virtual void init() = 0;

    virtual void setScene(const Scene& scene, const SceneDataGpu& gpu) = 0;

    virtual void prepare(MTL::CommandBuffer*, const SceneDataGpu&, const FrameBindings&) {}

    virtual void encode(MTL::RenderCommandEncoder* enc, const SceneDataGpu& gpu, const FrameBindings& frame) = 0;

protected:
    explicit RenderPath(MetalContext& ctx) : m_ctx(ctx) {}
    MetalContext& m_ctx;
    MTL::RenderPipelineState* m_pso = nullptr;
};
