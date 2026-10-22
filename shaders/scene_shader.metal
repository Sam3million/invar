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

[[mesh, max_total_threads_per_threadgroup(MESH_THREADS)]]
void ms_main(
    MeshOut out,
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
    const InstanceData inst = instances[w.instanceId];

    threadgroup float4x4 mvp;
    if (tid == 0)
    {
        out.set_primitive_count(m.triangleCount);
        mvp = frameData->viewProj * inst.model;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (uint v = tid; v < m.vertexCount; v += MESH_THREADS)
    {
        const Vertex vin = verts[mlVerts[m.vertexOffset + v]];
        Vertex_Out o;
        o.position = mvp * float4(vin.position, 1.0);
        o.normal = normalize((inst.model * float4(vin.normal, 0.0)).xyz);
        o.uv = vin.uv;
        o.materialId = inst.materialId;
        //o.meshletId = w.meshletId;
        out.set_vertex(v, o);
    }
    for (uint t = tid; t < m.triangleCount; t += MESH_THREADS)
     {
        const uint b = m.triangleOffset + t * 3;
        out.set_index(t * 3 + 0, mlTris[b + 0]);
        out.set_index(t * 3 + 1, mlTris[b + 1]);
        out.set_index(t * 3 + 2, mlTris[b + 2]);
    }
}

struct MeshletPayload
{
    uint work[32];
};

// Returns true if the meshlet can be skipped
bool cullMeshlet(constant CullParams& cp, InstanceData inst, GPUMeshlet m)
{
    const float3 center = (inst.model * float4(m.sphere.xyz, 1.0)).xyz;
    const float scale = max(length(inst.model[0].xyz), max(length(inst.model[1].xyz), length(inst.model[2].xyz)));
    const float radius = m.sphere.w * scale;

    if (cp.flags & CULL_FRUSTUM)
        for (int i = 0; i < 5; i++)
            if (dot(cp.planes[i].xyz, center) + cp.planes[i].w < -radius) return true;

    if (cp.flags & CULL_CONE)
    {
        const float3 axis = normalize((inst.model * float4(m.cone.xyz, 0.0)).xyz);
        const float3 toCenter = center - cp.cameraPos.xyz;
        // meshoptimizer's conservative sphere form: the whole meshlet faces away from the camera
        if (dot(toCenter, axis) >= m.cone.w * length(toCenter) + radius) return true;
    }
    return false;
}

[[object, max_total_threads_per_threadgroup(32)]]
void os_main(object_data MeshletPayload& payload [[payload]],
    mesh_grid_properties grid,
    constant CullParams& cp               [[buffer(BufferIndexCullParams)]],
    const device GPUMeshlet* meshlets     [[buffer(BufferIndexMeshlets)]],
    const device MeshletWork* work        [[buffer(BufferIndexWork)]],
    const device InstanceData* instances  [[buffer(BufferIndexInstanceData)]],
    //device atomic_uint* stats             [[buffer(BufferIndexStats)]],
    uint gid [[threadgroup_position_in_grid]],
    uint tid [[thread_index_in_threadgroup]])
{
    const uint idx = gid * 32 + tid;
    const bool valid = idx < cp.workCount;
    bool visible = false;
    if (valid)
    {
        const MeshletWork w = work[idx];
        visible = !cullMeshlet(cp, instances[w.instanceId], meshlets[w.meshletId]);
    }

    // Compact the survivors inside the 32-wide SIMD group (all of these must run in every thread)
    const uint slot  = simd_prefix_exclusive_sum(visible ? 1u : 0u);
    const uint count = simd_sum(visible ? 1u : 0u);
    //const uint total = simd_sum(valid ? 1u : 0u);
    if (visible) payload.work[slot] = idx;

    threadgroup_barrier(mem_flags::mem_object_data);   // if the compiler rejects this flag, use mem_threadgroup
    if (tid == 0)
    {
        grid.set_threadgroups_per_grid(uint3(count, 1, 1));   // always call it, even with 0
        //if (cp.flags & CULL_STATS)
        //{
        //    atomic_fetch_add_explicit(&stats[0], total, memory_order_relaxed);
        //    atomic_fetch_add_explicit(&stats[1], count, memory_order_relaxed);
        //}
    }
}

[[mesh, max_total_threads_per_threadgroup(MESH_THREADS)]]
void ms_main_culling(
    MeshOut out,
    const object_data MeshletPayload& payload [[payload]],
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
    const MeshletWork w = work[payload.work[gid]];
    const GPUMeshlet m = meshlets[w.meshletId];
    const InstanceData inst = instances[w.instanceId];

    threadgroup float4x4 mvp;
    if (tid == 0)
    {
        out.set_primitive_count(m.triangleCount);
        mvp = frameData->viewProj * inst.model;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (uint v = tid; v < m.vertexCount; v += MESH_THREADS)
    {
        const Vertex vin = verts[mlVerts[m.vertexOffset + v]];
        Vertex_Out o;
        o.position = mvp * float4(vin.position, 1.0);
        o.normal = normalize((inst.model * float4(vin.normal, 0.0)).xyz);
        o.uv = vin.uv;
        o.materialId = inst.materialId;
        //o.meshletId = w.meshletId;
        out.set_vertex(v, o);
    }
    for (uint t = tid; t < m.triangleCount; t += MESH_THREADS)
     {
        const uint b = m.triangleOffset + t * 3;
        out.set_index(t * 3 + 0, mlTris[b + 0]);
        out.set_index(t * 3 + 1, mlTris[b + 1]);
        out.set_index(t * 3 + 2, mlTris[b + 2]);
    }
}