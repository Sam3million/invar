#pragma once

#include "MetalContext.h"
#include "RenderPath.h"
#include "shared_types.h"
#include "scene/Scene.h"

inline constexpr uint32_t kMaxFramesInFlight = 3;

enum class RenderMode
{
    Classic, Meshlet, MeshletCulling, Count
};

class Renderer
{
public:
    explicit Renderer(MetalContext& ctx, RenderMode renderMode, int framesInFlight);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void setScene(const Scene& scene);

    void encode(MTL::CommandBuffer* cmd, const MTL::Texture* color, const FrameData& frame, uint32_t frameSlot);

    [[nodiscard]]
    RenderOptions& renderOptions()
    {
        return m_options;
    }

private:
    void ensureDepth(uint32_t width, uint32_t height);

    MetalContext& m_ctx;
    int m_framesInFlight;
    //MTL::RenderPipelineState* m_pso = nullptr;
    MTL::DepthStencilState* m_depthState = nullptr;
    MTL::Texture* m_depthTexture = nullptr;
    std::vector<MTL::Buffer*> m_frameBuffers;
    MTL::SamplerState* m_samplerState = nullptr;
    MTL::Buffer* m_vertexBuffer = nullptr;
    MTL::Buffer* m_indexBuffer = nullptr;
    MTL::Buffer* m_instanceBuffer = nullptr;
    MTL::Buffer* m_materialBuffer = nullptr;
    MTL::Buffer* m_globalTextureBuffer = nullptr;
    std::vector<MTL::Texture*> m_textures;

    SceneDataGpu m_gpu;
    RenderMode m_renderMode = RenderMode::Classic;
    std::array<std::unique_ptr<RenderPath>, static_cast<size_t>(RenderMode::Count)> m_renderPaths;

    RenderOptions m_options;

    [[nodiscard]]
    RenderPath& renderPath() const
    {
        return *m_renderPaths[static_cast<size_t>(m_renderMode)];
    }
};