#pragma once
#include "MeshletPath.h"
#include "Renderer.h"

class MeshletCullingPath final : public MeshletPath
{
public:
    explicit MeshletCullingPath(MetalContext& ctx) : MeshletPath(ctx) { }

    [[nodiscard]]
    const char* name() const override
    {
        return "meshlet-culling";
    }

    [[nodiscard]]
    std::string statsText() const;

    void setScene(const Scene& scene, const SceneDataGpu& gpu) override;

    void init() override;

    void encode(MTL::RenderCommandEncoder* enc, const SceneDataGpu& gpu, const FrameBindings& frame) override;

private:
    MTL::Buffer* m_stats[kMaxFramesInFlight] = {};
    CullParams m_cullParams = {};
    bool m_haveCull = false;
    uint32_t m_lastSubmitted = 0;
    uint32_t m_lastSurvived = 0;

    void drawRange(MTL::RenderCommandEncoder* enc, uint32_t first, uint32_t count, uint32_t flags, MTL::CullMode mode) const;
};
