#include <metal_stdlib>
using namespace metal;
#include "shared_types.h"

struct TextureTable
{
    array<texture2d<float>, MAX_TEXTURES> globalTextureBuffer;
};

struct Vertex_Out
{
    float4 position [[position]];
    float3 normal;
    float2 uv;
    uint materialId [[flat]];
    //uint meshletId [[flat]];
};

// Generates a pseudo-random float3 color [0.0, 1.0] from a float3 input
float3 hashColor(uint id)
{
    // Step 1: 1D to 3D PCG integer hash
    uint3 v;
    v.x = id * 1664525u + 1013904223u;
    v.y = v.x * 1664525u + 1013904223u;
    v.z = v.y * 1664525u + 1013904223u;

    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;

    // Step 2: Normalize the 32-bit unsigned integers to [0.0, 1.0]
    return float3(v) * (1.0 / 4294967295.0);
}

vertex Vertex_Out vs_main(
    uint vid [[vertex_id]],
    uint iid [[instance_id]],
    uint baseInstance [[base_instance]],
    constant FrameData* frameDataBuffer [[buffer(BufferIndexFrameData)]],
    const device Vertex* vertexBuffer [[buffer(BufferIndexVertexData)]],
    const device InstanceData* instanceBuffer [[buffer(BufferIndexInstanceData)]])
{
    Vertex_Out o;

    auto instanceData = instanceBuffer[iid];
    float4x4 modelMatrix = instanceData.model;
    Vertex v = vertexBuffer[vid];

    o.position = frameDataBuffer->viewProj * (modelMatrix * float4(v.position, 1.0));
    o.normal = normalize((modelMatrix * float4(v.normal, 0.0)).xyz);
    o.uv = v.uv;
    o.materialId = instanceData.materialId;
    //o.meshletId = 0;

    return o;
}

fragment float4 fs_main(
    Vertex_Out in [[stage_in]],
    const device Material* materialBuffer [[buffer(BufferIndexMaterialData)]],
    const device TextureTable& textureTable [[buffer(BufferIndexTextureData)]],
    sampler smp [[sampler(BufferIndexSamplerState)]])
{
    //return float4(hashColor(in.meshletId), 1);
    Material mat = materialBuffer[in.materialId];
    float4 base = mat.baseColorFactor;
    if (mat.baseColorTex >= 0)
    {
        base *= textureTable.globalTextureBuffer[mat.baseColorTex].sample(smp, in.uv);
    }
    if ((mat.flags & MATERIAL_FLAG_ALPHA_MASK) && base.a < mat.alphaCutoff)
    {
        discard_fragment();
    }

    float3 n = normalize(in.normal);
    float ndl = saturate(dot(n, normalize(float3(0.4, 0.8, -0.3))));
    return float4(base.rgb * (0.25 + 0.75 * ndl), 1.0);
}

using MeshOut = metal::mesh<Vertex_Out, void, MESHLET_MAX_VERTS, MESHLET_MAX_TRIS, metal::topology::triangle>;

[[mesh, max_total_threads_per_threadgroup(128)]]
void ms_main(MeshOut out,
    constant FrameData* frameData               [[buffer(BufferIndexFrameData)]],
    const device Vertex* verts                  [[buffer(BufferIndexVertexData)]],
    const device InstanceData* instances        [[buffer(BufferIndexInstanceData)]],
    const device GPUMeshlet* meshlets           [[buffer(BufferIndexMeshlets)]],
    const device uint* mlVerts                  [[buffer(BufferIndexMeshletVerts)]],
    const device uchar* mlTris                  [[buffer(BufferIndexMeshletTris)]],
    const device MeshletWork* work              [[buffer(BufferIndexWork)]],
    uint gid [[threadgroup_position_in_grid]],
    uint tid [[thread_index_in_threadgroup]])
{
    const MeshletWork w = work[gid];
    const GPUMeshlet m = meshlets[w.meshletId];

    if (tid == 0) out.set_primitive_count(m.triangleCount);

    if (tid < m.vertexCount)
    {
        const InstanceData inst = instances[w.instanceId];
        const Vertex vin = verts[mlVerts[m.vertexOffset + tid]];

        Vertex_Out o;
        o.position = frameData->viewProj * (inst.model * float4(vin.position, 1.0));
        o.normal = normalize((inst.model * float4(vin.normal, 0.0)).xyz);
        o.uv = vin.uv;
        o.materialId = inst.materialId;
        //o.meshletId = w.meshletId;
        out.set_vertex(tid, o);
    }
    if (tid < m.triangleCount)
    {
        const uint b = m.triangleOffset + tid * 3;
        out.set_index(tid * 3 + 0, mlTris[b + 0]);
        out.set_index(tid * 3 + 1, mlTris[b + 1]);
        out.set_index(tid * 3 + 2, mlTris[b + 2]);
    }
}