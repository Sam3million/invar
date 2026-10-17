#pragma once

#include <shared_types.h>
#include <string>
#include <vector>

struct MeshInfo
{
    uint32_t firstIndex, indexCount; int32_t baseVertex;
};

struct Scene {
    std::vector<Vertex>       vertices;    // every mesh concatenated
    std::vector<uint32_t>     indices;     // local to each mesh (0..vertexCount-1)
    std::vector<MeshInfo>     meshes;      // one per glTF primitive
    std::vector<InstanceData> instances;   // one per (node, primitive)
};

bool loadGltf(const std::string& path, Scene& out);