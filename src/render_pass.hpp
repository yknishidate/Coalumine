#pragma once
#include <Graphics/CommandBuffer.hpp>
#include <Graphics/DescriptorSet.hpp>

#include <Windows.h>
#undef near
#undef far
#undef RGB

namespace fs = std::filesystem;
inline fs::path getExecutableDirectory() {
    TCHAR filepath[1024];
    auto length = GetModuleFileName(NULL, filepath, 1024);
    assert(length > 0 && "Failed to query the executable path.");
    return fs::path(filepath).remove_filename();
}

inline fs::path getShaderSourceDirectory() {
    return getExecutableDirectory().parent_path().parent_path().parent_path() / "shader";
}

inline fs::path getSpvDirectory() {
    return getExecutableDirectory() / "spv";
}

inline fs::path getSpvFilePath(const std::string& shaderFileName, const std::string& entryPoint) {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    std::string spvFileName =
        glslFile.stem().string() + "_" + entryPoint + glslFile.extension().string() + ".spv";
    return getSpvDirectory() / spvFileName;
}

inline fs::path getAssetDirectory() {
    return getExecutableDirectory() / "asset";
}

inline bool shouldRecompile(const std::string& shaderFileName, const std::string& entryPoint) {
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

inline std::vector<uint32_t> compileShader(const std::string& shaderFileName,
                                           const std::string& entryPoint) {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    auto spvFile = getSpvFilePath(shaderFileName, entryPoint);
    spdlog::info("Compile shader: {}", spvFile.string());
    std::vector<uint32_t> spvCode = Compiler::compileToSPV(glslFile.string());
    File::writeBinary(spvFile, spvCode);
    return spvCode;
}

inline std::vector<uint32_t> readShader(const std::string& shaderFileName,
                                        const std::string& entryPoint) {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    auto spvFile = getSpvFilePath(shaderFileName, entryPoint);
    std::vector<uint32_t> spvCode;
    File::readBinary(spvFile, spvCode);
    return spvCode;
}

inline std::vector<uint32_t> compileOrReadShader(const std::string& shaderFileName,
                                                 const std::string& entryPoint) {
    if (shouldRecompile(shaderFileName, entryPoint)) {
        return compileShader(shaderFileName, entryPoint);
    } else {
        return readShader(shaderFileName, entryPoint);
    }
}

struct CompositeInfo {
    float bloomIntensity = 1.0;
    float saturation = 1.0;
    float exposure = 1.0;
    float gamma = 2.2;
    int enableToneMapping = 1;
    int enableGammaCorrection = 1;
};

class CompositePass {
public:
    CompositePass() = default;

    CompositePass(const Context& context,
                  const Image& baseImage,
                  const Image& bloomImage,
                  uint32_t width,
                  uint32_t height) {
        finalImage = context.createImage({
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eTransferSrc,
            .initialLayout = vk::ImageLayout::eGeneral,
            .aspect = vk::ImageAspectFlagBits::eColor,
            .width = width,
            .height = height,
        });

        shader = context.createShader({
            .code = compileShader("composite.comp", "main"),
            .stage = vk::ShaderStageFlagBits::eCompute,
        });

        descSet = context.createDescriptorSet({
            .shaders = shader,
            .images =
                {
                    {"baseImage", baseImage},
                    {"bloomImage", bloomImage},
                    {"finalImage", finalImage},
                },
        });

        pipeline = context.createComputePipeline({
            .computeShader = shader,
            .descSetLayout = descSet.getLayout(),
            .pushSize = sizeof(CompositeInfo),
        });
    }

    void render(const CommandBuffer& commandBuffer,
                uint32_t countX,
                uint32_t countY,
                CompositeInfo info) {
        commandBuffer.bindDescriptorSet(descSet, pipeline);
        commandBuffer.bindPipeline(pipeline);
        commandBuffer.pushConstants(pipeline, &info);
        commandBuffer.dispatch(pipeline, countX, countY, 1);
    }

    vk::Image getOutputImage() const { return finalImage.getImage(); }

    std::string shaderFile = "composite.comp";
    std::string entryPoint = "main";
    Shader shader;
    DescriptorSet descSet;
    ComputePipeline pipeline;
    Image finalImage;
};
