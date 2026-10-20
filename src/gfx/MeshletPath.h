#pragma once
#include "RenderPath.h"

class MeshletPath : public RenderPath
{
public:
    explicit MeshletPath(MetalContext& ctx);

    [[nodiscard]]
    const char* name() const override
    {
        return "meshlet";
    }

    void setScene(const Scene& scene, const SceneDataGpu& gpu) override;

    void encode(MTL::RenderCommandEncoder* enc, const SceneDataGpu& gpu, const FrameBindings& frame) override;

protected:
    MTL::Buffer* m_meshlets = nullptr;
    MTL::Buffer* m_meshletVerts = nullptr;
    MTL::Buffer* m_meshletTris = nullptr;
    MTL::Buffer* m_work = nullptr;
    uint32_t m_workSingle = 0;
    uint32_t m_workDouble = 0;
};
