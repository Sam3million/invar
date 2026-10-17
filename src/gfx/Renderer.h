#pragma once

#include "MetalContext.h"
#include "shared_types.h"
#include "scene/Scene.h"

inline constexpr uint32_t kFramesInFlight = 3;

class Renderer
{
public:
    explicit Renderer(MetalContext& ctx);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void setScene(const Scene& scene);
    void encode(MTL::CommandBuffer* cmd, MTL::Texture* color, const FrameData& frame, uint32_t frameSlot);
    int drawCount() const;
private:
    struct DrawItem
    {
        uint32_t indexCount;
        uint32_t firstIndex;
        int32_t baseVertex;
        uint32_t instance;
    };

    void ensureDepth(uint32_t width, uint32_t height);
    MTL::Buffer* createBuffer(const void* data, size_t length, const char* name) const;

    MetalContext& m_ctx;
    MTL::RenderPipelineState* m_pso = nullptr;
    MTL::DepthStencilState* m_depthState = nullptr;
    MTL::Texture* m_depthTexture = nullptr;
    MTL::Buffer* m_frameBuffers[kFramesInFlight] = {};
    MTL::Buffer* m_vertexBuffer = nullptr;
    MTL::Buffer* m_indexBuffer = nullptr;
    MTL::Buffer* m_instanceBuffer = nullptr;
    std::vector<DrawItem> m_draws;
};