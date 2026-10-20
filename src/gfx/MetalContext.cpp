#include "MetalContext.h"

#include <iostream>

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
}

MetalContext::~MetalContext()
{
    lib->release();
    commandQueue->release();
    device->release();
}

/// Creates a buffer from existing data
MTL::Buffer* MetalContext::createBuffer(const void* data, size_t length, const char* name) const
{
    auto* buffer = device->newBuffer(data, length, MTL::ResourceStorageModeShared);
    buffer->setLabel(NS::String::string(name, NS::UTF8StringEncoding));
    return buffer;
}
