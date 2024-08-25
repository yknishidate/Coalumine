#include "scene.hpp"

#include <iostream>

#include <glm/glm.hpp>
//#include <nlohmann/json.hpp>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include "loader_gltf.hpp"

void Scene::loadFromFile(const rv::Context& context, const std::filesystem::path& filepath) {
    if (filepath.extension() == ".gltf") {
        LoaderGltf::loadFromFile(*this, context, filepath);
    } else if (filepath.extension() == ".obj") {
        loadFromFileObj(context, filepath);
    } else if (filepath.extension() == ".json") {
        loadFromFileJson(context, filepath);
    } else {
        spdlog::error("Unknown file type: {}", filepath.string());
    }
}

void Scene::loadFromFileObj(const rv::Context& context, const std::filesystem::path& filepath) {
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
    materials.resize(objMaterials.size() + 1);
    materials.back() = Material{};
    const int defaultMaterialIndex = static_cast<int>(materials.size() - 1);

    for (size_t i = 0; i < objMaterials.size(); i++) {
        spdlog::info("material: {}", objMaterials[i].name);
        auto& mat = objMaterials[i];
        materials[i].baseColorFactor = {mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], 1.0f};
        materials[i].emissiveFactor = {mat.emission[0], mat.emission[1], mat.emission[2]};
        materials[i].metallicFactor = 0.0f;

        // diffuse
        if (!mat.diffuse_texname.empty()) {
            if (textureNames.contains(mat.diffuse_texname)) {
                materials[i].baseColorTextureIndex = textureNames[mat.diffuse_texname];
            } else {
                materials[i].baseColorTextureIndex = texCount;
                textureNames[mat.diffuse_texname] = texCount;
                texCount++;
            }
        }
        // emission
        if (!mat.emissive_texname.empty()) {
            if (textureNames.contains(mat.emissive_texname)) {
                materials[i].emissiveTextureIndex = textureNames[mat.emissive_texname];
            } else {
                materials[i].emissiveTextureIndex = texCount;
                textureNames[mat.emissive_texname] = texCount;
                texCount++;
            }
        }
    }

    createMaterialBuffer(context);

    std::unordered_map<rv::Vertex, uint32_t> uniqueVertices;
    meshes.resize(objShapes.size());
    nodes.resize(objShapes.size());
    for (int shapeIndex = 0; shapeIndex < objShapes.size(); shapeIndex++) {
        auto& mesh = meshes[shapeIndex];
        auto& node = nodes[shapeIndex];
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
            .debugName = std::format("vertexBuffers[{}]", meshes.size()).c_str(),
        });
        mesh.indexBuffer = context.createBuffer({
            .usage = rv::BufferUsage::AccelIndex,
            .size = sizeof(uint32_t) * indices.size(),
            .debugName = std::format("indexBuffers[{}]", meshes.size()).c_str(),
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

void Scene::loadFromFileJson(const rv::Context& context, const std::filesystem::path& filepath) {
    //// JSONファイルの読み込み
    // std::ifstream file(filepath);
    // if (!file.is_open()) {
    //     // spdlog::error("Failed to open file: {}", filepath.c_str());
    //     spdlog::error("Failed to open file.");
    //     return;
    // }

    // nlohmann::json jsonData;
    // file >> jsonData;

    //// "objects"セクションのパース
    // for (const auto& object : jsonData["objects"]) {
    //     Node node;
    //     node.meshIndex = object["mesh_index"];
    //     if (const auto& itr = object.find("material_index"); itr != object.end()) {
    //         node.materialIndex = *itr;
    //     }
    //     if (const auto& itr = object.find("translation"); itr != object.end()) {
    //         node.translation = {itr->at(0), itr->at(1), itr->at(2)};
    //     }
    //     if (const auto& itr = object.find("scale"); itr != object.end()) {
    //         node.scale = {itr->at(0), itr->at(1), itr->at(2)};
    //     }
    //     if (const auto& itr = object.find("rotation"); itr != object.end()) {
    //         node.rotation = {glm::vec3{itr->at(0), itr->at(1), itr->at(2)}};
    //     }
    //     nodes.push_back(node);
    // }

    //// "meshes"セクションのパース
    // for (const auto& mesh : jsonData["meshes"]) {
    //     std::string objPath = mesh["obj"];

    //    // ここでメッシュに対する具体的な処理を行います
    //    std::cout << "Loading mesh from: " << objPath << std::endl;
    //}

    //// "materials"セクションのパース
    // for (const auto& material : jsonData["materials"]) {
    //     std::vector<float> baseColor = material["base_color"];
    //     float metallic = material["metallic"];
    //     float roughness = material["roughness"];

    //    // ここでマテリアルに対する具体的な処理を行います
    //    std::cout << "Material properties - Base color: (" << baseColor[0] << ", " << baseColor[1]
    //              << ", " << baseColor[2] << ", " << baseColor[3] << ")"
    //              << ", Metallic: " << metallic << ", Roughness: " << roughness << std::endl;
    //}

    //// "camera"セクションのパース
    // std::string cameraType = jsonData["camera"]["type"];
    // float fovY = jsonData["camera"]["fov_y"];
    // float distance = jsonData["camera"]["distance"];
    // float phi = jsonData["camera"]["phi"];
    // float theta = jsonData["camera"]["theta"];

    //// ここでカメラに対する具体的な処理を行います
    // std::cout << "Camera setup - Type: " << cameraType << ", FOV Y: " << fovY
    //           << ", Distance: " << distance << ", Phi: " << phi << ", Theta: " << theta
    //           << std::endl;

    //// "environment_light"セクションのパース
    // std::vector<float> environmentColor = jsonData["environment_light"]["color"];
    // float intensity = jsonData["environment_light"]["intensity"];
    // std::string texturePath = jsonData["environment_light"]["texture"];

    //// ここで環境光に対する具体的な処理を行います
    // std::cout << "Environment light setup - Color: (" << environmentColor[0] << ", "
    //           << environmentColor[1] << ", " << environmentColor[2] << ")"
    //           << ", Intensity: " << intensity << ", Texture: " << texturePath << std::endl;
}

void Scene::createMaterialBuffer(const rv::Context& context) {
    if (materials.empty()) {
        materials.push_back({});  // dummy data
    }
    materialBuffer = context.createBuffer({
        .usage = rv::BufferUsage::Storage,
        .size = materials.size() * sizeof(Material),
    });

    context.oneTimeSubmit([&](auto commandBuffer) {  //
        commandBuffer->copyBuffer(materialBuffer, materials.data());
    });
}

void Scene::createNodeDataBuffer(const rv::Context& context) {
    nodeData.clear();
    for (auto& node : nodes) {
        NodeData data;
        if (node.meshIndex != -1) {
            data.vertexBufferAddress = meshes[node.meshIndex].vertexBuffer->getAddress();
            data.indexBufferAddress = meshes[node.meshIndex].indexBuffer->getAddress();
            data.materialIndex = node.materialIndex == -1
                                     ? meshes[node.meshIndex].materialIndex
                                     : node.materialIndex;  // マテリアルオーバーライド
            data.normalMatrix = node.computeNormalMatrix(0);
        }
        nodeData.push_back(data);
    }
    nodeDataBuffer = context.createBuffer({
        .usage = rv::BufferUsage::Storage,
        .memory = rv::MemoryUsage::DeviceHost,
        .size = sizeof(NodeData) * nodeData.size(),
        .debugName = "nodeDataBuffer",
    });
    nodeDataBuffer->copy(nodeData.data());
}

void Scene::loadDomeLightTexture(const rv::Context& context,
                                 const std::filesystem::path& filepath) {
    domeLightTexture = rv::Image::loadFromFileHDR(context, filepath.string());
}

void Scene::createDomeLightTexture(const rv::Context& context,
                                   const float* data,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t channel) {
    domeLightTexture = context.createImage({
        .usage = rv::ImageUsage::Sampled,
        .extent = {width, height, 1},
        .format = channel == 3 ? vk::Format::eR32G32B32Sfloat : vk::Format::eR32G32B32A32Sfloat,
        .debugName = "domeLightTexture",
    });
    domeLightTexture->createImageView();
    domeLightTexture->createSampler();

    rv::BufferHandle stagingBuffer = context.createBuffer({
        .usage = rv::BufferUsage::Staging,
        .memory = rv::MemoryUsage::Host,
        .size = width * height * channel * sizeof(float),
        .debugName = "stagingBuffer",
    });
    stagingBuffer->copy(data);

    context.oneTimeSubmit([&](auto commandBuffer) {
        commandBuffer->transitionLayout(domeLightTexture, vk::ImageLayout::eTransferDstOptimal);
        commandBuffer->copyBufferToImage(stagingBuffer, domeLightTexture);
        commandBuffer->transitionLayout(domeLightTexture, vk::ImageLayout::eShaderReadOnlyOptimal);
    });
}

inline void Scene::createDomeLightTexture(const rv::Context& context,
                                          uint32_t width,
                                          uint32_t height,
                                          const void* data) {
    domeLightTexture = context.createImage({
        .usage = vk::ImageUsageFlagBits::eSampled,
        .extent = {width, height, 1},
        .format = vk::Format::eR32G32B32A32Sfloat,
        .debugName = "domeLightTexture",
    });

    // Copy to image
    rv::BufferHandle stagingBuffer = context.createBuffer({
        .usage = rv::BufferUsage::Staging,
        .memory = rv::MemoryUsage::Host,
        .size = width * height * 4 * sizeof(float),
    });
    stagingBuffer->copy(data);

    context.oneTimeSubmit([&](auto commandBuffer) {
        commandBuffer->transitionLayout(domeLightTexture, vk::ImageLayout::eTransferDstOptimal);
        commandBuffer->copyBufferToImage(stagingBuffer, domeLightTexture);
        commandBuffer->transitionLayout(domeLightTexture, vk::ImageLayout::eShaderReadOnlyOptimal);
    });
}

void Scene::buildAccels(const rv::Context& context) {
    bottomAccels.resize(meshes.size());
    context.oneTimeSubmit([&](auto commandBuffer) {  //
        for (int i = 0; i < meshes.size(); i++) {
            bottomAccels[i] = context.createBottomAccel({
                .vertexBuffer = meshes[i].vertexBuffer,
                .indexBuffer = meshes[i].indexBuffer,
                .vertexStride = sizeof(rv::Vertex),
                .vertexCount = meshes[i].vertexCount,
                .triangleCount = meshes[i].triangleCount,
            });
            commandBuffer->buildBottomAccel(bottomAccels[i]);
        }
    });

    std::vector<rv::AccelInstance> accelInstances;
    for (size_t i = 0; i < nodes.size(); i++) {
        auto& node = nodes[i];
        if (node.meshIndex != -1) {
            accelInstances.push_back({
                .bottomAccel = bottomAccels[node.meshIndex],
                .transform = node.computeTransformMatrix(0),
                .customIndex = static_cast<uint32_t>(i),
            });
        }
    }
    topAccel = context.createTopAccel({.accelInstances = accelInstances});
    context.oneTimeSubmit([&](auto commandBuffer) {  //
        commandBuffer->buildTopAccel(topAccel);
    });
}

bool Scene::shouldUpdate(int frame) const {
    if (frame <= 1) {
        return true;
    }
    for (const auto& node : nodes) {
        if (node.meshIndex != -1) {
            if (node.computeTransformMatrix(frame - 1) != node.computeTransformMatrix(frame)) {
                return true;
            }
        }
    }
    return false;
}

void Scene::updateTopAccel(int frame) {
    std::vector<rv::AccelInstance> accelInstances;
    for (size_t i = 0; i < nodes.size(); i++) {
        auto& node = nodes[i];

        if (node.meshIndex != -1) {
            nodeData[i].normalMatrix = node.computeNormalMatrix(frame);
            accelInstances.push_back({
                .bottomAccel = bottomAccels[node.meshIndex],
                .transform = node.computeTransformMatrix(frame),
            });
        }
    }
    topAccel->updateInstances(accelInstances);
    nodeDataBuffer->copy(nodeData.data());
}

int Scene::addMaterial(const rv::Context& context, const Material& material) {
    materials.push_back(material);
    return static_cast<int>(materials.size() - 1);
}

int Scene::addMesh(const rv::Mesh& mesh, int materialIndex) {
    meshes.push_back({
        .vertexBuffer = mesh.vertexBuffer,
        .indexBuffer = mesh.indexBuffer,
        .vertexCount = static_cast<uint32_t>(mesh.vertices.size()),
        .triangleCount = static_cast<uint32_t>(mesh.indices.size() / 3),
        .materialIndex = materialIndex,
    });
    return static_cast<int>(meshes.size() - 1);
}

int Scene::addNode(const Node& node) {
    nodes.push_back(node);
    return static_cast<uint32_t>(nodes.size() - 1);
}
