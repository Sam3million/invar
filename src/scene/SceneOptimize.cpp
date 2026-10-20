#include "SceneOptimize.h"
#include <meshoptimizer.h>

void optimizeIndices(Scene& scene)
{
    for (const MeshInfo& mesh : scene.meshes)
    {
        uint32_t* idx = &scene.indices[mesh.firstIndex];
        void* vertices = &scene.vertices[mesh.baseVertex];
        meshopt_optimizeVertexCache(idx, idx, mesh.indexCount, mesh.vertexCount);

        std::vector<Vertex> tempVertices(mesh.vertexCount);
        meshopt_optimizeVertexFetch(tempVertices.data(), idx, mesh.indexCount, vertices, mesh.vertexCount, sizeof(Vertex));

        // Copy back the reordered vertices
        std::memcpy(vertices, tempVertices.data(), mesh.vertexCount * sizeof(Vertex));
    }
}
