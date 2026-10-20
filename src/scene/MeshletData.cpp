#include "MeshletData.h"

#include "meshoptimizer.h"

MeshletData buildMeshletData(const Scene& scene, const float coneWeight)
{
    MeshletData out;
    out.perMeshMeshletRange.resize(scene.meshes.size());
    const size_t stride = sizeof(Vertex);

    for (size_t mi = 0; mi < scene.meshes.size(); mi++)
    {
        const MeshInfo& mesh = scene.meshes[mi];
        if (mesh.indexCount < 3) continue;

        const uint32_t* indices = &scene.indices[mesh.firstIndex];
        const Vertex* first = &scene.vertices[mesh.baseVertex];
        const float* pos = reinterpret_cast<const float*>(
            reinterpret_cast<const char*>(first) + offsetof(Vertex, position));

        // 1. Build
        const size_t bound = meshopt_buildMeshletsBound(mesh.indexCount, MESHLET_MAX_VERTS, MESHLET_MAX_TRIS);
        std::vector<meshopt_Meshlet> ml(bound);
        std::vector<uint32_t> mv(bound * MESHLET_MAX_VERTS);
        std::vector<uint8_t>  mt(bound * MESHLET_MAX_TRIS * 3);
        const size_t n = meshopt_buildMeshlets(ml.data(), mv.data(), mt.data(),
                                               indices, mesh.indexCount,
                                               pos, mesh.vertexCount, stride,
                                               MESHLET_MAX_VERTS, MESHLET_MAX_TRIS, coneWeight);
        if (n == 0) continue;
        ml.resize(n);

        // 2. Trim the scratch arrays to what was actually used
        const meshopt_Meshlet& last = ml.back();
        mv.resize(last.vertex_offset + last.vertex_count);
        mt.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3u));

        const uint32_t vBase = static_cast<uint32_t>(out.vertices.size());
        const uint32_t tBase = static_cast<uint32_t>(out.triangles.size());
        const uint32_t firstMeshlet = static_cast<uint32_t>(out.meshlets.size());

        // 3. Per-meshlet: optimize locally, compute bounds, record the GPU struct
        for (const meshopt_Meshlet& m : ml)
        {
            meshopt_optimizeMeshlet(&mv[m.vertex_offset], &mt[m.triangle_offset],
                                    m.triangle_count, m.vertex_count);
            const meshopt_Bounds b = meshopt_computeMeshletBounds(
                &mv[m.vertex_offset], &mt[m.triangle_offset], m.triangle_count,
                pos, mesh.vertexCount, stride);

            GPUMeshlet g = {};
            g.vertexOffset   = vBase + m.vertex_offset;       // offsets become global
            g.triangleOffset = tBase + m.triangle_offset;     // (in bytes for triangles)
            g.vertexCount    = m.vertex_count;
            g.triangleCount  = m.triangle_count;
            g.sphere = float4{b.center[0], b.center[1], b.center[2], b.radius};
            g.cone   = float4{b.cone_axis[0], b.cone_axis[1], b.cone_axis[2], b.cone_cutoff};
            out.meshlets.push_back(g);
        }

        // 4. Append the index data, rebasing the vertex list so it indexes the GLOBAL vertex buffer
        for (uint32_t v : mv) out.vertices.push_back(v + static_cast<uint32_t>(mesh.baseVertex));
        out.triangles.insert(out.triangles.end(), mt.begin(), mt.end());

        out.perMeshMeshletRange[mi] = { firstMeshlet, static_cast<uint32_t>(n) };
    }

    uint64_t tris = 0, verts = 0;
    for (const GPUMeshlet& g : out.meshlets) { tris += g.triangleCount; verts += g.vertexCount; }

    std::printf("Meshlets: %zu built (avg %.1f tris, %.1f verts; max %d/%d)\n",
                out.meshlets.size(),
                double(tris) / double(out.meshlets.size()), double(verts) / double(out.meshlets.size()),
                MESHLET_MAX_TRIS, MESHLET_MAX_VERTS);

    return out;
}

bool validateMeshletData(const Scene& scene, const MeshletData& md)
{
    using Tri = std::array<uint32_t, 3>;
    auto canon = [](Tri t) {                       // rotate so the smallest index comes first; winding is kept
        int m = (t[1] < t[0]) ? 1 : 0;
        if (t[2] < t[m]) m = 2;
        return Tri{ t[m], t[(m + 1) % 3], t[(m + 2) % 3] };
    };

    std::vector<Tri> fromMesh, fromMeshlets;
    for (size_t mi = 0; mi < scene.meshes.size(); mi++)
    {
        const MeshInfo& mesh = scene.meshes[mi];
        const auto base = static_cast<uint32_t>(mesh.baseVertex);
        for (uint32_t i = 0; i + 2 < mesh.indexCount; i += 3)
            fromMesh.push_back(canon({ scene.indices[mesh.firstIndex + i]     + base,
                                       scene.indices[mesh.firstIndex + i + 1] + base,
                                       scene.indices[mesh.firstIndex + i + 2] + base }));

        const MeshletRange r = md.perMeshMeshletRange[mi];
        for (uint32_t k = 0; k < r.count; k++)
        {
            const GPUMeshlet& m = md.meshlets[r.first + k];
            for (uint32_t t = 0; t < m.triangleCount; t++)
            {
                Tri tri;
                for (int j = 0; j < 3; j++)
                    tri[j] = md.vertices[m.vertexOffset + md.triangles[m.triangleOffset + t * 3 + j]];
                fromMeshlets.push_back(canon(tri));
            }
        }
    }
    std::sort(fromMesh.begin(), fromMesh.end());
    std::sort(fromMeshlets.begin(), fromMeshlets.end());
    const bool ok = fromMesh == fromMeshlets;
    std::printf("Meshlet validation: %s (%zu vs %zu triangles)\n", ok ? "OK" : "MISMATCH",
                fromMesh.size(), fromMeshlets.size());
    return ok;
}