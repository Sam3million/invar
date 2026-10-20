#include "ClassicPath.h"

#include <iostream>

ClassicPath::ClassicPath(MetalContext& ctx) : RenderPath(ctx)
{
    MTL::Function* vs = ctx.lib->newFunction(NS::String::string("vs_main", NS::UTF8StringEncoding));
    MTL::Function* fs = ctx.lib->newFunction(NS::String::string("fs_main", NS::UTF8StringEncoding));

    // Create render pipeline
    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vs);
    desc->setFragmentFunction(fs);
    desc->colorAttachments()->object(0)->setPixelFormat(MetalContext::kColorFormat);
    desc->setDepthAttachmentPixelFormat(MetalContext::kDepthFormat);

    NS::Error* err = nullptr;
    m_pso = ctx.device->newRenderPipelineState(desc, &err);
    if (!m_pso)
    {
        std::cerr << "PSO Creation Failed: " << err->localizedDescription()->utf8String() << "\n";
    }
    desc->release();
    vs->release();
    fs->release();
}

void ClassicPath::setScene(const Scene& scene, const SceneDataGpu& gpu)
{
    m_draws.resize(scene.instances.size());
    for (uint32_t i = 0; i < scene.instances.size(); i++)
    {
        const MeshInfo& m = scene.meshes[scene.instances[i].meshId];
        m_draws[i] = { m.indexCount, m.firstIndex, m.baseVertex, i,
            (scene.materials[m.materialId].flags & MATERIAL_FLAG_DOUBLE_SIDED) != 0 };
    }
}

void ClassicPath::encode(MTL::RenderCommandEncoder* enc, const SceneDataGpu& gpu, const FrameBindings& frame)
{
    enc->setRenderPipelineState(m_pso);
    // Bind buffers
    enc->setVertexBuffer(frame.frameData, 0, BufferIndexFrameData);
    enc->setVertexBuffer(gpu.vertexBuffer, 0, BufferIndexVertexData);
    enc->setVertexBuffer(gpu.instanceBuffer, 0, BufferIndexInstanceData);

    for (uint32_t i = 0; i < m_draws.size(); i++)
    {
        const DrawItem draw = m_draws[i];
        //std::cout << "Double: " << draw.doubleSided << "\n";
        enc->setCullMode(MTL::CullModeBack);
        //enc->setCullMode(draw.doubleSided ? MTL::CullModeNone : MTL::CullModeBack);
        enc->drawIndexedPrimitives(
            MTL::PrimitiveTypeTriangle,
            draw.indexCount,
            MTL::IndexTypeUInt32,
            gpu.indexBuffer,
            draw.firstIndex * sizeof(uint32_t),
            1,
            draw.baseVertex,
            i);
    }
}