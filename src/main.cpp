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

bool shouldRecompile(const std::string& shaderFileName, const std::string& entryPoint) {
    if (shaderFileName.empty()) {
        return false;
    }
    fs::create_directory(getSpvDirectory());
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    if (!fs::exists(glslFile)) {
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
    std::vector<uint32_t> spvCode;
    spdlog::info("Compile shader: {}", spvFile.string());
    spvCode = Compiler::compileToSPV(glslFile.string(), {{entryPoint, "main"}});
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
            .shaders = {&shaders[0], &shaders[1]},
            .images =
                {
                    {"baseImage", baseImage},
                    {"bloomImage", bloomImage},
                    {"finalImage", finalImage},
                },
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
        shouldRecreate |= shouldRecompile("blur.main", "main");
        shouldRecreate |= shouldRecompile("composite.main", "main");
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

        commandBuffer.imageBarrier(
            vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
            {}, baseImage, vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);
        commandBuffer.imageBarrier(
            vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
            {}, bloomImage, vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);

        // Blur
        if (pushConstants.enableBloom) {
            for (int i = 0; i < blurIteration; i++) {
                commandBuffer.bindPipeline(computePipelines["blur"]);
                commandBuffer.pushConstants(computePipelines["blur"], &pushConstants);
                commandBuffer.dispatch(computePipelines["blur"], width / 8, height / 8, 1);

                commandBuffer.imageBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                           vk::PipelineStageFlagBits::eComputeShader, {},
                                           bloomImage, vk::AccessFlagBits::eShaderWrite,
                                           vk::AccessFlagBits::eShaderRead);
            }
        }

        // Composite
        commandBuffer.bindPipeline(computePipelines["composite"]);
        commandBuffer.pushConstants(computePipelines["composite"], &pushConstants);
        commandBuffer.dispatch(computePipelines["composite"], width / 8, height / 8, 1);

        commandBuffer.endTimestamp(gpuTimer);

        commandBuffer.copyImage(finalImage.getImage(), getCurrentColorImage(),
                                vk::ImageLayout::eGeneral, vk::ImageLayout::ePresentSrcKHR, width,
                                height);
    }

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
