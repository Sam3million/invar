#include <metal_stdlib>
using namespace metal;

constant float2 positions[3] =
{
    float2(0.0, 0.6),
    float2(-0.6, -0.6),
    float2(0.6, -0.6)
};

constant float3 colors[3] =
{
    float3(1, 0, 0),
    float3(0, 1, 0),
    float3(0, 0, 1)
};

struct vertex_out
{
    float4 position [[position]];
    float3 color;
};

vertex vertex_out vs_main2(uint vid [[vertex_id]])
{
    vertex_out o;
    o.position = float4(positions[vid], 0.0, 1.0);
    o.color = colors[vid];
    return o;
}

fragment float4 fs_main2(vertex_out in [[stage_in]])
{
    return float4(in.color, 1.0);
}