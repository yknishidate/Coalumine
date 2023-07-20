﻿#include "../shader/share.h"
#include "App.hpp"

#define TINYGLTF_IMPLEMENTATION
// #define STB_IMAGE_IMPLEMENTATION
// #define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <Windows.h>
#undef near
#undef far
#undef RGB

namespace fs = std::filesystem;
fs::path getExecutableDirectory() {
    TCHAR filepath[1024];
    auto length = GetModuleFileName(NULL, filepath, 1024);
    assert(length > 0 && "Failed to query the executable path.");
    return fs::path(filepath).remove_filename();
}

fs::path getShaderSourceDirectory() {
    return getExecutableDirectory().parent_path().parent_path().parent_path() / "shader";
}

fs::path getSpvDirectory() {
    return getExecutableDirectory() / "spv";
}

fs::path getSpvFilePath(const std::string& shaderFileName, const std::string& entryPoint) {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    std::string spvFileName =
        glslFile.stem().string() + "_" + entryPoint + glslFile.extension().string() + ".spv";
    return getSpvDirectory() / spvFileName;
}

fs::path getAssetDirectory() {
    return getExecutableDirectory() / "asset";
}

bool shouldRecompile(const std::string& shaderFileName, const std::string& entryPoint) {
    assert(!shaderFileName.empty());
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    if (!fs::exists(glslFile)) {
        spdlog::warn("GLSL file doesn't exists: {}", glslFile.string());
        return false;
    }
    auto spvFile = getSpvFilePath(shaderFileName, entryPoint);
    auto glslWriteTime = File::getLastWriteTimeWithIncludeFiles(glslFile);
    return !fs::exists(spvFile) || glslWriteTime > fs::last_write_time(spvFile);
}

std::vector<uint32_t> compileShader(const std::string& shaderFileName,
                                    const std::string& entryPoint) {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    auto spvFile = getSpvFilePath(shaderFileName, entryPoint);
    spdlog::info("Compile shader: {}", spvFile.string());
    std::vector<uint32_t> spvCode = Compiler::compileToSPV(glslFile.string());
    File::writeBinary(spvFile, spvCode);
    return spvCode;
}

std::vector<uint32_t> readShader(const std::string& shaderFileName, const std::string& entryPoint) {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    auto spvFile = getSpvFilePath(shaderFileName, entryPoint);
    std::vector<uint32_t> spvCode;
    File::readBinary(spvFile, spvCode);
    return spvCode;
}

class Scene {
public:
    Scene() = default;

    void loadFromFile(const Context& context) {
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err;
        std::string warn;

        std::string filepath = (getAssetDirectory() / "cube_v5.gltf").string();
        // std::string filepath = (getAssetDirectory() / "Box.gltf").string();
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

        spdlog::info("Meshes: {}\n", model.meshes.size());
        loadMeshes(context, model);
    }

    void loadMeshes(const Context& context, tinygltf::Model& gltfModel) {
        for (int gltfMeshIndex = 0; gltfMeshIndex < gltfModel.meshes.size(); gltfMeshIndex++) {
            auto& gltfMesh = gltfModel.meshes.at(gltfMeshIndex);
            for (const auto& gltfPrimitive : gltfMesh.primitives) {
                std::vector<Vertex> vertices;
                std::vector<uint32_t> indices;

                // Vertex attributes
                auto& attributes = gltfPrimitive.attributes;
                const float* pos = nullptr;
                const float* normal = nullptr;
                const float* uv = nullptr;

                assert(attributes.find("POSITION") != attributes.end());

                // auto& accessor = gltfModel.accessors[attributes.find("POSITION")->second];
                // auto& bufferView = gltfModel.bufferViews[accessor.bufferView];
                // auto& buffer = gltfModel.buffers[bufferView.buffer];
                // pos = reinterpret_cast<const float*>(&(buffer.data[accessor.byteOffset +
                // bufferView.byteOffset])); size_t verticesCount = accessor.count;

                auto& accessor = gltfModel.accessors[attributes.find("POSITION")->second];
                auto& bufferView = gltfModel.bufferViews[accessor.bufferView];
                auto& buffer = gltfModel.buffers[bufferView.buffer];

                // Get the byteOffset and byteStride
                size_t byteOffset = accessor.byteOffset + bufferView.byteOffset;
                size_t byteStride =
                    bufferView.byteStride
                        ? bufferView.byteStride
                        : sizeof(float) * 3;  // Assume 3 components if byteStride is not defined

                // Loop over the data considering the byteStride
                for (size_t i = byteOffset; i < byteOffset + byteStride * accessor.count;
                     i += byteStride) {
                    // Cast the data to float and add to the positions vector
                    vertices.push_back(
                        Vertex(*reinterpret_cast<const glm::vec3*>(&(buffer.data[i]))));
                }

                // if (attributes.find("NORMAL") != attributes.end()) {
                //     auto& accessor = gltfModel.accessors[attributes.find("NORMAL")->second];
                //     auto& bufferView = gltfModel.bufferViews[accessor.bufferView];
                //     auto& buffer = gltfModel.buffers[bufferView.buffer];
                //     normal = reinterpret_cast<const float*>(
                //         &(buffer.data[accessor.byteOffset + bufferView.byteOffset]));
                // }
                // if (attributes.find("TEXCOORD_0") != attributes.end()) {
                //     auto& accessor = gltfModel.accessors[attributes.find("TEXCOORD_0")->second];
                //     auto& bufferView = gltfModel.bufferViews[accessor.bufferView];
                //     auto& buffer = gltfModel.buffers[bufferView.buffer];
                //     uv = reinterpret_cast<const float*>(
                //         &(buffer.data[accessor.byteOffset + bufferView.byteOffset]));
                // }

                // Pack data to vertex array
                // for (size_t v = 0; v < verticesCount; v++) {
                //    Vertex vert{};
                //    vert.pos = glm::make_vec3(&pos[v * 3]);
                //    // vert.normal = glm::normalize(
                //    //     glm::vec3(normal ? glm::make_vec3(&normal[v * 3]) : glm::vec3(0.0f)));
                //    //// vert.pos = -vert.pos;
                //    //// vert.normal = -vert.normal;
                //    // vert.texCoord = uv ? glm::make_vec2(&uv[v * 2]) : glm::vec2(0.0f);
                //    // spdlog::info("pos: {}, norm: {}, uv: {}\n", glm::to_string(vert.pos),
                //    //              glm::to_string(vert.normal), glm::to_string(vert.texCoord));
                //    vertices.push_back(vert);
                //}

                // Get indices
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

                spdlog::info("Mesh: vertex={}, index={}\n", vertices.size(), indices.size());
                for (auto& v : vertices) {
                    std::cerr << glm::to_string(v.pos) << std::endl;
                }
                for (auto& i : indices) {
                    std::cerr << i << std::endl;
                }
                meshes.push_back(context.createMesh({
                    .vertices = vertices,
                    .indices = indices,
                }));
            }
        }
    }

    std::vector<Mesh> meshes;
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
        std::vector<Shader> shaders(5);
        shaders[0] = context.createShader({
            .code = compileShader("blur.comp", "main"),
            .stage = vk::ShaderStageFlagBits::eCompute,
        });
        shaders[1] = context.createShader({
            .code = compileShader("composite.comp", "main"),
            .stage = vk::ShaderStageFlagBits::eCompute,
        });
        shaders[2] = context.createShader({
            .code = compileShader("base.rgen", "main"),
            .stage = vk::ShaderStageFlagBits::eRaygenKHR,
        });
        shaders[3] = context.createShader({
            .code = compileShader("base.rmiss", "main"),
            .stage = vk::ShaderStageFlagBits::eMissKHR,
        });
        shaders[4] = context.createShader({
            .code = compileShader("base.rchit", "main"),
            .stage = vk::ShaderStageFlagBits::eClosestHitKHR,
        });

        descSet = context.createDescriptorSet({
            .shaders = shaders,
            .buffers =
                {
                    {"Vertices", scene.meshes[0].vertexBuffer},
                    {"Indices", scene.meshes[0].indexBuffer},
                },
            .images =
                {
                    {"baseImage", baseImage},
                    {"bloomImage", bloomImage},
                    {"finalImage", finalImage},
                },
            .accels = {{"topLevelAS", topAccel}},
        });

        computePipelines["blur"] = context.createComputePipeline({
            .computeShader = shaders[0],
            .descSetLayout = descSet.getLayout(),
            .pushSize = sizeof(PushConstants),
        });
        computePipelines["composite"] = context.createComputePipeline({
            .computeShader = shaders[1],
            .descSetLayout = descSet.getLayout(),
            .pushSize = sizeof(PushConstants),
        });
        rayTracingPipeline = context.createRayTracingPipeline({
            .rgenShader = shaders[2],
            .missShader = shaders[3],
            .chitShader = shaders[4],
            .descSetLayout = descSet.getLayout(),
            .pushSize = sizeof(PushConstants),
            .maxRayRecursionDepth = 2,
        });
    }

    void onStart() override {
        spdlog::info("Executable directory: {}", getExecutableDirectory().string());
        spdlog::info("Shader source directory: {}", getShaderSourceDirectory().string());
        spdlog::info("SPIR-V directory: {}", getSpvDirectory().string());
        fs::create_directory(getSpvDirectory());

        scene.loadFromFile(context);

        bottomAccel = context.createBottomAccel({
            .mesh = &scene.meshes[0],
        });

        topAccel = context.createTopAccel({
            .bottomAccels = {{&bottomAccel, glm::mat4{1.0}}},
        });

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

        finalImage = context.createImage({
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eTransferSrc,
            .initialLayout = vk::ImageLayout::eGeneral,
            .aspect = vk::ImageAspectFlagBits::eColor,
            .width = width,
            .height = height,
        });

        createPipelines();

        camera = OrbitalCamera{this, width, height};

        gpuTimer = context.createGPUTimer({});
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
        shouldRecreate |= shouldRecompile("composite.comp", "main");
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
            ImGui::SliderFloat("Bloom intensity", &pushConstants.bloomIntensity, 0.0, 10.0);
            ImGui::SliderFloat("Bloom threshold", &pushConstants.bloomThreshold, 0.0, 2.0);
            ImGui::SliderInt("Blur iteration", &blurIteration, 0, 64);
            ImGui::SliderInt("Blur size", &pushConstants.blurSize, 0, 64);
        }

        // Show GPU time
        if (pushConstants.frame > 1) {
            ImGui::Text("GPU time: %f ms", gpuTimer.elapsedInMilli());
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

        // Composite
        commandBuffer.bindDescriptorSet(descSet, computePipelines["composite"]);
        commandBuffer.bindPipeline(computePipelines["composite"]);
        commandBuffer.pushConstants(computePipelines["composite"], &pushConstants);
        commandBuffer.dispatch(computePipelines["composite"], width / 8, height / 8, 1);

        commandBuffer.endTimestamp(gpuTimer);

        commandBuffer.copyImage(finalImage.getImage(), getCurrentColorImage(),
                                vk::ImageLayout::eGeneral, vk::ImageLayout::ePresentSrcKHR, width,
                                height);
    }

    Scene scene;

    BottomAccel bottomAccel;
    TopAccel topAccel;

    Image baseImage;
    Image volumeImage;
    Image bloomImage;
    Image finalImage;

    DescriptorSet descSet;
    RayTracingPipeline rayTracingPipeline;
    std::unordered_map<std::string, ComputePipeline> computePipelines;
    OrbitalCamera camera;
    PushConstants pushConstants;
    GPUTimer gpuTimer;
};

int main() {
    try {
        HelloApp app{};
        app.run();
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}
