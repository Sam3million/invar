#include "Renderer.h"

#include <iostream>
#include "ClassicPath.h"
#include "MeshletPath.h"
#include "shared_types.h"

Renderer::Renderer(MetalContext& ctx, const RenderMode renderMode, const int framesInFlight) :
    m_ctx(ctx),
    m_framesInFlight(framesInFlight),
    m_frameBuffers(framesInFlight),
    m_renderMode(renderMode),
    m_renderPaths({std::make_unique<ClassicPath>(ctx), std::make_unique<MeshletPath>(ctx)})
{
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
}

Renderer::~Renderer()
{
    //m_pso->release();
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

void Renderer::setScene(const Scene& scene)
{
    if (m_vertexBuffer) m_vertexBuffer->release();
    if (m_indexBuffer) m_indexBuffer->release();
    if (m_instanceBuffer) m_instanceBuffer->release();
    if (m_materialBuffer) m_materialBuffer->release();

    m_vertexBuffer = m_ctx.createBuffer(scene.vertices.data(), scene.vertices.size() * sizeof(Vertex), "Vertex Buffer");
    m_indexBuffer = m_ctx.createBuffer(scene.indices.data(), scene.indices.size() * sizeof(uint32_t), "Index Buffer");
    m_instanceBuffer = m_ctx.createBuffer(scene.instances.data(), scene.instances.size() * sizeof(InstanceData), "Instance Buffer");
    m_materialBuffer = m_ctx.createBuffer(scene.materials.data(), scene.materials.size() * sizeof(Material), "Material Buffer");

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

    m_gpu =
    {
        .samplerState = m_samplerState,
        .vertexBuffer = m_vertexBuffer,
        .indexBuffer = m_indexBuffer,
        .instanceBuffer = m_instanceBuffer,
        .materialBuffer = m_materialBuffer,
        .globalTextureBuffer = m_globalTextureBuffer,
        .textures = m_textures
    };

    renderPath().setScene(scene, m_gpu);
}

void Renderer::encode(MTL::CommandBuffer* cmd, MTL::Texture* color, const FrameData& frame, uint32_t frameSlot)
{
    ensureDepth(static_cast<uint32_t>(color->width()), static_cast<uint32_t>(color->height()));

    std::memcpy(m_frameBuffers[frameSlot]->contents(), &frame, sizeof(FrameData));
    const FrameBindings frameBindings { .frameData = m_frameBuffers[frameSlot], .frameSlot = frameSlot };

    // The render path might have prepasses (eg compute culling)
    renderPath().prepare(cmd, m_gpu, frameBindings);

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
    enc->setDepthStencilState(m_depthState);

    enc->setFragmentBuffer(m_materialBuffer, 0, BufferIndexMaterialData);
    enc->setFragmentBuffer(m_globalTextureBuffer, 0, BufferIndexTextureData);
    enc->setFragmentSamplerState(m_samplerState, BufferIndexSamplerState);
    // Make the textures resident for bindless
    for (const auto* texture : m_textures)
    {
        enc->useResource(texture, MTL::ResourceUsageRead, MTL::RenderStageFragment);
    }

    // Encode main pass
    renderPath().encode(enc, m_gpu, frameBindings);

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