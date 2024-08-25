#include "scene.hpp"

#include <iostream>

#include <glm/glm.hpp>

#include "loader_gltf.hpp"
#include "loader_json.hpp"
#include "loader_obj.hpp"

void Scene::loadFromFile(const rv::Context& context, const std::filesystem::path& filepath) {
    if (filepath.extension() == ".gltf") {
        LoaderGltf::loadFromFile(*this, context, filepath);
    } else if (filepath.extension() == ".obj") {
        LoaderObj::loadFromFile(*this, context, filepath);
    } else if (filepath.extension() == ".json") {
        LoaderJson::loadFromFile(*this, context, filepath);
    } else {
        spdlog::error("Unknown file type: {}", filepath.string());
    }
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
