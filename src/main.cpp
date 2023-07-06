#include "../shader/share.h"
#include "App.hpp"

#define NOMINMAX
#include <Windows.h>
#undef near
#undef far
#undef RGB

namespace fs = std::filesystem;
fs::path getExecutableDirectory() {
    TCHAR filepath[1024];
    auto length = GetModuleFileName(NULL, filepath, 1024);
    assert(length > 0, "Failed to query the executable path.");
    return fs::path(filepath).remove_filename();
}

fs::path getShaderSourceDirectory() {
    return getExecutableDirectory().parent_path().parent_path().parent_path() / "shader";
}

fs::path getSpvDirectory() {
    return getExecutableDirectory() / "spv";
}

fs::path getSpvFilePath(const std::string& shaderFileName, const std::string& entryPoint = "main") {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    std::string spvFileName =
        glslFile.stem().string() + "_" + entryPoint + glslFile.extension().string() + ".spv";
    return getSpvDirectory() / spvFileName;
}

bool shouldRecompile(const std::string& shaderFileName, const std::string& entryPoint = "main") {
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

std::vector<uint32_t> compileOrReadShader(const std::string& shaderFileName,
                                          const std::string& entryPoint = "main") {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    auto spvFile = getSpvFilePath(shaderFileName, entryPoint);
    std::vector<uint32_t> spvCode;
    spdlog::info("Check last write time of the shader: {}", shaderFileName);
    if (shouldRecompile(shaderFileName, entryPoint)) {
        spdlog::info("Compile shader: {}", spvFile.string());
        spvCode = Compiler::compileToSPV(glslFile.string(), {{entryPoint, "main"}});
        File::writeBinary(spvFile, spvCode);
    } else {
        File::readBinary(spvFile, spvCode);
    }
    return spvCode;
}

class HelloApp : public App {
public:
    HelloApp()
        : App({
              .width = 1280,
              .height = 720,
              .title = "HelloCompute",
          }) {}

    void onStart() override {
        spdlog::info("Executable directory: {}", getExecutableDirectory().string());
        spdlog::info("Shader source directory: {}", getShaderSourceDirectory().string());
        spdlog::info("SPIR-V directory: {}", getSpvDirectory().string());

        image = context.createImage({
            .usage = ImageUsage::GeneralStorage,
            .width = width,
            .height = height,
        });

        std::vector<uint32_t> code = compileOrReadShader("render.comp");

        Shader compShader = context.createShader({
            .code = code,
            .stage = vk::ShaderStageFlagBits::eCompute,
        });

        descSet = context.createDescriptorSet({
            .shaders = {&compShader},
            .images = {{"outputImage", image}},
        });

        pipeline = context.createComputePipeline({
            .computeShader = compShader,
            .descSetLayout = descSet.getLayout(),
            .pushSize = sizeof(int),
        });

        camera = OrbitalCamera{this, width, height};
    }

    void onUpdate() override { pushConstants.frame++; }

    void onRender(const CommandBuffer& commandBuffer) override {
        ImGui::SliderInt("Test slider", &testInt, 0, 100);
        commandBuffer.bindDescriptorSet(descSet, pipeline);
        commandBuffer.bindPipeline(pipeline);
        commandBuffer.pushConstants(pipeline, &pushConstants.frame);
        commandBuffer.dispatch(pipeline, width, height, 1);
        commandBuffer.copyImage(image.getImage(), getCurrentColorImage(), vk::ImageLayout::eGeneral,
                                vk::ImageLayout::ePresentSrcKHR, width, height);
    }

    Image image;
    DescriptorSet descSet;
    ComputePipeline pipeline;
    OrbitalCamera camera;
    int testInt = 0;
    PushConstants pushConstants;
};

int main() {
    try {
        HelloApp app{};
        app.run();
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}
