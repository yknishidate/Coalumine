#include "loader_obj.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include "scene.hpp"

void LoaderObj::loadFromFile(Scene& scene,
                             const rv::Context& context,
                             const std::filesystem::path& filepath) {
    spdlog::info("Load file: {}", filepath.string());

    tinyobj::attrib_t objAttrib;
    std::vector<tinyobj::shape_t> objShapes;
    std::vector<tinyobj::material_t> objMaterials;
    std::string objWarn, objErr;

    std::filesystem::path base_dir = filepath.parent_path();
    if (!tinyobj::LoadObj(&objAttrib, &objShapes, &objMaterials, &objWarn, &objErr,
                          filepath.string().c_str(), base_dir.string().c_str(), true)) {
        spdlog::error("Failed to load");
    }

    int texCount = 0;
    std::unordered_map<std::string, int> textureNames{};

    // 最後の1つはデフォルトマテリアルとして確保しておく
    // マテリアルが空の場合でもバッファを作成できるように
    // 最初をデフォルトにするとmaterial indexがずれるのでやめておく
    scene.materials.resize(objMaterials.size() + 1);
    scene.materials.back() = Material{};
    const int defaultMaterialIndex = static_cast<int>(scene.materials.size() - 1);

    for (size_t i = 0; i < objMaterials.size(); i++) {
        spdlog::info("material: {}", objMaterials[i].name);
        auto& mat = objMaterials[i];
        scene.materials[i].baseColorFactor = {mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], 1.0f};
        scene.materials[i].emissiveFactor = {mat.emission[0], mat.emission[1], mat.emission[2]};
        scene.materials[i].metallicFactor = 0.0f;

        // diffuse
        if (!mat.diffuse_texname.empty()) {
            if (textureNames.contains(mat.diffuse_texname)) {
                scene.materials[i].baseColorTextureIndex = textureNames[mat.diffuse_texname];
            } else {
                scene.materials[i].baseColorTextureIndex = texCount;
                textureNames[mat.diffuse_texname] = texCount;
                texCount++;
            }
        }
        // emission
        if (!mat.emissive_texname.empty()) {
            if (textureNames.contains(mat.emissive_texname)) {
                scene.materials[i].emissiveTextureIndex = textureNames[mat.emissive_texname];
            } else {
                scene.materials[i].emissiveTextureIndex = texCount;
                textureNames[mat.emissive_texname] = texCount;
                texCount++;
            }
        }
    }

    scene.createMaterialBuffer(context);

    std::unordered_map<rv::Vertex, uint32_t> uniqueVertices;
    scene.meshes.resize(objShapes.size());
    scene.nodes.resize(objShapes.size());
    for (int shapeIndex = 0; shapeIndex < objShapes.size(); shapeIndex++) {
        auto& mesh = scene.meshes[shapeIndex];
        auto& node = scene.nodes[shapeIndex];
        auto& shape = objShapes[shapeIndex];

        std::vector<rv::Vertex> vertices;
        std::vector<uint32_t> indices;
        for (const auto& index : shape.mesh.indices) {
            // TODO: y反転を削除
            rv::Vertex vertex;
            vertex.pos = {objAttrib.vertices[3 * index.vertex_index + 0],
                          -objAttrib.vertices[3 * index.vertex_index + 1],
                          objAttrib.vertices[3 * index.vertex_index + 2]};
            if (index.normal_index != -1) {
                vertex.normal = {objAttrib.normals[3 * index.normal_index + 0],
                                 -objAttrib.normals[3 * index.normal_index + 1],
                                 objAttrib.normals[3 * index.normal_index + 2]};
            }
            if (index.texcoord_index != -1) {
                vertex.texCoord = {objAttrib.texcoords[2 * index.texcoord_index + 0],
                                   1.0f - objAttrib.texcoords[2 * index.texcoord_index + 1]};
            }
            if (!uniqueVertices.contains(vertex)) {
                vertices.push_back(vertex);
                uniqueVertices[vertex] = static_cast<uint32_t>(uniqueVertices.size());
            }
            indices.push_back(uniqueVertices[vertex]);
        }

        mesh.vertexBuffer = context.createBuffer({
            .usage = rv::BufferUsage::AccelVertex,
            .size = sizeof(rv::Vertex) * vertices.size(),
            .debugName = std::format("vertexBuffers[{}]", scene.meshes.size()).c_str(),
        });
        mesh.indexBuffer = context.createBuffer({
            .usage = rv::BufferUsage::AccelIndex,
            .size = sizeof(uint32_t) * indices.size(),
            .debugName = std::format("indexBuffers[{}]", scene.meshes.size()).c_str(),
        });

        context.oneTimeSubmit([&](auto commandBuffer) {
            commandBuffer->copyBuffer(mesh.vertexBuffer, vertices.data());
            commandBuffer->copyBuffer(mesh.indexBuffer, indices.data());
        });

        mesh.vertexCount = static_cast<uint32_t>(vertices.size());
        mesh.triangleCount = static_cast<uint32_t>(indices.size() / 3);
        mesh.materialIndex = shape.mesh.material_ids[0];
        if (mesh.materialIndex == -1) {
            mesh.materialIndex = defaultMaterialIndex;
        }

        node.meshIndex = shapeIndex;
    }
}
