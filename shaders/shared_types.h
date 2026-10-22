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

#define MESHLET_MAX_VERTS 256
#define MESHLET_MAX_TRIS 512
#define MESH_THREADS 128

#define CULL_FRUSTUM 1u
#define CULL_CONE 2u
#define CULL_STATS 4u

enum BufferIndex
{
    BufferIndexFrameData = 0,
    BufferIndexVertexData = 1,
    BufferIndexInstanceData = 2,
    BufferIndexMaterialData = 3,
    BufferIndexTextureData = 4,
    BufferIndexSamplerState = 5,
    BufferIndexMeshlets = 6,
    BufferIndexMeshletVerts = 7,
    BufferIndexMeshletTris = 8,
    BufferIndexWork = 9,
    BufferIndexCullParams = 10,
    BufferIndexStats = 11
};

struct FrameData
{
    float4x4 viewProj;
    float3 cameraPos;
    float2 viewportSize;
    float time;
    float _pad0;
};

struct CullParams {
    float4 planes[5];     // left, right, bottom, top, near; world space, normalized: xyz = normal, w = distance
    float4 cameraPos;     // xyz = culling camera position
    uint   workCount;     // work items in the range being drawn
    uint   flags;
    uint   _pad0, _pad1;
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

struct GPUMeshlet
{
    uint vertexOffset, triangleOffset, vertexCount, triangleCount;
    float4 sphere;
    float4 cone;
};

struct MeshletWork
{
    uint instanceId;
    uint meshletId;
};