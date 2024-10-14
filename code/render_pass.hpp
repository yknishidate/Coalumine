#pragma once
#include <reactive/Compiler/Compiler.hpp>
#include <reactive/Graphics/CommandBuffer.hpp>
#include <reactive/Graphics/DescriptorSet.hpp>
#include <reactive/Graphics/Pipeline.hpp>

#include "shader.hpp"

struct CompositeConstants {
    float bloomIntensity = 1.0f;
    float saturation = 1.0f;
    float exposure = 1.0f;
    float gamma = 2.2f;
    int enableToneMapping = 1;
    int enableGammaCorrection = 1;
    int _dummy0;
    int _dummy1;
};

class CompositePass {
public:
    CompositePass() = default;

    CompositePass(const rv::Context& context,
                  rv::ImageHandle baseImage,
                  rv::ImageHandle bloomImage,
                  uint32_t width,
                  uint32_t height);

    void render(const rv::CommandBufferHandle& commandBuffer,
                uint32_t countX,
                uint32_t countY,
                CompositeConstants info);

    vk::Image getOutputImageRGBA() const { return finalImageRGBA->getImage(); }
    vk::Image getOutputImageBGRA() const { return finalImageBGRA->getImage(); }

    rv::ShaderHandle shader;
    rv::DescriptorSetHandle descSet;
    rv::ComputePipelineHandle pipeline;
    rv::ImageHandle finalImageRGBA;
    rv::ImageHandle finalImageBGRA;
};

struct BloomConstants {
    int blurSize = 16;
};

class BloomPass {
public:
    BloomPass() = default;

    BloomPass(const rv::Context& context, uint32_t width, uint32_t height);

    void render(const rv::CommandBufferHandle& commandBuffer,
                uint32_t countX,
                uint32_t countY,
                BloomConstants info);

    vk::Image getOutputImage() const { return bloomImage->getImage(); }

    rv::ShaderHandle shader;
    rv::DescriptorSetHandle descSet;
    rv::ComputePipelineHandle pipeline;
    rv::ImageHandle bloomImage;
};
