#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "Scene.h"
#include <cstdio>
#include <cstring>

// glTF is right-handed; our world is left-handed (+Z forward). Mirror Z.
// For a node matrix M (column-major, 16 floats): M' = S*M*S with S = diag(1,1,-1).
static void mirrorZ(float m[16]) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            if ((r == 2) != (c == 2)) m[c * 4 + r] = -m[c * 4 + r];
}

bool loadGltf(const std::string& path, Scene& out) {
    cgltf_options opts = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opts, path.c_str(), &data) != cgltf_result_success) {
        std::printf("glTF parse failed: %s\n", path.c_str());
        return false;
    }
    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success) {
        std::printf("glTF buffer load failed: %s\n", path.c_str());
        cgltf_free(data);
        return false;
    }

    // Pass 1: every triangle primitive becomes one of our meshes.
    std::vector<std::vector<uint32_t>> primToMesh(data->meshes_count);
    for (size_t mi = 0; mi < data->meshes_count; mi++) {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (size_t pi = 0; pi < mesh.primitives_count; pi++) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) {
                primToMesh[mi].push_back(UINT32_MAX);
                continue;
            }

            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr;
            for (size_t a = 0; a < prim.attributes_count; a++) {
                const cgltf_attribute& at = prim.attributes[a];
                if      (at.type == cgltf_attribute_type_position) pos = at.data;
                else if (at.type == cgltf_attribute_type_normal)   nrm = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
            }
            if (!pos) { primToMesh[mi].push_back(UINT32_MAX); continue; }

            MeshInfo info;
            info.baseVertex = (int32_t)out.vertices.size();
            info.firstIndex = (uint32_t)out.indices.size();

            size_t base = out.vertices.size();
            out.vertices.resize(base + pos->count);
            for (size_t v = 0; v < pos->count; v++) {
                float p[3] = {0, 0, 0}, n[3] = {0, 1, 0}, t[2] = {0, 0};
                cgltf_accessor_read_float(pos, v, p, 3);
                if (nrm) cgltf_accessor_read_float(nrm, v, n, 3);
                if (uv)  cgltf_accessor_read_float(uv,  v, t, 2);
                Vertex& vert = out.vertices[base + v];
                vert.position = float3{p[0], p[1], -p[2]};   // mirror Z
                vert.normal   = float3{n[0], n[1], -n[2]};
                //vert.uv       = float2{t[0], t[1]};
            }

            size_t ic = prim.indices ? prim.indices->count : pos->count;
            for (size_t i = 0; i + 2 < ic; i += 3) {
                uint32_t i0 = prim.indices ? (uint32_t)cgltf_accessor_read_index(prim.indices, i)     : (uint32_t)i;
                uint32_t i1 = prim.indices ? (uint32_t)cgltf_accessor_read_index(prim.indices, i + 1) : (uint32_t)i + 1;
                uint32_t i2 = prim.indices ? (uint32_t)cgltf_accessor_read_index(prim.indices, i + 2) : (uint32_t)i + 2;
                out.indices.push_back(i0);
                out.indices.push_back(i2);     // swapped: the mirror flipped the winding
                out.indices.push_back(i1);
            }
            info.indexCount = (uint32_t)out.indices.size() - info.firstIndex;

            primToMesh[mi].push_back((uint32_t)out.meshes.size());
            out.meshes.push_back(info);
        }
    }

    // Pass 2: every node with a mesh becomes instances, with its baked world matrix.
    for (size_t ni = 0; ni < data->nodes_count; ni++) {
        const cgltf_node& node = data->nodes[ni];
        if (!node.mesh) continue;
        float m[16];
        cgltf_node_transform_world(&node, m);      // includes all parent transforms
        mirrorZ(m);
        size_t mi = cgltf_mesh_index(data, node.mesh);
        for (uint32_t meshId : primToMesh[mi]) {
            if (meshId == UINT32_MAX) continue;
            InstanceData inst = {};
            std::memcpy(&inst.model, m, sizeof(m));
            inst.meshId = meshId;
            out.instances.push_back(inst);
        }
    }

    cgltf_free(data);
    std::printf("Loaded %s: %zu meshes, %zu instances, %zu verts, %zu tris\n",
        path.c_str(), out.meshes.size(), out.instances.size(),
        out.vertices.size(), out.indices.size() / 3);
    return true;
}