#pragma once
#include <vector>
#include "Scene.h"
#include "shared_types.h"

struct MeshletRange
{
    uint32_t first = 0, count = 0;
};

// Describes meshlet data for the entire scene
struct MeshletData
{
    std::vector<GPUMeshlet> meshlets;
    // indices into Scene::vertices
    std::vector<uint32_t> vertices;
    // 3 local indices per triangle, each block starts 4-byte aligned
    std::vector<uint8_t> triangles;
    std::vector<MeshletRange> perMeshMeshletRange;
};

MeshletData buildMeshletData(const Scene& scene, float coneWeight = 0.5f);
bool validateMeshletData(const Scene& scene, const MeshletData& md);