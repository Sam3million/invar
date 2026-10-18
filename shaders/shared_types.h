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

#define MATERIAL_FLAG_ALPHA_MASK 1u
#define MATERIAL_FLAG_DOUBLE_SIDED 2u
#define MAX_TEXTURES 1024

enum BufferIndex
{
    BufferIndexFrameData = 0,
    BufferIndexVertexData = 1,
    BufferIndexInstanceData = 2,
    BufferIndexMaterialData = 3,
    BufferIndexTextureData = 4,
    BufferIndexSamplerState = 5
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
    float2 uv;
};

struct InstanceData
{
    float4x4 model;
    uint meshId;
    uint materialId;
    uint _pad[2];
};

struct Material
{
    float4 baseColorFactor;
    // index into the texture table:
    int baseColorTex;
    uint flags;
    float alphaCutoff;
    uint _pad;
};

