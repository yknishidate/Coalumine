#include "loader_gltf.hpp"
#include "scene.hpp"

#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

namespace {
void loadNodes(Scene& scene, const rv::Context& context, tinygltf::Model& gltfModel) {
    for (int gltfNodeIndex = 0; gltfNodeIndex < gltfModel.nodes.size(); gltfNodeIndex++) {
        auto& gltfNode = gltfModel.nodes.at(gltfNodeIndex);
        if (gltfNode.camera != -1) {
            if (!gltfNode.translation.empty()) {
                scene.camera.setPosition({static_cast<float>(gltfNode.translation[0]),
                                          static_cast<float>(gltfNode.translation[1]),
                                          static_cast<float>(gltfNode.translation[2])});
            }
            if (!gltfNode.rotation.empty()) {
                glm::quat rotation;
                rotation.x = static_cast<float>(gltfNode.rotation[0]);
                rotation.y = static_cast<float>(gltfNode.rotation[1]);
                rotation.z = static_cast<float>(gltfNode.rotation[2]);
                rotation.w = static_cast<float>(gltfNode.rotation[3]);
                scene.camera.setEulerRotation(glm::eulerAngles(rotation));
            }

            tinygltf::Camera camera = gltfModel.cameras[gltfNode.camera];
            scene.camera.setFovY(static_cast<float>(camera.perspective.yfov));
            scene.nodes.push_back(Node{});
            continue;
        }

        if (gltfNode.skin != -1) {
            scene.nodes.push_back(Node{});
            continue;
        }

        if (gltfNode.mesh != -1) {
            Node node;
            node.meshIndex = gltfNode.mesh;
            if (!gltfNode.translation.empty()) {
                node.translation.x = static_cast<float>(gltfNode.translation[0]);
                node.translation.y = static_cast<float>(gltfNode.translation[1]);
                node.translation.z = static_cast<float>(gltfNode.translation[2]);
            }

            if (!gltfNode.rotation.empty()) {
                node.rotation.x = static_cast<float>(gltfNode.rotation[0]);
                node.rotation.y = static_cast<float>(gltfNode.rotation[1]);
                node.rotation.z = static_cast<float>(gltfNode.rotation[2]);
                node.rotation.w = static_cast<float>(gltfNode.rotation[3]);
            }

            if (!gltfNode.scale.empty()) {
                node.scale.x = static_cast<float>(gltfNode.scale[0]);
                node.scale.y = static_cast<float>(gltfNode.scale[1]);
                node.scale.z = static_cast<float>(gltfNode.scale[2]);
            }
            scene.nodes.push_back(node);
            continue;
        }

        scene.nodes.push_back(Node{});
    }
}

void loadMeshes(Scene& scene, const rv::Context& context, tinygltf::Model& gltfModel) {
    // Count the meshes and reserve the vector
    size_t meshCount = 0;
    for (size_t gltfMeshIndex = 0; gltfMeshIndex < gltfModel.meshes.size(); gltfMeshIndex++) {
        auto& gltfMesh = gltfModel.meshes.at(gltfMeshIndex);
        meshCount += gltfMesh.primitives.size();
    }
    scene.meshes.resize(meshCount);

    size_t meshIndex = 0;
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

                if (normalBufferView) {
                    size_t normalByteOffset = normalAccessor->byteOffset +
                                              normalBufferView->byteOffset +
                                              i * normalBufferView->byteStride;
                    vertices[i].normal = *reinterpret_cast<const glm::vec3*>(
                        &(gltfModel.buffers[normalBufferView->buffer].data[normalByteOffset]));
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
                        std::memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
                                    size);
                        for (size_t i = 0; i < indicesCount; i++) {
                            indices.push_back(buf[i]);
                        }
                        break;
                    }
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
                        uint16_t* buf = new uint16_t[indicesCount];
                        size_t size = indicesCount * sizeof(uint16_t);
                        std::memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
                                    size);
                        for (size_t i = 0; i < indicesCount; i++) {
                            indices.push_back(buf[i]);
                        }
                        break;
                    }
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
                        uint8_t* buf = new uint8_t[indicesCount];
                        size_t size = indicesCount * sizeof(uint8_t);
                        std::memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
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

            auto& mesh = scene.meshes[meshIndex];
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
            mesh.materialIndex = gltfPrimitive.material;
            meshIndex++;
        }
    }
}

void loadMaterials(Scene& scene, const rv::Context& context, tinygltf::Model& gltfModel) {
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

        scene.materials.push_back(material);
    }
}

void loadAnimation(Scene& scene, const rv::Context& context, const tinygltf::Model& model) {
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

                auto& keyFrames = scene.nodes[channel.target_node].keyFrames;
                if (keyFrames.empty()) {
                    keyFrames.resize(inputCount);
                }

                // Clear default TRS
                for (size_t i = 0; i < inputCount; i++) {
                    // KeyFrame keyframe;
                    keyFrames[i].time = inputData[i];

                    if (channel.target_path == "translation") {
                        keyFrames[i].translation.x = outputData[i * 3 + 0];
                        keyFrames[i].translation.y = outputData[i * 3 + 1];
                        keyFrames[i].translation.z = outputData[i * 3 + 2];
                    } else if (channel.target_path == "rotation") {
                        keyFrames[i].rotation.x = outputData[i * 4 + 0];
                        keyFrames[i].rotation.y = outputData[i * 4 + 1];
                        keyFrames[i].rotation.z = outputData[i * 4 + 2];
                        keyFrames[i].rotation.w = outputData[i * 4 + 3];
                    } else if (channel.target_path == "scale") {
                        keyFrames[i].scale.x = outputData[i * 3 + 0];
                        keyFrames[i].scale.y = outputData[i * 3 + 1];
                        keyFrames[i].scale.z = outputData[i * 3 + 2];
                    }
                }
            }
        }
    }
}
}  // namespace

void LoaderGltf::loadFromFile(Scene& scene,
                              const rv::Context& context,
                              const std::filesystem::path& filepath) {
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
    loadNodes(scene, context, model);
    loadMeshes(scene, context, model);
    loadMaterials(scene, context, model);
    loadAnimation(scene, context, model);
}
