#include "render_pass.hpp"

CompositePass::CompositePass(const rv::Context& context,
                             rv::ImageHandle baseImage,
                             rv::ImageHandle bloomImage,
                             uint32_t width,
                             uint32_t height) {
    m_finalImageRGBA = context.createImage({
        .usage = rv::ImageUsage::Storage,
        .extent = {width, height, 1},
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .viewInfo = rv::ImageViewCreateInfo{},
        .debugName = "finalImageRGBA",
    });

    m_finalImageBGRA = context.createImage({
        .usage = rv::ImageUsage::Storage,
        .extent = {width, height, 1},
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eB8G8R8A8Unorm,
        .viewInfo = rv::ImageViewCreateInfo{},
        .debugName = "finalImageBGRA",
    });

    context.oneTimeSubmit([&](auto commandBuffer) {
        commandBuffer->transitionLayout(m_finalImageRGBA, vk::ImageLayout::eGeneral);
        commandBuffer->transitionLayout(m_finalImageBGRA, vk::ImageLayout::eGeneral);
    });

    m_shader = context.createShader({
        .code = readShader("composite.comp", "main"),
        .stage = vk::ShaderStageFlagBits::eCompute,
    });

    m_descSet = context.createDescriptorSet({
        .shaders = m_shader,
        .images =
            {
                {"baseImage", baseImage},
                {"bloomImage", bloomImage},
                {"finalImageRGBA", m_finalImageRGBA},
                {"finalImageBGRA", m_finalImageBGRA},
            },
    });
    m_descSet->update();

    m_pipeline = context.createComputePipeline({
        .descSetLayout = m_descSet->getLayout(),
        .pushSize = sizeof(CompositeConstants),
        .computeShader = m_shader,
    });
}

void CompositePass::render(const rv::CommandBufferHandle& commandBuffer,
                           uint32_t countX,
                           uint32_t countY,
                           CompositeConstants info) {
    commandBuffer->bindDescriptorSet(m_pipeline, m_descSet);
    commandBuffer->bindPipeline(m_pipeline);
    commandBuffer->pushConstants(m_pipeline, &info);
    commandBuffer->dispatch(countX, countY, 1);
}

BloomPass::BloomPass(const rv::Context& context, uint32_t width, uint32_t height) {
    m_bloomImage = context.createImage({
        .usage = rv::ImageUsage::Storage,
        .extent = {width, height, 1},
        .format = vk::Format::eR32G32B32A32Sfloat,
        .viewInfo = rv::ImageViewCreateInfo{},
        .debugName = "bloomImage",
    });

    context.oneTimeSubmit([&](auto commandBuffer) {
        commandBuffer->transitionLayout(m_bloomImage, vk::ImageLayout::eGeneral);
    });

    m_shader = context.createShader({
        .code = readShader("blur.comp", "main"),
        .stage = vk::ShaderStageFlagBits::eCompute,
    });

    m_descSet = context.createDescriptorSet({
        .shaders = m_shader,
        .images =
            {
                {"bloomImage", m_bloomImage},
            },
    });
    m_descSet->update();

    m_pipeline = context.createComputePipeline({
        .descSetLayout = m_descSet->getLayout(),
        .pushSize = sizeof(BloomConstants),
        .computeShader = m_shader,
    });
}

void BloomPass::render(const rv::CommandBufferHandle& commandBuffer,
                       uint32_t countX,
                       uint32_t countY,
                       BloomConstants info) {
    commandBuffer->bindDescriptorSet(m_pipeline, m_descSet);
    commandBuffer->bindPipeline(m_pipeline);
    commandBuffer->pushConstants(m_pipeline, &info);
    commandBuffer->dispatch(countX, countY, 1);
    commandBuffer->imageBarrier(m_bloomImage, vk::PipelineStageFlagBits::eComputeShader,
                                vk::PipelineStageFlagBits::eComputeShader,
                                vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);
}
