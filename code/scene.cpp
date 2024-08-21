#define TINYGLTF_IMPLEMENTATION
#include "scene.hpp"

#include <glm/glm.hpp>
#include <iostream>

#include <reactive/reactive.hpp>

void Scene::loadFromFile(const rv::Context& context, const std::filesystem::path& filepath) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, filepath.string());
    if (!warn.empty()) {
        std::cerr << "Warn: " << warn.c_str() << std::endl;
    }
    if (!err.empty()) {
        std::cerr << "Err: " << err.c_str() << std::endl;
    }
    if (!ret) {
        throw std::runtime_error("Failed to parse glTF: " + filepath.string());
    }

    spdlog::info("Nodes: {}", model.nodes.size());
    spdlog::info("Meshes: {}", model.meshes.size());
    loadNodes(context, model);
    loadMeshes(context, model);
    loadMaterials(context, model);
    loadAnimation(context, model);
    createNodeDataBuffer(context);
}

void Scene::createNodeDataBuffer(const rv::Context& context) {
    for (auto& node : nodes) {
        NodeData data;
        if (node.meshIndex != -1) {
            data.vertexBufferAddress = meshes[node.meshIndex].vertexBuffer->getAddress();
            data.indexBufferAddress = meshes[node.meshIndex].indexBuffer->getAddress();
            data.materialIndex = meshes[node.meshIndex].materialIndex;
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
        //.layout = vk::ImageLayout::eTransferDstOptimal,
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

void Scene::loadNodes(const rv::Context& context, tinygltf::Model& gltfModel) {
    for (int gltfNodeIndex = 0; gltfNodeIndex < gltfModel.nodes.size(); gltfNodeIndex++) {
        auto& gltfNode = gltfModel.nodes.at(gltfNodeIndex);
        if (gltfNode.camera != -1) {
            if (!gltfNode.translation.empty()) {
                cameraTranslation = glm::vec3{gltfNode.translation[0],
                                              -gltfNode.translation[1],  // invert y
                                              gltfNode.translation[2]};
            }
            if (!gltfNode.rotation.empty()) {
                cameraRotation = glm::quat{static_cast<float>(gltfNode.rotation[3]),
                                           static_cast<float>(gltfNode.rotation[0]),
                                           static_cast<float>(gltfNode.rotation[1]),
                                           static_cast<float>(gltfNode.rotation[2])};
            }

            tinygltf::Camera camera = gltfModel.cameras[gltfNode.camera];
            cameraYFov = static_cast<float>(camera.perspective.yfov);
            cameraExists = true;
            nodes.push_back(Node{});
            continue;
        }

        if (gltfNode.skin != -1) {
            nodes.push_back(Node{});
            continue;
        }

        if (gltfNode.mesh != -1) {
            Node node;
            node.meshIndex = gltfNode.mesh;
            if (!gltfNode.translation.empty()) {
                node.translation = glm::vec3{gltfNode.translation[0],
                                             -gltfNode.translation[1],  // invert y
                                             gltfNode.translation[2]};
            }

            if (!gltfNode.rotation.empty()) {
                node.rotation = glm::quat{static_cast<float>(gltfNode.rotation[3]),
                                          static_cast<float>(gltfNode.rotation[0]),
                                          static_cast<float>(gltfNode.rotation[1]),
                                          static_cast<float>(gltfNode.rotation[2])};
            }

            if (!gltfNode.scale.empty()) {
                node.scale = glm::vec3{gltfNode.scale[0], gltfNode.scale[1], gltfNode.scale[2]};
            }
            nodes.push_back(node);
            continue;
        }

        nodes.push_back(Node{});

        // transformMatrices.push_back(node.computeTransformMatrix());
        // normalMatrices.push_back(node.computeNormalMatrix());
    }

    // transformMatrixBuffer = context.createBuffer({
    //     .usage = BufferUsage::Storage,
    //     .size = transformMatrices.size() * sizeof(glm::mat4),
    //     .data = transformMatrices.data(),
    // });
    // normalMatrixBuffer = context.createBuffer({
    //     .usage = BufferUsage::Storage,
    //     .size = normalMatrices.size() * sizeof(glm::mat3),
    //     .data = normalMatrices.data(),
    // });
}

void Scene::loadMeshes(const rv::Context& context, tinygltf::Model& gltfModel) {
    for (int gltfMeshIndex = 0; gltfMeshIndex < gltfModel.meshes.size(); gltfMeshIndex++) {
        auto& gltfMesh = gltfModel.meshes.at(gltfMeshIndex);
        for (const auto& gltfPrimitive : gltfMesh.primitives) {
            // WARN: Since different attributes may refer to the same data, creating a
            // vertex/index buffer for each attribute will result in data duplication.

            // Vertex attributes
            auto& attributes = gltfPrimitive.attributes;

            assert(attributes.find("POSITION") != attributes.end());
            int positionIndex = attributes.find("POSITION")->second;
            tinygltf::Accessor* positionAccessor = &gltfModel.accessors[positionIndex];
            tinygltf::BufferView* positionBufferView =
                &gltfModel.bufferViews[positionAccessor->bufferView];

            tinygltf::Accessor* normalAccessor = nullptr;
            tinygltf::BufferView* normalBufferView = nullptr;
            if (attributes.find("NORMAL") != attributes.end()) {
                int normalIndex = attributes.find("NORMAL")->second;
                normalAccessor = &gltfModel.accessors[normalIndex];
                normalBufferView = &gltfModel.bufferViews[normalAccessor->bufferView];
            }

            tinygltf::Accessor* texCoordAccessor = nullptr;
            tinygltf::BufferView* texCoordBufferView = nullptr;
            if (attributes.find("TEXCOORD_0") != attributes.end()) {
                int texCoordIndex = attributes.find("TEXCOORD_0")->second;
                texCoordAccessor = &gltfModel.accessors[texCoordIndex];
                texCoordBufferView = &gltfModel.bufferViews[texCoordAccessor->bufferView];
            }

            // Create a vector to store the vertices
            std::vector<rv::Vertex> vertices(positionAccessor->count);

            // Loop over the vertices
            for (size_t i = 0; i < positionAccessor->count; i++) {
                // Compute the byteOffsets
                size_t positionByteOffset = positionAccessor->byteOffset +
                                            positionBufferView->byteOffset +
                                            i * positionBufferView->byteStride;
                vertices[i].pos = *reinterpret_cast<const glm::vec3*>(
                    &(gltfModel.buffers[positionBufferView->buffer].data[positionByteOffset]));
                vertices[i].pos.y = -vertices[i].pos.y;  // invert y

                if (normalBufferView) {
                    size_t normalByteOffset = normalAccessor->byteOffset +
                                              normalBufferView->byteOffset +
                                              i * normalBufferView->byteStride;
                    vertices[i].normal = *reinterpret_cast<const glm::vec3*>(
                        &(gltfModel.buffers[normalBufferView->buffer].data[normalByteOffset]));
                    vertices[i].normal.y = -vertices[i].normal.y;  // invert y
                }

                if (texCoordBufferView) {
                    size_t texCoordByteOffset = texCoordAccessor->byteOffset +
                                                texCoordBufferView->byteOffset +
                                                i * texCoordBufferView->byteStride;
                    vertices[i].texCoord = *reinterpret_cast<const glm::vec2*>(
                        &(gltfModel.buffers[texCoordBufferView->buffer].data[texCoordByteOffset]));
                }
            }

            // Get indices
            std::vector<uint32_t> indices;
            {
                auto& accessor = gltfModel.accessors[gltfPrimitive.indices];
                auto& bufferView = gltfModel.bufferViews[accessor.bufferView];
                auto& buffer = gltfModel.buffers[bufferView.buffer];

                size_t indicesCount = accessor.count;
                switch (accessor.componentType) {
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
                        uint32_t* buf = new uint32_t[indicesCount];
                        size_t size = indicesCount * sizeof(uint32_t);
                        memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
                               size);
                        for (size_t i = 0; i < indicesCount; i++) {
                            indices.push_back(buf[i]);
                        }
                        break;
                    }
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
                        uint16_t* buf = new uint16_t[indicesCount];
                        size_t size = indicesCount * sizeof(uint16_t);
                        memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
                               size);
                        for (size_t i = 0; i < indicesCount; i++) {
                            indices.push_back(buf[i]);
                        }
                        break;
                    }
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
                        uint8_t* buf = new uint8_t[indicesCount];
                        size_t size = indicesCount * sizeof(uint8_t);
                        memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
                               size);
                        for (size_t i = 0; i < indicesCount; i++) {
                            indices.push_back(buf[i]);
                        }
                        break;
                    }
                    default:
                        std::cerr << "Index component type " << accessor.componentType
                                  << " not supported!" << std::endl;
                        return;
                }
            }

            Mesh mesh;
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
            mesh.materialIndex = gltfPrimitive.material;
        }
    }
}

void Scene::loadMaterials(const rv::Context& context, tinygltf::Model& gltfModel) {
    for (auto& mat : gltfModel.materials) {
        Material material;

        // Base color
        if (mat.values.contains("baseColorTexture")) {
            material.baseColorTextureIndex = mat.values["baseColorTexture"].TextureIndex();
        }
        if (mat.values.contains("baseColorFactor")) {
            material.baseColorFactor =
                glm::make_vec4(mat.values["baseColorFactor"].ColorFactor().data());
        }

        // Metallic / Roughness
        if (mat.values.contains("metallicRoughnessTexture")) {
            material.metallicRoughnessTextureIndex =
                mat.values["metallicRoughnessTexture"].TextureIndex();
        }
        if (mat.values.contains("roughnessFactor")) {
            material.roughnessFactor = static_cast<float>(mat.values["roughnessFactor"].Factor());
        }
        if (mat.values.contains("metallicFactor")) {
            material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());
        }

        // Normal
        if (mat.additionalValues.contains("normalTexture")) {
            material.normalTextureIndex = mat.additionalValues["normalTexture"].TextureIndex();
        }

        // Emissive
        material.emissiveFactor[0] = static_cast<float>(mat.emissiveFactor[0]);
        material.emissiveFactor[1] = static_cast<float>(mat.emissiveFactor[1]);
        material.emissiveFactor[2] = static_cast<float>(mat.emissiveFactor[2]);
        if (mat.additionalValues.contains("emissiveTexture")) {
            material.emissiveTextureIndex = mat.additionalValues["emissiveTexture"].TextureIndex();
        }

        // Occlusion
        if (mat.additionalValues.contains("occlusionTexture")) {
            material.occlusionTextureIndex =
                mat.additionalValues["occlusionTexture"].TextureIndex();
        }

        //// Alpha
        // if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
        //     auto param = mat.additionalValues["alphaMode"];
        //     if (param.string_value == "BLEND") {
        //         material.alphaMode = AlphaMode::Blend;
        //     }
        //     if (param.string_value == "MASK") {
        //         material.alphaMode = AlphaMode::Mask;
        //     }
        // }
        // if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
        //     material.alphaCutoff =
        //         static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
        // }

        materials.push_back(material);
    }

    // Material
    if (materials.empty()) {
        materials.push_back({});  // dummy data
    }
    materialBuffer = context.createBuffer({
        .usage = rv::BufferUsage::Storage, .size = materials.size() * sizeof(Material),
        //.data = materials.data(),
    });

    context.oneTimeSubmit(
        [&](auto commandBuffer) { commandBuffer->copyBuffer(materialBuffer, materials.data()); });
}

void Scene::loadAnimation(const rv::Context& context, const tinygltf::Model& model) {
    for (const auto& animation : model.animations) {
        for (const auto& channel : animation.channels) {
            const auto& sampler = animation.samplers[channel.sampler];

            if (channel.target_path == "translation" || channel.target_path == "rotation" ||
                channel.target_path == "scale") {
                const tinygltf::Accessor& inputAccessor = model.accessors[sampler.input];
                const tinygltf::BufferView& inputBufferView =
                    model.bufferViews[inputAccessor.bufferView];
                const tinygltf::Buffer& inputBuffer = model.buffers[inputBufferView.buffer];
                const float* inputData = reinterpret_cast<const float*>(
                    &inputBuffer.data[inputBufferView.byteOffset + inputAccessor.byteOffset]);
                const size_t inputCount = inputAccessor.count;

                const tinygltf::Accessor& outputAccessor = model.accessors[sampler.output];
                const tinygltf::BufferView& outputBufferView =
                    model.bufferViews[outputAccessor.bufferView];
                const tinygltf::Buffer& outputBuffer = model.buffers[outputBufferView.buffer];
                const float* outputData = reinterpret_cast<const float*>(
                    &outputBuffer.data[outputBufferView.byteOffset + outputAccessor.byteOffset]);

                auto& keyFrames = nodes[channel.target_node].keyFrames;
                if (keyFrames.empty()) {
                    keyFrames.resize(inputCount);
                }

                // Clear default TRS
                for (size_t i = 0; i < inputCount; i++) {
                    // KeyFrame keyframe;
                    keyFrames[i].time = inputData[i];

                    if (channel.target_path == "translation") {
                        keyFrames[i].translation = glm::vec3(
                            outputData[i * 3 + 0], -outputData[i * 3 + 1], outputData[i * 3 + 2]);
                    } else if (channel.target_path == "rotation") {
                        keyFrames[i].rotation =
                            glm::quat(outputData[i * 4 + 3], outputData[i * 4 + 0],
                                      outputData[i * 4 + 1], outputData[i * 4 + 2]);
                    } else if (channel.target_path == "scale") {
                        keyFrames[i].scale = glm::vec3(outputData[i * 3 + 0], outputData[i * 3 + 1],
                                                       outputData[i * 3 + 2]);
                    }
                }
            }
        }
    }
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
                // .sbtOffset = node.materialType,
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
            // normalMatrices[node.meshIndex] = node.computeNormalMatrix(frame);
            accelInstances.push_back({
                .bottomAccel = bottomAccels[node.meshIndex],
                .transform = node.computeTransformMatrix(frame),
                // .sbtOffset = node.materialType,
            });
        }
    }
    topAccel->updateInstances(accelInstances);
    nodeDataBuffer->copy(nodeData.data());
}

int Scene::addMaterial(const rv::Context& context, const Material& material) {
    materials.push_back(material);
    materialBuffer = context.createBuffer({
        .usage = rv::BufferUsage::Storage,
        .size = materials.size() * sizeof(Material),
        .debugName = "materialBuffer",
    });
    context.oneTimeSubmit([&](auto commandBuffer) {
        commandBuffer->copyBuffer(materialBuffer, materials.data());  //
    });
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
