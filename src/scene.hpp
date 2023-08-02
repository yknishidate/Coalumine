﻿#pragma once

#define NOMINMAX
#undef near
#undef far
#undef RGB

#include "Graphics/Buffer.hpp"
#include "Graphics/Context.hpp"

#include <tiny_gltf.h>

struct Address {
    // vk::DeviceAddress vertices;
    // vk::DeviceAddress indices;
    vk::DeviceAddress materials;
};

struct KeyFrame {
    float time;
    glm::vec3 translation = {0.0f, 0.0f, 0.0f};
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale = {1.0f, 1.0f, 1.0f};
};

class Node {
public:
    int meshIndex = -1;
    glm::vec3 translation = glm::vec3{0.0, 0.0, 0.0};
    glm::quat rotation = glm::quat{0.0, 0.0, 0.0, 0.0};
    glm::vec3 scale = glm::vec3{1.0, 1.0, 1.0};
    std::vector<KeyFrame> keyFrames;

    glm::mat4 computeTransformMatrix(int frame) const {
        if (keyFrames.empty()) {
            return glm::mat4{1.0};
        }
        // TODO: use default values
        int index = frame % keyFrames.size();
        glm::mat4 T = glm::translate(glm::mat4{1.0}, keyFrames[index].translation);
        glm::mat4 R = glm::mat4_cast(keyFrames[index].rotation);
        glm::mat4 S = glm::scale(glm::mat4{1.0}, keyFrames[index].scale);
        return T * R * S;
    }

    glm::mat4 computeNormalMatrix(int frame) const {
        if (keyFrames.empty()) {
            return glm::mat4{1.0};
        }

        int index = frame % keyFrames.size();
        return glm::mat4_cast(keyFrames[index].rotation);
    }
};

struct Material {
    // Textures
    int baseColorTextureIndex{-1};
    int metallicRoughnessTextureIndex{-1};
    int normalTextureIndex{-1};
    int occlusionTextureIndex{-1};
    int emissiveTextureIndex{-1};

    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor{1.0f};
    float roughnessFactor{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    // AlphaMode alphaMode{AlphaMode::Opaque};
    // float alphaCutoff{0.5f};
    // bool doubleSided{false};
};

class Scene {
public:
    Scene() = default;

    void loadFromFile(const Context& context) {
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err;
        std::string warn;

        // std::string filepath = (getAssetDirectory() / "glass_test_v4.gltf").string();
        // std::string filepath = (getAssetDirectory() / "animated_cube_test.gltf").string();
        // std::string filepath = (getAssetDirectory() / "animation_test.gltf").string();
        // std::string filepath = (getAssetDirectory() / "material_test.gltf").string();
        // std::string filepath = (getAssetDirectory() / "material_test_emissive.gltf").string();
        //  std::string filepath = (getAssetDirectory() / "glass_shpere.gltf").string();
        //  std::string filepath = (getAssetDirectory() / "glass_sphere_low.gltf").string();
        //   std::string filepath = (getAssetDirectory() / "metal_test.gltf").string();
        //   std::string filepath = (getAssetDirectory() / "metal_test_v2.gltf").string();
        // std::string filepath = (getAssetDirectory() / "diffuse_base.gltf").string();
        std::string filepath = (getAssetDirectory() / "clean_scene_v2.gltf").string();
        bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, filepath);
        if (!warn.empty()) {
            std::cerr << "Warn: " << warn.c_str() << std::endl;
        }
        if (!err.empty()) {
            std::cerr << "Err: " << err.c_str() << std::endl;
        }
        if (!ret) {
            throw std::runtime_error("Failed to parse glTF: " + filepath);
        }

        spdlog::info("Nodes: {}", model.nodes.size());
        spdlog::info("Meshes: {}", model.meshes.size());
        loadNodes(context, model);
        loadMeshes(context, model);
        loadMaterials(context, model);
        loadAnimation(context, model);

        for (auto& node : nodes) {
            if (node.meshIndex != -1) {
                normalMatrices.push_back(node.computeNormalMatrix(0));
            }
        }
        normalMatrixBuffer = context.createDeviceBuffer({
            .usage = BufferUsage::Storage,
            .size = sizeof(glm::mat4) * normalMatrices.size(),
            .data = normalMatrices.data(),
        });
    }

    void loadDomeLightTexture(const Context& context) {
        std::string filepath =
            (getAssetDirectory() / "drakensberg_solitary_mountain_1k.hdr").string();
        domeLightTexture = Image::loadFromFileHDR(context, filepath);

        filepath = (getAssetDirectory() / "drakensberg_solitary_mountain_1k_40.hdr").string();
        lowDomeLightTexture = Image::loadFromFileHDR(context, filepath);
    }

    void loadNodes(const Context& context, tinygltf::Model& gltfModel) {
        for (int gltfNodeIndex = 0; gltfNodeIndex < gltfModel.nodes.size(); gltfNodeIndex++) {
            auto& gltfNode = gltfModel.nodes.at(gltfNodeIndex);
            if (gltfNode.camera != -1) {
                spdlog::warn("Multiple cameras");
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
                cameraYFov = camera.perspective.yfov;
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

        // transformMatrixBuffer = context.createDeviceBuffer({
        //     .usage = BufferUsage::Storage,
        //     .size = transformMatrices.size() * sizeof(glm::mat4),
        //     .data = transformMatrices.data(),
        // });
        // normalMatrixBuffer = context.createDeviceBuffer({
        //     .usage = BufferUsage::Storage,
        //     .size = normalMatrices.size() * sizeof(glm::mat3),
        //     .data = normalMatrices.data(),
        // });
    }

    void loadMeshes(const Context& context, tinygltf::Model& gltfModel) {
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
                std::vector<Vertex> vertices(positionAccessor->count);

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
                            &(gltfModel.buffers[texCoordBufferView->buffer]
                                  .data[texCoordByteOffset]));
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

                spdlog::info("Mesh: vertex={}, index={}", vertices.size(), indices.size());
                vertexBuffers.push_back(context.createDeviceBuffer({
                    .usage = BufferUsage::Vertex,
                    .size = sizeof(Vertex) * vertices.size(),
                    .data = vertices.data(),
                }));
                indexBuffers.push_back(context.createDeviceBuffer({
                    .usage = BufferUsage::Index,
                    .size = sizeof(uint32_t) * indices.size(),
                    .data = indices.data(),
                }));
                vertexCounts.push_back(vertices.size());
                triangleCounts.push_back(indices.size() / 3);

                materialIndices.push_back(gltfPrimitive.material);
            }
        }

        if (materialIndices.empty()) {
            materialIndices.push_back(-1);  // dummy data
        }
        materialIndexBuffer = context.createDeviceBuffer({
            .usage = BufferUsage::Index,
            .size = sizeof(int) * materialIndices.size(),
            .data = materialIndices.data(),
        });
    }

    void loadMaterials(const Context& context, tinygltf::Model& gltfModel) {
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
                material.roughnessFactor =
                    static_cast<float>(mat.values["roughnessFactor"].Factor());
            }
            if (mat.values.contains("metallicFactor")) {
                material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());
            }

            // Normal
            if (mat.additionalValues.contains("normalTexture")) {
                material.normalTextureIndex = mat.additionalValues["normalTexture"].TextureIndex();
            }

            // Emissive
            material.emissiveFactor[0] = mat.emissiveFactor[0];
            material.emissiveFactor[1] = mat.emissiveFactor[1];
            material.emissiveFactor[2] = mat.emissiveFactor[2];
            if (mat.additionalValues.contains("emissiveTexture")) {
                material.emissiveTextureIndex =
                    mat.additionalValues["emissiveTexture"].TextureIndex();
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
        materialBuffer = context.createDeviceBuffer({
            .usage = BufferUsage::Storage,
            .size = materials.size() * sizeof(Material),
            .data = materials.data(),
        });

        // Address
        address.materials = materialBuffer.getAddress();
        addressBuffer = context.createDeviceBuffer({
            .usage = BufferUsage::Storage,
            .size = sizeof(Address),
            .data = &address,
        });
    }

    void loadAnimation(const Context& context, const tinygltf::Model& model) {
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
                        &outputBuffer
                             .data[outputBufferView.byteOffset + outputAccessor.byteOffset]);

                    auto& keyFrames = nodes[channel.target_node].keyFrames;
                    if (keyFrames.empty()) {
                        keyFrames.resize(inputCount);
                    }

                    // Clear default TRS
                    for (size_t i = 0; i < inputCount; i++) {
                        // KeyFrame keyframe;
                        keyFrames[i].time = inputData[i];

                        if (channel.target_path == "translation") {
                            keyFrames[i].translation =
                                glm::vec3(outputData[i * 3 + 0], outputData[i * 3 + 1],
                                          outputData[i * 3 + 2]);
                        } else if (channel.target_path == "rotation") {
                            keyFrames[i].rotation =
                                glm::quat(outputData[i * 4 + 3], outputData[i * 4 + 0],
                                          outputData[i * 4 + 1], outputData[i * 4 + 2]);
                        } else if (channel.target_path == "scale") {
                            keyFrames[i].scale =
                                glm::vec3(outputData[i * 3 + 0], outputData[i * 3 + 1],
                                          outputData[i * 3 + 2]);
                        }
                    }
                }
            }
        }
    }

    void buildAccels(const Context& context) {
        bottomAccels.resize(vertexBuffers.size());
        for (int i = 0; i < vertexBuffers.size(); i++) {
            bottomAccels[i] = context.createBottomAccel({
                .vertexBuffer = vertexBuffers[i],
                .indexBuffer = indexBuffers[i],
                .vertexStride = sizeof(Vertex),
                .vertexCount = vertexCounts[i],
                .triangleCount = triangleCounts[i],
            });
        }

        std::vector<std::pair<const BottomAccel*, glm::mat4>> buildAccels;
        for (auto& node : nodes) {
            if (node.meshIndex != -1) {
                buildAccels.push_back(
                    {&bottomAccels[node.meshIndex], node.computeTransformMatrix(0.0)});
            }
        }

        topAccel = context.createTopAccel({
            .bottomAccels = buildAccels,
        });
    }

    void updateTopAccel(vk::CommandBuffer commandBuffer, int frame) {
        std::vector<std::pair<const BottomAccel*, glm::mat4>> buildAccels;
        for (auto& node : nodes) {
            if (node.meshIndex != -1) {
                buildAccels.push_back(
                    {&bottomAccels[node.meshIndex], node.computeTransformMatrix(frame)});
            }
        }
        topAccel.update(commandBuffer, buildAccels);
    }

    std::vector<Node> nodes;
    std::vector<DeviceBuffer> vertexBuffers;
    std::vector<DeviceBuffer> indexBuffers;
    std::vector<uint32_t> vertexCounts;
    std::vector<uint32_t> triangleCounts;

    std::vector<BottomAccel> bottomAccels;
    TopAccel topAccel;
    Image domeLightTexture;
    Image lowDomeLightTexture;

    std::vector<int> materialIndices;
    DeviceBuffer materialIndexBuffer;

    std::vector<Material> materials;
    DeviceBuffer materialBuffer;

    Address address;
    DeviceBuffer addressBuffer;

    bool cameraExists = false;
    glm::vec3 cameraTranslation;
    glm::quat cameraRotation;
    float cameraYFov;

    // std::vector<glm::mat4> transformMatrices;
    // DeviceBuffer transformMatrixBuffer;
    std::vector<glm::mat4> normalMatrices;
    DeviceBuffer normalMatrixBuffer;
};
