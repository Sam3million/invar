#pragma once

#include "shared_types.h"
#include "Transform.h"

struct Camera
{
    Transform transform;
    float fovY = 1.0472f;
    float nearZ = 0.1f;
    int pixelWidth;
    int pixelHeight;

    [[nodiscard]] float3 forward() const { return simd_act(transform.rotation, float3{0, 0, 1}); }
    [[nodiscard]] float3 right() const { return simd_act(transform.rotation, float3{1, 0, 0}); }
    [[nodiscard]] float3 up() const { return simd_act(transform.rotation, float3{0, 1, 0}); }

    [[nodiscard]] float4x4 view() const
    {
        const quatf inv = simd_conjugate(transform.rotation);
        const float3 t = simd_act(inv, -transform.position);
        float4x4 V = simd_matrix4x4(inv);
        V.columns[3] = float4{t.x, t.y, t.z, 1};
        return V;
    }

    [[nodiscard]] float4x4 projection() const
    {
        const float f = 1.0f / tanf(fovY * 0.5f);
        return float4x4(
            float4{(pixelHeight * f) / pixelWidth, 0, 0, 0 },
            float4{ 0, f, 0, 0 },
            float4{ 0, 0, 0, 1 },
            float4{ 0, 0, nearZ, 0 }
        );
    }
};
