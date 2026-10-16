#pragma once

#include <simd/quaternion.h>
#include <simd/vector_types.h>

struct Transform
{
    simd::float3 position;
    simd::quatf rotation;
    simd::float3 scale;
};
