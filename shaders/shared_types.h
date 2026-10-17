#pragma once

#ifdef __METAL_VERSION__
#include <metal_stdlib>
using namespace metal;
#else
#include <simd/simd.h>
#include <cstdint>
using namespace simd;
typedef uint32_t uint;
#endif

enum BufferIndex
{
    BufferIndexFrameData = 0,
    BufferIndexVertexData = 1,
    BufferIndexInstanceData = 2,
};

struct FrameData
{
    float4x4 viewProj;
    float3 cameraPos;
    float2 viewportSize;
    float time;
    float _pad0;
};

struct Vertex
{
    float3 position;
    float3 normal;
};

struct InstanceData
{
    float4x4 model;
    uint meshId;
    uint materialId;
    uint _pad[2];
};