#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "Scene.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include "stb_image.h"

static constexpr bool kLoaderDiagnostics = false;

// glTF is right-handed; our world is left-handed (+Z forward). Mirror Z.
// For a node matrix M (column-major, 16 floats): M' = S*M*S with S = diag(1,1,-1).
static void mirrorZ(float m[16])
{
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            if ((r == 2) != (c == 2)) m[c * 4 + r] = -m[c * 4 + r];
}
static float det3(const float m[16])
{
    const float *c0 = m, *c1 = m + 4, *c2 = m + 8;
    return c0[0] * (c1[1] * c2[2] - c1[2] * c2[1])
         - c1[0] * (c0[1] * c2[2] - c0[2] * c2[1])
         + c2[0] * (c0[1] * c1[2] - c0[2] * c1[1]);
}

bool loadGltf(const std::string& path, Scene& out)
{
    cgltf_options opts = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opts, path.c_str(), &data) != cgltf_result_success)
    {
        std::printf("glTF parse failed: %s\n", path.c_str());
        return false;
    }
    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success)
    {
        std::printf("glTF buffer load failed: %s\n", path.c_str());
        cgltf_free(data);
        return false;
    }

    if (kLoaderDiagnostics)
        for (size_t i = 0; i < data->extensions_required_count; i++)
            std::printf("glTF requires extension: %s\n", data->extensions_required[i]);

    // images and materials
    const std::filesystem::path baseDir = std::filesystem::path(path).parent_path();
    std::unordered_map<const cgltf_image*, int> imageIndex;

    auto loadImage = [&](const cgltf_image* img) -> int
    {
        if (!img) return -1;
        if (auto it = imageIndex.find(img); it != imageIndex.end()) return it->second;

        int w = 0, h = 0, comp = 0;
        stbi_uc* px = nullptr;
        if (img->buffer_view)
        {
            // embedded in a .glb
            const uint8_t* bytes = static_cast<const uint8_t*>(img->buffer_view->buffer->data) + img->buffer_view->offset;
            px = stbi_load_from_memory(bytes, static_cast<int>(img->buffer_view->size), &w, &h, &comp, 4);
        }
        else if (img->uri)
        {
            std::string uri = img->uri;
            cgltf_decode_uri(uri.data());
            px = stbi_load((baseDir / uri).string().c_str(), &w, &h, &comp, 4);   // force RGBA
        }
        if (!px)
        {
            std::printf("Failed to load image: %s\n", img->uri ? img->uri : "(embedded)");
            return -1;
        }

        Image im;
        im.width = w;
        im.height = h;
        im.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
        stbi_image_free(px);
        const int idx = static_cast<int>(out.images.size());
        out.images.push_back(std::move(im));
        imageIndex[img] = idx;
        return idx;
    };

    out.materials.push_back({ {1, 1, 1, 1}, -1, 0, 0.5f, 0 });   // index 0 = default
    for (size_t i = 0; i < data->materials_count; i++)
    {
        const cgltf_material& gm = data->materials[i];
        Material m = { {1, 1, 1, 1}, -1, 0, 0.5f, 0 };

        if (gm.has_pbr_metallic_roughness)
        {
            const auto& pbr = gm.pbr_metallic_roughness;
            m.baseColorFactor = float4{pbr.base_color_factor[0], pbr.base_color_factor[1],
                                       pbr.base_color_factor[2], pbr.base_color_factor[3]};
            if (pbr.base_color_texture.texture)
                m.baseColorTex = loadImage(pbr.base_color_texture.texture->image);

            if (kLoaderDiagnostics && pbr.base_color_texture.texture &&
                (pbr.base_color_texture.has_transform || pbr.base_color_texture.texcoord != 0))
                std::printf("material %zu ('%s') uses a texture transform or texcoord set %d (not supported)\n",
                            i, gm.name ? gm.name : "", static_cast<int>(pbr.base_color_texture.texcoord));
        }
        else if (gm.has_pbr_specular_glossiness)
        {
            const auto& sg = gm.pbr_specular_glossiness;
            m.baseColorFactor = float4{sg.diffuse_factor[0], sg.diffuse_factor[1],
                                       sg.diffuse_factor[2], sg.diffuse_factor[3]};
            if (sg.diffuse_texture.texture)
                m.baseColorTex = loadImage(sg.diffuse_texture.texture->image);
        }

        if (gm.alpha_mode == cgltf_alpha_mode_mask)
        {
            m.flags |= MATERIAL_FLAG_ALPHA_MASK;
            m.alphaCutoff = gm.alpha_cutoff;
        }
        if (gm.double_sided) m.flags |= MATERIAL_FLAG_DOUBLE_SIDED;
        out.materials.push_back(m);
    }

    // make meshes
    constexpr uint32_t kNoMesh = UINT32_MAX;
    size_t skippedPrimitives = 0;
    std::vector<std::vector<uint32_t>> primToMesh(data->meshes_count);

    for (size_t mi = 0; mi < data->meshes_count; mi++)
    {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (size_t pi = 0; pi < mesh.primitives_count; pi++)
        {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles)
            {
                primToMesh[mi].push_back(kNoMesh);
                skippedPrimitives++;
                continue;
            }

            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr;
            for (size_t a = 0; a < prim.attributes_count; a++)
            {
                const cgltf_attribute& at = prim.attributes[a];
                if      (at.type == cgltf_attribute_type_position) pos = at.data;
                else if (at.type == cgltf_attribute_type_normal)   nrm = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
            }
            if (!pos)
            {
                primToMesh[mi].push_back(kNoMesh);
                skippedPrimitives++;
                continue;
            }

            MeshInfo info;
            info.baseVertex = static_cast<int32_t>(out.vertices.size());
            info.firstIndex = static_cast<uint32_t>(out.indices.size());
            info.materialId = prim.material
                ? static_cast<uint32_t>(cgltf_material_index(data, prim.material) + 1)
                : 0;

            const size_t base = out.vertices.size();
            out.vertices.resize(base + pos->count);
            for (size_t v = 0; v < pos->count; v++)
            {
                float p[3] = {0, 0, 0}, n[3] = {0, 1, 0}, t[2] = {0, 0};
                cgltf_accessor_read_float(pos, v, p, 3);
                if (nrm) cgltf_accessor_read_float(nrm, v, n, 3);
                if (uv)  cgltf_accessor_read_float(uv,  v, t, 2);
                Vertex& vert = out.vertices[base + v];
                vert.position = float3{p[0], p[1], -p[2]};
                vert.normal   = float3{n[0], n[1], -n[2]};
                vert.uv       = float2{t[0], t[1]};
            }

            const size_t ic = prim.indices ? prim.indices->count : pos->count;
            for (size_t i = 0; i + 2 < ic; i += 3)
            {
                const uint32_t i0 = prim.indices ? static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i))     : static_cast<uint32_t>(i);
                const uint32_t i1 = prim.indices ? static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i + 1)) : static_cast<uint32_t>(i + 1);
                const uint32_t i2 = prim.indices ? static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i + 2)) : static_cast<uint32_t>(i + 2);
                out.indices.push_back(i0);
                out.indices.push_back(i2);
                out.indices.push_back(i1);
            }
            info.indexCount = static_cast<uint32_t>(out.indices.size()) - info.firstIndex;

            primToMesh[mi].push_back(static_cast<uint32_t>(out.meshes.size()));
            out.meshes.push_back(info);
        }
    }

    // make instances from nodes
    std::unordered_map<uint32_t, uint32_t> flippedMesh;
    auto getFlipped = [&](uint32_t meshId) -> uint32_t
    {
        if (auto it = flippedMesh.find(meshId); it != flippedMesh.end()) return it->second;

        const MeshInfo orig = out.meshes[meshId];
        const std::vector<uint32_t> src(out.indices.begin() + orig.firstIndex,
                                        out.indices.begin() + orig.firstIndex + orig.indexCount);

        MeshInfo flipped = orig;
        flipped.firstIndex = static_cast<uint32_t>(out.indices.size());
        for (size_t i = 0; i + 2 < src.size(); i += 3)
        {
            out.indices.push_back(src[i]);
            out.indices.push_back(src[i + 2]);
            out.indices.push_back(src[i + 1]);
        }
        const uint32_t id = static_cast<uint32_t>(out.meshes.size());
        out.meshes.push_back(flipped);
        flippedMesh[meshId] = id;
        return id;
    };

    size_t mirroredInstances = 0;
    for (size_t ni = 0; ni < data->nodes_count; ni++)
    {
        const cgltf_node& node = data->nodes[ni];
        if (!node.mesh) continue;

        float m[16];
        cgltf_node_transform_world(&node, m);
        mirrorZ(m);
        const bool mirrored = det3(m) < 0.0f;
        if (mirrored) mirroredInstances++;

        const size_t mi = cgltf_mesh_index(data, node.mesh);
        for (uint32_t meshId : primToMesh[mi])
        {
            if (meshId == kNoMesh) continue;
            const uint32_t useMesh = mirrored ? getFlipped(meshId) : meshId;

            InstanceData inst = {};
            std::memcpy(&inst.model, m, sizeof(m));
            inst.meshId = useMesh;
            inst.materialId = out.meshes[useMesh].materialId;
            out.instances.push_back(inst);
        }
    }

    cgltf_free(data);
    std::printf("Loaded %s: %zu meshes, %zu instances, %zu verts, %zu tris, %zu materials, %zu images\n",
        path.c_str(), out.meshes.size(), out.instances.size(),
        out.vertices.size(), out.indices.size() / 3, out.materials.size(), out.images.size());
    if (kLoaderDiagnostics)
        std::printf("  mirrored (negative-determinant) instances: %zu, reversed mesh copies: %zu, skipped primitives: %zu\n",
            mirroredInstances, flippedMesh.size(), skippedPrimitives);
    return true;
}