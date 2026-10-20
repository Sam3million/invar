#pragma once

#include <shared_types.h>
#include <string>
#include <vector>

struct MeshInfo
{
    uint32_t firstIndex;
    uint32_t indexCount;
    int32_t baseVertex;
    uint32_t materialId = 0;
    uint32_t vertexCount;
};

struct Image
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

struct Scene
{
    std::vector<Vertex> vertices;           // every mesh concatenated
    std::vector<uint32_t> indices;          // local to each mesh (0..vertexCount-1)
    std::vector<MeshInfo> meshes;           // one per glTF primitive
    std::vector<InstanceData> instances;    // one per (node, primitive)
    std::vector<Image> images;
    std::vector<Material> materials;        // index 0 = default (white, no texture)
};

bool loadGltf(const std::string& path, Scene& out);