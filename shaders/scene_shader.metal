#include <metal_stdlib>
using namespace metal;
#include "shared_types.h"

struct Vertex_Out
{
    float4 position [[position]];
    float3 normal;
};

vertex Vertex_Out vs_main(
    uint vid [[vertex_id]],
    uint iid [[instance_id]],
    uint baseInstance [[base_instance]],
    constant FrameData* frameDataBuffer [[buffer(BufferIndexFrameData)]],
    const device Vertex* vertexBuffer [[buffer(BufferIndexVertices)]],
    const device InstanceData* instanceBuffer [[buffer(BufferIndexInstanceData)]])
{
    Vertex_Out o;

    auto instanceData = instanceBuffer[iid + baseInstance];
    float4x4 modelMatrix = instanceData.model;
    Vertex v = vertexBuffer[vid];

    o.position = frameDataBuffer->viewProj * (modelMatrix * float4(v.position, 1.0));
    o.normal = normalize((modelMatrix * float4(v.normal, 0.0)).xyz);

    return o;
}

fragment float4 fs_main(Vertex_Out in [[stage_in]])
{
    float3 n = normalize(in.normal);
    float ndl = saturate(dot(n, normalize(float3(0.4, 0.8, -0.3))));
    return float4((n * 0.5 + 0.5) * (0.3 + 0.7 * ndl), 1.0);   // normal-colored + lit
}