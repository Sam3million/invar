#include "MeshletCullingPath.h"

#include <iostream>

void MeshletCullingPath::init()
{
    NS::Error* err = nullptr;

    MTL::Function* os = m_ctx.lib->newFunction(NS::String::string("os_main", NS::UTF8StringEncoding), m_ctx.constantValues, &err);
    MTL::Function* ms = m_ctx.lib->newFunction(NS::String::string("ms_main_culling", NS::UTF8StringEncoding), m_ctx.constantValues, &err);
    MTL::Function* fs = m_ctx.lib->newFunction(NS::String::string("fs_main", NS::UTF8StringEncoding));

    // Create render pipeline
    MTL::MeshRenderPipelineDescriptor* desc = MTL::MeshRenderPipelineDescriptor::alloc()->init();
    desc->setObjectFunction(os);
    desc->setMeshFunction(ms);
    desc->setFragmentFunction(fs);
    desc->colorAttachments()->object(0)->setPixelFormat(MetalContext::kColorFormat);
    desc->setDepthAttachmentPixelFormat(MetalContext::kDepthFormat);

    m_pso = m_ctx.device->newRenderPipelineState(desc, MTL::PipelineOptionNone, nullptr, &err);
    if (!m_pso)
    {
        std::cerr << "PSO Creation Failed: " << err->localizedDescription()->utf8String() << "\n";
    }
    desc->release();
    ms->release();
    fs->release();
}

std::string MeshletCullingPath::statsText() const
{
    if (m_lastSubmitted == 0) return {};
    return std::format("meshlets {}/{} ({:.0f}% culled)", m_lastSurvived, m_lastSubmitted,
        100.0 * (1.0 - static_cast<double>(m_lastSurvived) / static_cast<double>(m_lastSubmitted)));
}

void MeshletCullingPath::setScene(const Scene& scene, const SceneDataGpu& gpu)
{
    MeshletPath::setScene(scene, gpu);
    constexpr uint32_t empty[4] = {};
    for (auto& buffer : m_stats) buffer = m_ctx.createBuffer(empty, sizeof(empty), "Cull Stats");
}

static CullParams makeCullParams(const float4x4& vp, float3 camPos)
{
    auto row = [&](int i) { return float4{ vp.columns[0][i], vp.columns[1][i], vp.columns[2][i], vp.columns[3][i] }; };
    const float4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

    CullParams cp = {};
    cp.planes[0] = r3 + r0;    // left
    cp.planes[1] = r3 - r0;    // right
    cp.planes[2] = r3 + r1;    // bottom
    cp.planes[3] = r3 - r1;    // top
    cp.planes[4] = r3 - r2;    // near (reversed-Z: z <= w)
    for (auto& p : cp.planes)
        p /= length(float3{ p.x, p.y, p.z });          // normalize so plane distances are real units
    cp.cameraPos = float4{ camPos.x, camPos.y, camPos.z, 0.0f };
    return cp;
}

void MeshletCullingPath::encode(MTL::RenderCommandEncoder* enc, const SceneDataGpu& gpu, const FrameBindings& frame)
{
    const RenderOptions& opt = *frame.options;

    if (!opt.freezeCulling || !m_haveCull)
    {
        m_cullParams = makeCullParams(frame.frame->viewProj, frame.frame->cameraPos);
        m_haveCull = true;
    }
    const uint32_t flags =
        (opt.cullFrustum ? CULL_FRUSTUM : 0u) |
        (opt.cullCone ? CULL_CONE : 0u) |
        (opt.cullStats ? CULL_STATS : 0u);

    // reset culling stats:
    /*auto* s = static_cast<uint32_t*>(m_stats[frame.frameSlot]->contents());
    m_lastSubmitted = s[0];
    m_lastSurvived  = s[1];
    s[0] = 0;
    s[1] = 0;*/

    bindMeshStage(enc, gpu, frame);
    enc->setObjectBuffer(m_meshlets, 0, BufferIndexMeshlets);
    enc->setObjectBuffer(gpu.instanceBuffer, 0, BufferIndexInstanceData);
    //enc->setObjectBuffer(m_stats[frame.frameSlot], 0, BufferIndexStats);
    enc->setObjectBytes(&m_cullParams, sizeof(m_cullParams), BufferIndexCullParams);

    drawRange(enc, 0, m_workSingle, flags, MTL::CullModeBack);
    drawRange(enc, m_workSingle, m_workDouble, flags & ~CULL_CONE, MTL::CullModeNone);
}

void MeshletCullingPath::drawRange(MTL::RenderCommandEncoder* enc, const uint32_t first, const uint32_t count,
    const uint32_t flags, const MTL::CullMode mode) const
{
    if (count == 0) return;
    CullParams cp = m_cullParams;
    cp.workCount = count;
    cp.flags = flags;

    const uint32_t offset = first * sizeof(MeshletWork);
    enc->setCullMode(mode);
    enc->setObjectBytes(&cp, sizeof(cp), BufferIndexCullParams);
    enc->setObjectBuffer(m_work, offset, BufferIndexWork);
    enc->setMeshBuffer(m_work, offset, BufferIndexWork);
    enc->drawMeshThreadgroups(MTL::Size((count + 31) / 32, 1, 1),
        MTL::Size(32, 1, 1),
        MTL::Size(MESH_THREADS, 1, 1));
}
