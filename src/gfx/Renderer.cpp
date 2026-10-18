#include "Renderer.h"

#include <iostream>
#include "shared_types.h"

Renderer::Renderer(MetalContext& ctx, const int framesInFlight) :
    m_ctx(ctx), m_framesInFlight(framesInFlight), m_frameBuffers(framesInFlight)
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

    MTL::DepthStencilDescriptor* depthStencilDescriptor = MTL::DepthStencilDescriptor::alloc()->init();
    depthStencilDescriptor->setDepthCompareFunction(MTL::CompareFunctionGreater);
    depthStencilDescriptor->setDepthWriteEnabled(true);

    m_depthState = ctx.device->newDepthStencilState(depthStencilDescriptor);
    if (!m_depthState)
    {
        std::cerr << "Depth Stencil State Creation Failed\n";
    }
    depthStencilDescriptor->release();

    MTL::SamplerDescriptor* samplerDescriptor = MTL::SamplerDescriptor::alloc()->init();
    samplerDescriptor->setMinFilter(MTL::SamplerMinMagFilterLinear);
    samplerDescriptor->setMagFilter(MTL::SamplerMinMagFilterLinear);
    samplerDescriptor->setMaxAnisotropy(16);
    samplerDescriptor->setSAddressMode(MTL::SamplerAddressModeRepeat);
    samplerDescriptor->setTAddressMode(MTL::SamplerAddressModeRepeat);
    m_samplerState = ctx.device->newSamplerState(samplerDescriptor);
    if (!m_samplerState)
    {
        std::cerr << "Sampler State Creation Failed\n";
    }
    samplerDescriptor->release();

    for (int i = 0; i < m_framesInFlight; i++)
    {
        m_frameBuffers[i] = ctx.device->newBuffer(sizeof(FrameData), MTL::ResourceStorageModeShared);
    }

    vs->release();
    fs->release();
}

Renderer::~Renderer()
{
    m_pso->release();
    m_depthState->release();
    m_samplerState->release();
    m_depthTexture->release();
    m_vertexBuffer->release();
    m_indexBuffer->release();
    m_instanceBuffer->release();
    m_materialBuffer->release();
    m_globalTextureBuffer->release();
    for (int i = 0; i < m_framesInFlight; i++)
    {
        m_frameBuffers[i]->release();
    }
}

/// Creates a buffer from existing data
MTL::Buffer* Renderer::createBuffer(const void* data, size_t length, const char* name) const
{
    auto* buffer = m_ctx.device->newBuffer(data, length, MTL::ResourceStorageModeShared);
    buffer->setLabel(NS::String::string(name, NS::UTF8StringEncoding));
    return buffer;
}

void Renderer::setScene(const Scene& scene)
{
    if (m_vertexBuffer) m_vertexBuffer->release();
    if (m_indexBuffer) m_indexBuffer->release();
    if (m_instanceBuffer) m_instanceBuffer->release();
    if (m_materialBuffer) m_materialBuffer->release();
    m_draws.resize(scene.instances.size());

    m_vertexBuffer = createBuffer(scene.vertices.data(), scene.vertices.size() * sizeof(Vertex), "Vertex Buffer");
    m_indexBuffer = createBuffer(scene.indices.data(), scene.indices.size() * sizeof(uint32_t), "Index Buffer");
    m_instanceBuffer = createBuffer(scene.instances.data(), scene.instances.size() * sizeof(InstanceData), "Instance Buffer");
    m_materialBuffer = createBuffer(scene.materials.data(), scene.materials.size() * sizeof(Material), "Material Buffer");

    for (uint32_t i = 0; i < scene.instances.size(); i++)
    {
        const MeshInfo& m = scene.meshes[scene.instances[i].meshId];
        m_draws[i] = { m.indexCount, m.firstIndex, m.baseVertex, i,
            (scene.materials[m.materialId].flags & MATERIAL_FLAG_DOUBLE_SIDED) != 0 };
    }

    // Create the metal texture objects
    for (const Image& image : scene.images)
    {
        MTL::TextureDescriptor* td = MTL::TextureDescriptor::texture2DDescriptor(
            MTL::PixelFormatRGBA8Unorm_sRGB, image.width, image.height, true);
        td->setUsage(MTL::TextureUsageShaderRead);
        td->setStorageMode(MTL::StorageModeShared);
        MTL::Texture* texture = m_ctx.device->newTexture(td);
        texture->replaceRegion(MTL::Region(0, 0, image.width, image.height),
            0, image.rgba.data(), image.width * 4);
        m_textures.push_back(texture);
        td->release();
    }

    // Generate mipmaps
    MTL::CommandBuffer* cb = m_ctx.commandQueue->commandBuffer();
    MTL::BlitCommandEncoder* blit = cb->blitCommandEncoder();
    for (const auto* texture : m_textures) blit->generateMipmaps(texture);
    blit->endEncoding();
    cb->commit();
    cb->waitUntilCompleted();

    // Bindless texture buffer
    m_globalTextureBuffer = m_ctx.device->newBuffer(sizeof(MTL::ResourceID) * MAX_TEXTURES,
        MTL::ResourceStorageModeShared);
    auto* ptr = static_cast<MTL::ResourceID*>(m_globalTextureBuffer->contents());
    for (int i = 0; i < m_textures.size(); i++)
    {
        ptr[i] = m_textures[i]->gpuResourceID();
    }
}

void Renderer::encode(MTL::CommandBuffer* cmd, MTL::Texture* color, const FrameData& frame, uint32_t frameSlot)
{
    ensureDepth(static_cast<uint32_t>(color->width()), static_cast<uint32_t>(color->height()));

    std::memcpy(m_frameBuffers[frameSlot]->contents(), &frame, sizeof(FrameData));

    MTL::RenderPassDescriptor* rp = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = rp->colorAttachments()->object(0);
    ca->setTexture(color);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setStoreAction(MTL::StoreActionStore);
    ca->setClearColor(MTL::ClearColor(0.10, 0.12, 0.16, 1.0));

    auto* da = rp->depthAttachment();
    da->setTexture(m_depthTexture);
    da->setLoadAction(MTL::LoadActionClear);
    da->setClearDepth(0.0);
    da->setStoreAction(MTL::StoreActionDontCare);

    MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(rp);
    enc->setLabel(NS::String::string("Invar Command Encoder", NS::UTF8StringEncoding));
    enc->setRenderPipelineState(m_pso);
    enc->setDepthStencilState(m_depthState);
    //enc->setCullMode(MTL::CullModeBack);

    // Bind buffers
    enc->setVertexBuffer(m_frameBuffers[frameSlot], 0, BufferIndexFrameData);
    enc->setVertexBuffer(m_vertexBuffer, 0, BufferIndexVertexData);
    enc->setVertexBuffer(m_instanceBuffer, 0, BufferIndexInstanceData);

    enc->setFragmentBuffer(m_materialBuffer, 0, BufferIndexMaterialData);
    enc->setFragmentBuffer(m_globalTextureBuffer, 0, BufferIndexTextureData);
    enc->setFragmentSamplerState(m_samplerState, BufferIndexSamplerState);

    // Make the textures resident for bindless
    for (const auto* texture : m_textures)
    {
        enc->useResource(texture, MTL::ResourceUsageRead, MTL::RenderStageFragment);
    }

    for (uint32_t i = 0; i < m_draws.size(); i++)
    {
        const DrawItem draw = m_draws[i];
        enc->setCullMode(draw.doubleSided ? MTL::CullModeNone : MTL::CullModeBack);
        enc->drawIndexedPrimitives(
            MTL::PrimitiveTypeTriangle,
            draw.indexCount,
            MTL::IndexTypeUInt32,
            m_indexBuffer,
            draw.firstIndex * sizeof(uint32_t),
            1,
            draw.baseVertex,
            i);
    }

    enc->endEncoding();
}

void Renderer::ensureDepth(uint32_t w, uint32_t h)
{
    if (m_depthTexture && m_depthTexture->width() == w && m_depthTexture->height() == h) return;
    if (m_depthTexture) m_depthTexture->release();

    MTL::TextureDescriptor* depthDesc = MTL::TextureDescriptor::texture2DDescriptor(
        MetalContext::kDepthFormat, w, h, false);
    depthDesc->setUsage(MTL::TextureUsageRenderTarget);
    depthDesc->setStorageMode(MTL::StorageModePrivate);
    m_depthTexture = m_ctx.device->newTexture(depthDesc);
}

int Renderer::drawCount() const
{
    return m_draws.size();
}
