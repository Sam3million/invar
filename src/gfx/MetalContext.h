#pragma once

#include <Metal/Metal.hpp>

class MetalContext
{
public:
    static constexpr MTL::PixelFormat kColorFormat = MTL::PixelFormatBGRA8Unorm_sRGB;
    static constexpr MTL::PixelFormat kDepthFormat = MTL::PixelFormatDepth32Float;

    MetalContext();
    ~MetalContext();
    MetalContext(const MetalContext&) = delete;
    MetalContext& operator=(const MetalContext&) = delete;
    MTL::Buffer* createBuffer(const void* data, size_t length, const char* name) const;

    MTL::Device* device = nullptr;
    MTL::CommandQueue* commandQueue = nullptr;
    MTL::Library* lib = nullptr;
    MTL::FunctionConstantValues* constantValues = nullptr;
};