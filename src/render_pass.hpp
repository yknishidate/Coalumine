#pragma once
#include <reactive/Compiler/Compiler.hpp>
#include <reactive/Graphics/CommandBuffer.hpp>
#include <reactive/Graphics/DescriptorSet.hpp>

#define NOMINMAX
#include <Windows.h>
#undef near
#undef far
#undef RGB

using namespace rv;

namespace fs = std::filesystem;
inline fs::path getExecutableDirectory() {
    TCHAR filepath[1024];
    auto length = GetModuleFileName(NULL, filepath, 1024);
    assert(length > 0 && "Failed to query the executable path.");
    return fs::path(filepath).remove_filename();
}

inline fs::path getShaderSourceDirectory() {
    // exectable:  project/build/preset/config/*.exe
    // shader:     project/shader/*.glsl
    const auto projectRoot =
        getExecutableDirectory().parent_path().parent_path().parent_path().parent_path();
    return projectRoot / "shader";
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
    auto glslWriteTime = Compiler::getLastWriteTimeWithIncludeFiles(glslFile);
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
    float bloomIntensity = 1.0f;
    float saturation = 1.0f;
    float exposure = 1.0f;
    float gamma = 2.2f;
    int enableToneMapping = 1;
    int enableGammaCorrection = 1;
};

class CompositePass {
public:
    CompositePass() = default;

    CompositePass(const Context& context,
                  ImageHandle baseImage,
                  ImageHandle bloomImage,
                  uint32_t width,
                  uint32_t height) {
        finalImageRGBA = context.createImage({
            .usage = rv::ImageUsage::Storage,
            .extent = {width, height, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
            .debugName = "finalImageRGBA",
        });

        finalImageBGRA = context.createImage({
            .usage = rv::ImageUsage::Storage,
            .extent = {width, height, 1},
            .format = vk::Format::eB8G8R8A8Unorm,
            .debugName = "finalImageBGRA",
        });

        finalImageRGBA->createImageView();
        finalImageBGRA->createImageView();

        context.oneTimeSubmit([&](auto commandBuffer) {
            commandBuffer->transitionLayout(finalImageRGBA, vk::ImageLayout::eGeneral);
            commandBuffer->transitionLayout(finalImageBGRA, vk::ImageLayout::eGeneral);
        });

        shader = context.createShader({
            .code = readShader("composite.comp", "main"),
            .stage = vk::ShaderStageFlagBits::eCompute,
        });

        descSet = context.createDescriptorSet({
            .shaders = shader,
            .images =
                {
                    {"baseImage", baseImage},
                    {"bloomImage", bloomImage},
                    {"finalImageRGBA", finalImageRGBA},
                    {"finalImageBGRA", finalImageBGRA},
                },
        });
        descSet->update();

        pipeline = context.createComputePipeline({
            .computeShader = shader,
            .descSetLayout = descSet->getLayout(),
            .pushSize = sizeof(CompositeInfo),
        });
    }

    void render(const CommandBufferHandle& commandBuffer,
                uint32_t countX,
                uint32_t countY,
                CompositeInfo info) {
        commandBuffer->bindDescriptorSet(descSet, pipeline);
        commandBuffer->bindPipeline(pipeline);
        commandBuffer->pushConstants(pipeline, &info);
        commandBuffer->dispatch(countX, countY, 1);
    }

    vk::Image getOutputImageRGBA() const { return finalImageRGBA->getImage(); }
    vk::Image getOutputImageBGRA() const { return finalImageBGRA->getImage(); }

    ShaderHandle shader;
    DescriptorSetHandle descSet;
    ComputePipelineHandle pipeline;
    ImageHandle finalImageRGBA;
    ImageHandle finalImageBGRA;
};

struct BloomInfo {
    int blurSize = 16;
};

class BloomPass {
public:
    BloomPass() = default;

    BloomPass(const Context& context, uint32_t width, uint32_t height) {
        bloomImage = context.createImage({
            .usage = rv::ImageUsage::Storage,
            .extent = {width, height, 1},
            .format = vk::Format::eR32G32B32A32Sfloat,
            .debugName = "bloomImage",
        });

        bloomImage->createImageView();
        // bloomImage->createSampler();

        context.oneTimeSubmit([&](auto commandBuffer) {
            commandBuffer->transitionLayout(bloomImage, vk::ImageLayout::eGeneral);
        });

        shader = context.createShader({
            .code = readShader("blur.comp", "main"),
            .stage = vk::ShaderStageFlagBits::eCompute,
        });

        descSet = context.createDescriptorSet({
            .shaders = shader,
            .images =
                {
                    {"bloomImage", bloomImage},
                },
        });

        pipeline = context.createComputePipeline({
            .computeShader = shader,
            .descSetLayout = descSet->getLayout(),
            .pushSize = sizeof(BloomInfo),
        });
    }

    void render(const CommandBufferHandle& commandBuffer,
                uint32_t countX,
                uint32_t countY,
                BloomInfo info) {
        commandBuffer->bindDescriptorSet(descSet, pipeline);
        commandBuffer->bindPipeline(pipeline);
        commandBuffer->pushConstants(pipeline, &info);
        commandBuffer->dispatch(countX, countY, 1);
        commandBuffer->imageBarrier(bloomImage, vk::PipelineStageFlagBits::eComputeShader,
                                    vk::PipelineStageFlagBits::eComputeShader,
                                    vk::AccessFlagBits::eShaderWrite,
                                    vk::AccessFlagBits::eShaderRead);
    }

    vk::Image getOutputImage() const { return bloomImage->getImage(); }

    ShaderHandle shader;
    DescriptorSetHandle descSet;
    ComputePipelineHandle pipeline;
    ImageHandle bloomImage;
};
