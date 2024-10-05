#include "loader_obj.hpp"

#include <iostream>

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

        glm::vec3 aabbMin;
        aabbMin.x = objAttrib.vertices[3 * shape.mesh.indices[0].vertex_index + 0];
        aabbMin.y = objAttrib.vertices[3 * shape.mesh.indices[0].vertex_index + 1];
        aabbMin.z = objAttrib.vertices[3 * shape.mesh.indices[0].vertex_index + 2];
        glm::vec3 aabbMax = aabbMin;

        std::vector<rv::Vertex> vertices;
        std::vector<uint32_t> indices;
        for (const auto& index : shape.mesh.indices) {
            rv::Vertex vertex;
            vertex.pos.x = objAttrib.vertices[3 * index.vertex_index + 0];
            vertex.pos.y = objAttrib.vertices[3 * index.vertex_index + 1];
            vertex.pos.z = objAttrib.vertices[3 * index.vertex_index + 2];
            aabbMin = glm::min(aabbMin, vertex.pos);
            aabbMax = glm::max(aabbMax, vertex.pos);
            if (index.normal_index != -1) {
                vertex.normal.x = objAttrib.normals[3 * index.normal_index + 0];
                vertex.normal.y = objAttrib.normals[3 * index.normal_index + 1];
                vertex.normal.z = objAttrib.normals[3 * index.normal_index + 2];
            }
            if (index.texcoord_index != -1) {
                vertex.texCoord.x = objAttrib.texcoords[2 * index.texcoord_index + 0];
                vertex.texCoord.y = 1.0f - objAttrib.texcoords[2 * index.texcoord_index + 1];  // ?
            }
            if (!uniqueVertices.contains(vertex)) {
                vertices.push_back(vertex);
                uniqueVertices[vertex] = static_cast<uint32_t>(uniqueVertices.size());
            }
            indices.push_back(uniqueVertices[vertex]);
        }

        mesh.keyFrames.resize(1);
        mesh.keyFrames[0].vertexBuffer = context.createBuffer({
            .usage = rv::BufferUsage::AccelVertex,
            .size = sizeof(rv::Vertex) * vertices.size(),
            .debugName = std::format("vertexBuffers[{}]", scene.meshes.size()).c_str(),
        });
        mesh.keyFrames[0].indexBuffer = context.createBuffer({
            .usage = rv::BufferUsage::AccelIndex,
            .size = sizeof(uint32_t) * indices.size(),
            .debugName = std::format("indexBuffers[{}]", scene.meshes.size()).c_str(),
        });
        mesh.aabb = rv::AABB(aabbMin, aabbMax);

        context.oneTimeSubmit([&](auto commandBuffer) {
            commandBuffer->copyBuffer(mesh.keyFrames[0].vertexBuffer, vertices.data());
            commandBuffer->copyBuffer(mesh.keyFrames[0].indexBuffer, indices.data());
        });

        mesh.keyFrames[0].vertexCount = static_cast<uint32_t>(vertices.size());
        mesh.keyFrames[0].triangleCount = static_cast<uint32_t>(indices.size() / 3);
        mesh.materialIndex = shape.mesh.material_ids[0];
        if (mesh.materialIndex == -1) {
            mesh.materialIndex = defaultMaterialIndex;
        }

        node.meshIndex = shapeIndex;
    }
}

void LoaderObj::loadMesh(Mesh& mesh,
                         const rv::Context& context,
                         const std::filesystem::path& filepath) {
    tinyobj::attrib_t objAttrib;
    std::vector<tinyobj::shape_t> objShapes;
    std::vector<tinyobj::material_t> objMaterials;
    std::string objWarn, objErr;

    std::filesystem::path base_dir = filepath.parent_path();
    if (!tinyobj::LoadObj(&objAttrib, &objShapes, &objMaterials, &objWarn, &objErr,
                          filepath.string().c_str(), base_dir.string().c_str(), true)) {
        spdlog::error("Failed to load");
    }

    // メッシュは1つだけと想定して最初の要素だけ読み込む
    auto& shape = objShapes.front();

    glm::vec3 aabbMin;
    aabbMin.x = objAttrib.vertices[3 * shape.mesh.indices[0].vertex_index + 0];
    aabbMin.y = objAttrib.vertices[3 * shape.mesh.indices[0].vertex_index + 1];
    aabbMin.z = objAttrib.vertices[3 * shape.mesh.indices[0].vertex_index + 2];
    glm::vec3 aabbMax = aabbMin;

    std::unordered_map<rv::Vertex, uint32_t> uniqueVertices;
    std::vector<rv::Vertex> vertices;
    std::vector<uint32_t> indices;
    for (const auto& index : shape.mesh.indices) {
        rv::Vertex vertex;
        vertex.pos.x = objAttrib.vertices[3 * index.vertex_index + 0];
        vertex.pos.y = objAttrib.vertices[3 * index.vertex_index + 1];
        vertex.pos.z = objAttrib.vertices[3 * index.vertex_index + 2];
        aabbMin = glm::min(aabbMin, vertex.pos);
        aabbMax = glm::max(aabbMax, vertex.pos);
        if (index.normal_index != -1) {
            vertex.normal.x = objAttrib.normals[3 * index.normal_index + 0];
            vertex.normal.y = objAttrib.normals[3 * index.normal_index + 1];
            vertex.normal.z = objAttrib.normals[3 * index.normal_index + 2];
        }
        if (index.texcoord_index != -1) {
            vertex.texCoord.x = objAttrib.texcoords[2 * index.texcoord_index + 0];
            vertex.texCoord.y = 1.0f - objAttrib.texcoords[2 * index.texcoord_index + 1];  // ?
        }
        if (!uniqueVertices.contains(vertex)) {
            vertices.push_back(vertex);
            uniqueVertices[vertex] = static_cast<uint32_t>(uniqueVertices.size());
        }
        indices.push_back(uniqueVertices[vertex]);
    }

    mesh.keyFrames.resize(1);
    mesh.keyFrames[0].vertexBuffer = context.createBuffer({
        .usage = rv::BufferUsage::AccelVertex,
        .size = sizeof(rv::Vertex) * vertices.size(),
        .debugName = "vertexBuffer",
    });
    mesh.keyFrames[0].indexBuffer = context.createBuffer({
        .usage = rv::BufferUsage::AccelIndex,
        .size = sizeof(uint32_t) * indices.size(),
        .debugName = "indexBuffer",
    });
    mesh.aabb = rv::AABB(aabbMin, aabbMax);

    context.oneTimeSubmit([&](auto commandBuffer) {
        commandBuffer->copyBuffer(mesh.keyFrames[0].vertexBuffer, vertices.data());
        commandBuffer->copyBuffer(mesh.keyFrames[0].indexBuffer, indices.data());
    });

    mesh.keyFrames[0].vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.keyFrames[0].triangleCount = static_cast<uint32_t>(indices.size() / 3);
    mesh.materialIndex = -1;
}
