#include "../shader/share.h"
#include "App.hpp"

#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include "render_pass.hpp"

class Node {
public:
    int meshIndex;
    glm::vec3 translation = glm::vec3{0.0, 0.0, 0.0};
    glm::quat rotation = glm::quat{0.0, 0.0, 0.0, 0.0};
    glm::vec3 scale = glm::vec3{1.0, 1.0, 1.0};

    glm::mat4 computeTransformMatrix() const {
        glm::mat4 T = glm::translate(glm::mat4{1.0}, translation);
        glm::mat4 R = glm::mat4_cast(rotation);
        glm::mat4 S = glm::scale(glm::mat4{1.0}, scale);
        return T * R * S;
    }

    glm::mat3 computeNormalMatrix() const {
        return glm::transpose(glm::inverse(glm::mat3{computeTransformMatrix()}));
    }
};

class Scene {
public:
    Scene() = default;

    void loadFromFile(const Context& context) {
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err;
        std::string warn;

        std::string filepath = (getAssetDirectory() / "glass_test_v2.gltf").string();
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
    }

    void loadDomeLightTexture(const Context& context) {
        int width;
        int height;
        int comp;
        std::string filepath = (getAssetDirectory() / "solitude_interior_4k.hdr").string();
        float* pixels = stbi_loadf(filepath.c_str(), &width, &height, &comp, 0);
        if (!pixels) {
            throw std::runtime_error("Failed to load image: " + filepath);
        }
        spdlog::info("DomeLightTexture: w={}, h={}, c={}", width, height, comp);

        Buffer stagingBuffer = context.createHostBuffer({
            .usage = BufferUsage::Staging,
            .size = width * height * comp * sizeof(float),
            .data = reinterpret_cast<void*>(pixels),
        });

        domeLightTexture = context.createImage({
            .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
                     vk::ImageUsageFlagBits::eTransferDst,
            .initialLayout = vk::ImageLayout::eTransferDstOptimal,
            .aspect = vk::ImageAspectFlagBits::eColor,
            .width = static_cast<uint32_t>(width),
            .height = static_cast<uint32_t>(height),
            .depth = 1,
            .format = vk::Format::eR32G32B32Sfloat,
        });

        vk::ImageSubresourceLayers subresourceLayers;
        subresourceLayers.setAspectMask(vk::ImageAspectFlagBits::eColor);
        subresourceLayers.setMipLevel(0);
        subresourceLayers.setBaseArrayLayer(0);
        subresourceLayers.setLayerCount(1);

        vk::Extent3D extent;
        extent.setWidth(static_cast<uint32_t>(width));
        extent.setHeight(static_cast<uint32_t>(height));
        extent.setDepth(1);

        vk::BufferImageCopy region;
        region.imageSubresource = subresourceLayers;
        region.imageExtent = extent;

        context.oneTimeSubmit([&](vk::CommandBuffer commandBuffer) {
            commandBuffer.copyBufferToImage(stagingBuffer.getBuffer(), domeLightTexture.getImage(),
                                            vk::ImageLayout::eTransferDstOptimal, region);
            Image::setImageLayout(commandBuffer, domeLightTexture.getImage(),
                                  vk::ImageLayout::eReadOnlyOptimal);
        });

        stbi_image_free(pixels);
    }

    void loadNodes(const Context& context, tinygltf::Model& gltfModel) {
        for (int gltfNodeIndex = 0; gltfNodeIndex < gltfModel.nodes.size(); gltfNodeIndex++) {
            auto& gltfNode = gltfModel.nodes.at(gltfNodeIndex);
            if (gltfNode.camera != -1) {
                continue;
            }
            if (gltfNode.skin != -1) {
                continue;
            }

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
        }
    }

    void loadMeshes(const Context& context, tinygltf::Model& gltfModel) {
        for (int gltfMeshIndex = 0; gltfMeshIndex < gltfModel.meshes.size(); gltfMeshIndex++) {
            auto& gltfMesh = gltfModel.meshes.at(gltfMeshIndex);
            for (const auto& gltfPrimitive : gltfMesh.primitives) {
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
            buildAccels.push_back({&bottomAccels[node.meshIndex], node.computeTransformMatrix()});
        }

        topAccel = context.createTopAccel({
            .bottomAccels = buildAccels,
        });
    }

    std::vector<Node> nodes;
    std::vector<DeviceBuffer> vertexBuffers;
    std::vector<DeviceBuffer> indexBuffers;
    std::vector<uint32_t> vertexCounts;
    std::vector<uint32_t> triangleCounts;

    std::vector<BottomAccel> bottomAccels;
    TopAccel topAccel;
    Image domeLightTexture;
};

class HelloApp : public App {
public:
    HelloApp()
        : App({
              .width = 1920,
              .height = 1080,
              .title = "rtcamp9",
              .enableValidation = true,
              .enableRayTracing = true,
          }) {}

    void createPipelines() {
        std::vector<Shader> shaders(4);
        shaders[0] = context.createShader({
            .code = compileShader("blur.comp", "main"),
            .stage = vk::ShaderStageFlagBits::eCompute,
        });
        shaders[1] = context.createShader({
            .code = compileShader("base.rgen", "main"),
            .stage = vk::ShaderStageFlagBits::eRaygenKHR,
        });
        shaders[2] = context.createShader({
            .code = compileShader("base.rmiss", "main"),
            .stage = vk::ShaderStageFlagBits::eMissKHR,
        });
        shaders[3] = context.createShader({
            .code = compileShader("base.rchit", "main"),
            .stage = vk::ShaderStageFlagBits::eClosestHitKHR,
        });

        descSet = context.createDescriptorSet({
            .shaders = shaders,
            .buffers =
                {
                    {"VertexBuffers", scene.vertexBuffers},
                    {"IndexBuffers", scene.indexBuffers},
                },
            .images =
                {
                    {"baseImage", baseImage},
                    {"bloomImage", bloomImage},
                    {"domeLightTexture", scene.domeLightTexture},
                },
            .accels = {{"topLevelAS", scene.topAccel}},
        });

        computePipelines["blur"] = context.createComputePipeline({
            .computeShader = shaders[0],
            .descSetLayout = descSet.getLayout(),
            .pushSize = sizeof(PushConstants),
        });
        rayTracingPipeline = context.createRayTracingPipeline({
            .rgenShader = shaders[1],
            .missShader = shaders[2],
            .chitShader = shaders[3],
            .descSetLayout = descSet.getLayout(),
            .pushSize = sizeof(PushConstants),
            .maxRayRecursionDepth = 31,
        });

        compositePass = CompositePass(context, baseImage, bloomImage, width, height);
    }

    void onStart() override {
        spdlog::info("Executable directory: {}", getExecutableDirectory().string());
        spdlog::info("Shader source directory: {}", getShaderSourceDirectory().string());
        spdlog::info("SPIR-V directory: {}", getSpvDirectory().string());
        fs::create_directory(getSpvDirectory());

        // Output ray tracing props
        auto rtProps =
            context
                .getPhysicalDeviceProperties2<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
        spdlog::info("RayTracingPipelineProperties::maxRayRecursionDepth: {}",
                     rtProps.maxRayRecursionDepth);

        scene.loadFromFile(context);
        scene.buildAccels(context);
        scene.loadDomeLightTexture(context);

        baseImage = context.createImage({
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eTransferSrc,
            .initialLayout = vk::ImageLayout::eGeneral,
            .aspect = vk::ImageAspectFlagBits::eColor,
            .width = width,
            .height = height,
            .format = vk::Format::eR32G32B32A32Sfloat,
        });

        bloomImage = context.createImage({
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eTransferSrc,
            .initialLayout = vk::ImageLayout::eGeneral,
            .aspect = vk::ImageAspectFlagBits::eColor,
            .width = width,
            .height = height,
            .format = vk::Format::eR32G32B32A32Sfloat,
        });

        createPipelines();

        camera = OrbitalCamera{this, width, height};

        gpuTimer = context.createGPUTimer({});

        imageSavingBuffer = context.createHostBuffer({
            .usage = BufferUsage::Staging,
            .size = width * height * 4 * sizeof(uint8_t),
        });
    }

    void onUpdate() override {
        camera.processInput();
        pushConstants.frame++;
        pushConstants.invView = camera.getInvView();
        pushConstants.invProj = camera.getInvProj();
    }

    void recreatePipelinesIfShadersWereUpdated() {
        bool shouldRecreate = false;
        shouldRecreate |= shouldRecompile("blur.comp", "main");
        shouldRecreate |= shouldRecompile("base.rgen", "main");
        shouldRecreate |= shouldRecompile("base.rchit", "main");
        shouldRecreate |= shouldRecompile("base.rmiss", "main");
        if (shouldRecreate) {
            try {
                createPipelines();
            } catch (const std::exception& e) {
                spdlog::error(e.what());
            }
        }
    }

    void onRender(const CommandBuffer& commandBuffer) override {
        static int imageIndex = 0;
        static int blurIteration = 32;
        ImGui::Combo("Image", &imageIndex, "Render\0Bloom");

        // Bloom
        ImGui::Checkbox("Enable bloom", reinterpret_cast<bool*>(&pushConstants.enableBloom));
        if (pushConstants.enableBloom) {
            ImGui::SliderFloat("Bloom intensity", &compositeInfo.bloomIntensity, 0.0, 10.0);
            ImGui::SliderFloat("Bloom threshold", &pushConstants.bloomThreshold, 0.0, 2.0);
            ImGui::SliderInt("Blur iteration", &blurIteration, 0, 64);
            ImGui::SliderInt("Blur size", &pushConstants.blurSize, 0, 64);
        }

        // Tone mapping
        ImGui::Checkbox("Enable tone mapping",
                        reinterpret_cast<bool*>(&compositeInfo.enableToneMapping));
        if (compositeInfo.enableToneMapping) {
            ImGui::SliderFloat("Exposure", &compositeInfo.exposure, 0.0, 5.0);
        }

        // Gamma correction
        ImGui::Checkbox("Enable gamma correction",
                        reinterpret_cast<bool*>(&compositeInfo.enableGammaCorrection));
        if (compositeInfo.enableGammaCorrection) {
            ImGui::SliderFloat("Gamma", &compositeInfo.gamma, 0.0, 5.0);
        }

        // Show GPU time
        if (pushConstants.frame > 1) {
            ImGui::Text("GPU time: %f ms", gpuTimer.elapsedInMilli());
        }

        // Save image
        if (ImGui::Button("Save image")) {
            saveImage();
        }

        // Check shader files
        recreatePipelinesIfShadersWereUpdated();

        commandBuffer.beginTimestamp(gpuTimer);

        // Base rendering

        ///
        /// Render base image here
        ///
        commandBuffer.bindDescriptorSet(descSet, rayTracingPipeline);
        commandBuffer.bindPipeline(rayTracingPipeline);
        commandBuffer.pushConstants(rayTracingPipeline, &pushConstants);
        commandBuffer.traceRays(rayTracingPipeline, width, height, 1);

        commandBuffer.imageBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                   vk::PipelineStageFlagBits::eComputeShader, {}, baseImage,
                                   vk::AccessFlagBits::eShaderWrite,
                                   vk::AccessFlagBits::eShaderRead);
        commandBuffer.imageBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                   vk::PipelineStageFlagBits::eComputeShader, {}, bloomImage,
                                   vk::AccessFlagBits::eShaderWrite,
                                   vk::AccessFlagBits::eShaderRead);

        //// Blur
        // if (pushConstants.enableBloom) {
        //     for (int i = 0; i < blurIteration; i++) {
        //         commandBuffer.bindPipeline(computePipelines["blur"]);
        //         commandBuffer.pushConstants(computePipelines["blur"], &pushConstants);
        //         commandBuffer.dispatch(computePipelines["blur"], width / 8, height / 8, 1);

        //        commandBuffer.imageBarrier(vk::PipelineStageFlagBits::eComputeShader,
        //                                   vk::PipelineStageFlagBits::eComputeShader, {},
        //                                   bloomImage, vk::AccessFlagBits::eShaderWrite,
        //                                   vk::AccessFlagBits::eShaderRead);
        //    }
        //}

        commandBuffer.endTimestamp(gpuTimer);

        compositePass.render(commandBuffer, width / 8, height / 8, compositeInfo);

        commandBuffer.copyImage(compositePass.getOutputImage(), getCurrentColorImage(),
                                vk::ImageLayout::eGeneral, vk::ImageLayout::ePresentSrcKHR, width,
                                height);
    }

    void saveImage() {
        auto* pixels = static_cast<uint8_t*>(imageSavingBuffer.map());

        context.oneTimeSubmit([&](vk::CommandBuffer commandBuffer) {
            Image::setImageLayout(commandBuffer, compositePass.getOutputImage(),
                                  vk::ImageLayout::eTransferSrcOptimal);

            vk::BufferImageCopy copyInfo;
            copyInfo.setImageExtent({width, height, 1});
            copyInfo.setImageSubresource({vk::ImageAspectFlagBits::eColor, 0, 0, 1});
            commandBuffer.copyImageToBuffer(compositePass.getOutputImage(),
                                            vk::ImageLayout::eTransferSrcOptimal,
                                            imageSavingBuffer.getBuffer(), copyInfo);

            Image::setImageLayout(commandBuffer, compositePass.getOutputImage(),
                                  vk::ImageLayout::eGeneral);
        });

        std::string frame = std::to_string(pushConstants.frame);
        std::string zeros = std::string(std::max(0, 3 - (int)frame.size()), '0');
        std::string img = zeros + frame + ".png";
        stbi_write_png(img.c_str(), width, height, 4, pixels, width * 4);
    }

    Scene scene;

    CompositeInfo compositeInfo;
    CompositePass compositePass;

    Image baseImage;
    Image volumeImage;
    Image bloomImage;

    DescriptorSet descSet;
    RayTracingPipeline rayTracingPipeline;
    std::unordered_map<std::string, ComputePipeline> computePipelines;
    OrbitalCamera camera;
    PushConstants pushConstants;
    GPUTimer gpuTimer;

    HostBuffer imageSavingBuffer;
};

int main() {
    try {
        HelloApp app{};
        app.run();
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}
