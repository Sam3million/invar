#pragma once
#include "RenderPath.h"

class ClassicPath final : public RenderPath
{
public:
    explicit ClassicPath(MetalContext& ctx) : RenderPath(ctx) {};

    [[nodiscard]]
    const char* name() const override
    {
        return "classic";
    }

    void init() override;

    void setScene(const Scene& scene, const SceneDataGpu& gpu) override;

    void encode(MTL::RenderCommandEncoder* enc, const SceneDataGpu& gpu, const FrameBindings& frame) override;

private:
    struct DrawItem
    {
        uint32_t indexCount;
        uint32_t firstIndex;
        int32_t baseVertex;
        uint32_t instance;
        bool doubleSided;
    };

    std::vector<DrawItem> m_draws;
};
