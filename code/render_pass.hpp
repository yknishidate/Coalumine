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

    const rv::ImageHandle& getOutputImageRGBA() const { return m_finalImageRGBA; }

    const rv::ImageHandle& getOutputImageBGRA() const { return m_finalImageBGRA; }

private:
    rv::ShaderHandle m_shader;
    rv::DescriptorSetHandle m_descSet;
    rv::ComputePipelineHandle m_pipeline;
    rv::ImageHandle m_finalImageRGBA;
    rv::ImageHandle m_finalImageBGRA;
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

    const rv::ImageHandle& getOutputImage() const { return m_bloomImage; }

private:
    rv::ShaderHandle m_shader;
    rv::DescriptorSetHandle m_descSet;
    rv::ComputePipelineHandle m_pipeline;
    rv::ImageHandle m_bloomImage;
};
