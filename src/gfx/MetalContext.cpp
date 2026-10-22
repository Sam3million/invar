#include "MetalContext.h"

#include <iostream>
#include "shared_types.h"

MetalContext::MetalContext()
{
    device =  MTL::CreateSystemDefaultDevice();
    commandQueue = device->newCommandQueue();

    NS::Error* err = nullptr;
    lib = device->newLibrary(NS::String::string(SHADER_LIB_PATH, NS::UTF8StringEncoding), &err);
    if (!lib)
    {
        std::cerr << "Failed to load shader library: " << err->localizedDescription()->utf8String() << "\n";
    }

    constantValues = MTL::FunctionConstantValues::alloc()->init();
    const auto verts = static_cast<uint32_t>(MESHLET_MAX_VERTS);
    const auto tris = static_cast<uint32_t>(MESHLET_MAX_TRIS);
    constexpr int vertConstantIndex = 0;
    constexpr int triConstantIndex = 1;
    constantValues->setConstantValue(&verts, MTL::DataTypeUInt, vertConstantIndex);
    constantValues->setConstantValue(&tris, MTL::DataTypeUInt, triConstantIndex);
}

MetalContext::~MetalContext()
{
    lib->release();
    commandQueue->release();
    device->release();
    constantValues->release();
}

/// Creates a buffer from existing data
MTL::Buffer* MetalContext::createBuffer(const void* data, size_t length, const char* name) const
{
    auto* buffer = device->newBuffer(data, length, MTL::ResourceStorageModeShared);
    buffer->setLabel(NS::String::string(name, NS::UTF8StringEncoding));
    return buffer;
}
