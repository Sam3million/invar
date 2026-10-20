#include "MeshletPath.h"

#include <iostream>

#include "scene/MeshletData.h"

MeshletPath::MeshletPath(MetalContext& ctx) : RenderPath(ctx)
{
    MTL::Function* ms = ctx.lib->newFunction(NS::String::string("ms_main", NS::UTF8StringEncoding));
    MTL::Function* fs = ctx.lib->newFunction(NS::String::string("fs_main", NS::UTF8StringEncoding));

    // Create render pipeline
    MTL::MeshRenderPipelineDescriptor* desc = MTL::MeshRenderPipelineDescriptor::alloc()->init();
    desc->setMeshFunction(ms);
    desc->setFragmentFunction(fs);
    desc->colorAttachments()->object(0)->setPixelFormat(MetalContext::kColorFormat);
    desc->setDepthAttachmentPixelFormat(MetalContext::kDepthFormat);

    NS::Error* err = nullptr;
    m_pso = ctx.device->newRenderPipelineState(desc, MTL::PipelineOptionNone, nullptr, &err);
    if (!m_pso)
    {
        std::cerr << "PSO Creation Failed: " << err->localizedDescription()->utf8String() << "\n";
    }
    desc->release();
    ms->release();
    fs->release();
}

void MeshletPath::setScene(const Scene& scene, const SceneDataGpu& gpu)
{
    const MeshletData meshletData = buildMeshletData(scene, 0.5);
    //std::cout << "Meshlet Validation: " << validateMeshletData(scene, meshletData) << "\n";

    m_meshlets = m_ctx.createBuffer(meshletData.meshlets.data(), meshletData.meshlets.size() * sizeof(GPUMeshlet), "Meshlets");
    m_meshletVerts = m_ctx.createBuffer(meshletData.vertices.data(), meshletData.vertices.size() * sizeof(uint32_t), "Meshlet Vertices");
    m_meshletTris = m_ctx.createBuffer(meshletData.triangles.data(), meshletData.triangles.size(), "Meshlet Triangles");

    // One work item per (instance, meshlet), in two ranges: single-sided first, then double-sided
    std::vector<MeshletWork> work;
    for (int pass = 0; pass < 2; pass++)
    {
        for (uint32_t i = 0; i < scene.instances.size(); i++)
        {
            const uint32_t meshId = scene.instances[i].meshId;
            const bool dbl = (scene.materials[scene.meshes[meshId].materialId].flags & MATERIAL_FLAG_DOUBLE_SIDED) != 0;
            if (dbl != (pass == 1)) continue;
            const MeshletRange r = meshletData.perMeshMeshletRange[meshId];
            for (uint32_t k = 0; k < r.count; k++) work.push_back({ i, r.first + k });
        }
        if (pass == 0) m_workSingle = static_cast<uint32_t>(work.size());
    }
    m_workDouble = static_cast<uint32_t>(work.size()) - m_workSingle;
    m_work = m_ctx.createBuffer(work.data(), work.size() * sizeof(MeshletWork), "Meshlet Work");
}

void MeshletPath::encode(MTL::RenderCommandEncoder* enc, const SceneDataGpu& gpu, const FrameBindings& frame)
{
    enc->setRenderPipelineState(m_pso);
    enc->setMeshBuffer(frame.frameData, 0, BufferIndexFrameData);
    enc->setMeshBuffer(gpu.vertexBuffer,   0, BufferIndexVertexData);
    enc->setMeshBuffer(gpu.instanceBuffer, 0, BufferIndexInstanceData);
    enc->setMeshBuffer(m_meshlets,  0, BufferIndexMeshlets);
    enc->setMeshBuffer(m_meshletVerts, 0, BufferIndexMeshletVerts);
    enc->setMeshBuffer(m_meshletTris,  0, BufferIndexMeshletTris);

    const MTL::Size noObject(1, 1, 1), meshTG(128, 1, 1);
    enc->setCullMode(MTL::CullModeBack);
    //enc->setMeshBuffer(m_work, 0, BufferIndexWork);
    //enc->drawMeshThreadgroups(MTL::Size(m_workSingle, 1, 1), noObject, meshTG);

    //std::cout << "Single vs double: " << m_workSingle << " " << m_workDouble << "\n";
    if (m_workDouble)
    {
        //enc->setCullMode(MTL::CullModeNone);
        enc->setMeshBuffer(m_work, m_workSingle * sizeof(MeshletWork), BufferIndexWork);
        enc->drawMeshThreadgroups(MTL::Size(m_workDouble, 1, 1), noObject, meshTG);
    }
}
