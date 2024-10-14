#include "scene.hpp"

#include <iostream>

#include <glm/glm.hpp>

#include "loader/loader_gltf.hpp"
#include "loader/loader_json.hpp"
#include "loader/loader_obj.hpp"

void Scene::initialize(const rv::Context& context,
                       const std::filesystem::path& scenePath,
                       uint32_t width,
                       uint32_t height) {
    // Load scene
    rv::CPUTimer timer;
    loadFromFile(context, scenePath);
    createMaterialBuffer(context);
    createNodeDataBuffer(context);
    createDummyTextures(context);
    m_camera.setAspect(width / static_cast<float>(height));
    spdlog::info("Load scene: {} ms", timer.elapsedInMilli());

    // Build BVH
    timer.restart();
    buildAccels(context);
    spdlog::info("Build accels: {} ms", timer.elapsedInMilli());
}

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
    if (m_materials.empty()) {
        m_materials.push_back({});  // dummy data
    }
    m_materialBuffer = context.createBuffer({
        .usage = rv::BufferUsage::Storage,
        .size = m_materials.size() * sizeof(Material),
    });

    context.oneTimeSubmit([&](auto commandBuffer) {  //
        commandBuffer->copyBuffer(m_materialBuffer, m_materials.data());
    });
}

void Scene::createNodeDataBuffer(const rv::Context& context) {
    m_nodeData.clear();
    for (auto& node : m_nodes) {
        NodeData data;
        if (node.meshIndex != -1) {
            const auto& mesh = m_meshes[node.meshIndex];
            data.vertexBufferAddress = mesh.keyFrames[0].vertexBuffer->getAddress();
            data.indexBufferAddress = mesh.keyFrames[0].indexBuffer->getAddress();
            data.meshAabbMin = mesh.aabb.getMin();
            data.meshAabbMax = mesh.aabb.getMax();
            data.materialIndex = node.overrideMaterialIndex == -1
                                     ? mesh.materialIndex
                                     : node.overrideMaterialIndex;  // マテリアルオーバーライド
            data.normalMatrix = node.computeNormalMatrix(0);
        }
        m_nodeData.push_back(data);
    }
    m_nodeDataBuffer = context.createBuffer({
        .usage = rv::BufferUsage::Storage,
        .memory = rv::MemoryUsage::DeviceHost,
        .size = sizeof(NodeData) * m_nodeData.size(),
        .debugName = "nodeDataBuffer",
    });
    m_nodeDataBuffer->copy(m_nodeData.data());
}

void Scene::loadEnvLightTexture(const rv::Context& context, const std::filesystem::path& filepath) {
    m_envLightTexture = rv::Image::loadFromFileHDR(context, filepath.string());
}

void Scene::createDummyTextures(const rv::Context& context) {
    if (m_textures2d.empty()) {
        auto newTexture = context.createImage({
            .usage = rv::ImageUsage::Sampled,
            .extent = {1, 1, 1},
            .format = vk::Format::eR32G32B32A32Sfloat,
            .viewInfo = rv::ImageViewCreateInfo{},
            .samplerInfo = rv::SamplerCreateInfo{},
            .debugName = "dummy",
        });
        context.oneTimeSubmit([&](const rv::CommandBufferHandle& commandBuffer) {
            commandBuffer->transitionLayout(newTexture, vk::ImageLayout::eGeneral);
        });
        m_textures2d.push_back(newTexture);
    }
    if (m_textures3d.empty()) {
        auto newTexture = context.createImage({
            .usage = rv::ImageUsage::Sampled,
            .extent = {1, 1, 1},
            .imageType = vk::ImageType::e3D,
            .format = vk::Format::eR32G32B32A32Sfloat,
            .viewInfo = rv::ImageViewCreateInfo{},
            .samplerInfo = rv::SamplerCreateInfo{},
            .debugName = "dummy",
        });
        context.oneTimeSubmit([&](const rv::CommandBufferHandle& commandBuffer) {
            commandBuffer->transitionLayout(newTexture, vk::ImageLayout::eGeneral);
        });
        m_textures3d.push_back(newTexture);
    }
}

void Scene::createEnvLightTexture(const rv::Context& context,
                                  const float* data,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t channel) {
    m_envLightTexture = context.createImage({
        .usage = rv::ImageUsage::Sampled,
        .extent = {width, height, 1},
        .format = channel == 3 ? vk::Format::eR32G32B32Sfloat : vk::Format::eR32G32B32A32Sfloat,
        .viewInfo = rv::ImageViewCreateInfo{},
        .samplerInfo = rv::SamplerCreateInfo{},
        .debugName = "envLightTexture",
    });

    rv::BufferHandle stagingBuffer = context.createBuffer({
        .usage = rv::BufferUsage::Staging,
        .memory = rv::MemoryUsage::Host,
        .size = width * height * channel * sizeof(float),
        .debugName = "stagingBuffer",
    });
    stagingBuffer->copy(data);

    context.oneTimeSubmit([&](auto commandBuffer) {
        commandBuffer->transitionLayout(m_envLightTexture, vk::ImageLayout::eTransferDstOptimal);
        commandBuffer->copyBufferToImage(stagingBuffer, m_envLightTexture);
        commandBuffer->transitionLayout(m_envLightTexture, vk::ImageLayout::eShaderReadOnlyOptimal);
    });
}

void Scene::buildAccels(const rv::Context& context) {
    m_bottomAccels.resize(m_meshes.size());
    context.oneTimeSubmit([&](auto commandBuffer) {  //
        for (int i = 0; i < m_meshes.size(); i++) {
            // 本当に初期化時にバッファが必要？
            m_bottomAccels[i] = context.createBottomAccel({
                .vertexBuffer = m_meshes[i].keyFrames[0].vertexBuffer,
                .indexBuffer = m_meshes[i].keyFrames[0].indexBuffer,
                .vertexStride = sizeof(rv::Vertex),
                .maxVertexCount = m_meshes[i].getMaxVertexCount(),
                .maxTriangleCount = m_meshes[i].getMaxTriangleCount(),
                .triangleCount = m_meshes[i].keyFrames[0].triangleCount,
            });
            commandBuffer->buildBottomAccel(m_bottomAccels[i]);
        }
    });

    updateAccelInstances(0);
    m_topAccel = context.createTopAccel({.accelInstances = m_accelInstances});
    context.oneTimeSubmit([&](auto commandBuffer) {  //
        commandBuffer->buildTopAccel(m_topAccel);
    });
}

bool Scene::shouldUpdate(int frame) const {
    if (frame <= 1) {
        return true;
    }
    for (const auto& node : m_nodes) {
        if (node.meshIndex != -1) {
            if (node.computeTransformMatrix(frame - 1) != node.computeTransformMatrix(frame)) {
                return true;
            }
            if (m_meshes[node.meshIndex].hasAnimation()) {
                return true;
            }
        }
    }
    return false;
}

void Scene::updateAccelInstances(int frame) {
    m_accelInstances.clear();
    for (size_t i = 0; i < m_nodes.size(); i++) {
        auto& node = m_nodes[i];

        if (node.meshIndex != -1) {
            // BLASをUpdate/Rebuildする場合はバッファも更新して合わせる必要がある
            if (m_meshes[node.meshIndex].hasAnimation()) {
                const auto& keyFrame = m_meshes[node.meshIndex].getKeyFrameMesh(frame);
                m_nodeData[i].vertexBufferAddress = keyFrame.vertexBuffer->getAddress();
                m_nodeData[i].indexBufferAddress = keyFrame.indexBuffer->getAddress();
            }

            m_nodeData[i].normalMatrix = node.computeNormalMatrix(frame);
            m_accelInstances.push_back({
                .bottomAccel = m_bottomAccels[node.meshIndex],
                .transform = node.computeTransformMatrix(frame),
                .customIndex = static_cast<uint32_t>(i),
            });
        }
    }
}

void Scene::updateBottomAccel(int frame) {
    for (int i = 0; i < m_meshes.size(); i++) {
        if (m_meshes[i].hasAnimation()) {
            const auto& keyFrame = m_meshes[i].getKeyFrameMesh(frame);
            m_bottomAccels[i]->update(keyFrame.vertexBuffer, keyFrame.indexBuffer,
                                      keyFrame.triangleCount);
        }
    }
}

void Scene::updateTopAccel(int frame) {
    updateAccelInstances(frame);
    m_topAccel->updateInstances(m_accelInstances);
    m_nodeDataBuffer->copy(m_nodeData.data());
}

void Scene::updateMaterialBuffer(const rv::CommandBufferHandle& commandBuffer) {
    commandBuffer->copyBuffer(m_materialBuffer, m_materials.data());
}

uint32_t Scene::getMaxFrame() const {
    uint32_t frame = 0;
    for (const auto& node : m_nodes) {
        frame = std::max(frame, static_cast<uint32_t>(node.keyFrames.size()));
    }
    for (const auto& mesh : m_meshes) {
        frame = std::max(frame, static_cast<uint32_t>(mesh.keyFrames.size()));
    }
    return frame;
}
