#include "../shader/share.h"
#include "App.hpp"

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

    void loadFromFile() {
        vertices.clear();
        indices.clear();

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        std::string objFilePath = (getAssetDirectory() / "CornellBox-Original.obj").string();
        std::string matDirectory = getAssetDirectory().string();

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objFilePath.c_str(),
                              matDirectory.c_str())) {
            throw std::runtime_error(warn + err);
        }

        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                Vertex vertex{};
                vertex.pos[0] = attrib.vertices[3 * index.vertex_index + 0];
                vertex.pos[1] = -attrib.vertices[3 * index.vertex_index + 1];
                vertex.pos[2] = attrib.vertices[3 * index.vertex_index + 2];
                vertices.push_back(vertex);
                indices.push_back(static_cast<uint32_t>(indices.size()));
            }
            // for (const auto& matIndex : shape.mesh.material_ids) {
            //     Face face;
            //     face.diffuse[0] = materials[matIndex].diffuse[0];
            //     face.diffuse[1] = materials[matIndex].diffuse[1];
            //     face.diffuse[2] = materials[matIndex].diffuse[2];
            //     face.emission[0] = materials[matIndex].emission[0];
            //     face.emission[1] = materials[matIndex].emission[1];
            //     face.emission[2] = materials[matIndex].emission[2];
            //     faces.push_back(face);
            // }
        }
    }

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

        loadFromFile();

        mesh = context.createMesh({
            .vertices = vertices,
            .indices = indices,
        });

        bottomAccel = context.createBottomAccel({
            .mesh = &mesh,
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

    std::vector<Vertex> vertices{{{-1, 0, 0}}, {{0, -1, 0}}, {{1, 0, 0}}};
    std::vector<uint32_t> indices{0, 1, 2};
    Mesh mesh;
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
