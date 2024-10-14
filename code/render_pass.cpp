#include "render_pass.hpp"

CompositePass::CompositePass(const rv::Context& context,
                             rv::ImageHandle baseImage,
                             rv::ImageHandle bloomImage,
                             uint32_t width,
                             uint32_t height) {
    finalImageRGBA = context.createImage({
        .usage = rv::ImageUsage::Storage,
        .extent = {width, height, 1},
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .viewInfo = rv::ImageViewCreateInfo{},
        .debugName = "finalImageRGBA",
    });

    finalImageBGRA = context.createImage({
        .usage = rv::ImageUsage::Storage,
        .extent = {width, height, 1},
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eB8G8R8A8Unorm,
        .viewInfo = rv::ImageViewCreateInfo{},
        .debugName = "finalImageBGRA",
    });

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
        .descSetLayout = descSet->getLayout(),
        .pushSize = sizeof(CompositeConstants),
        .computeShader = shader,
    });
}

void CompositePass::render(const rv::CommandBufferHandle& commandBuffer,
                           uint32_t countX,
                           uint32_t countY,
                           CompositeConstants info) {
    commandBuffer->bindDescriptorSet(pipeline, descSet);
    commandBuffer->bindPipeline(pipeline);
    commandBuffer->pushConstants(pipeline, &info);
    commandBuffer->dispatch(countX, countY, 1);
}

BloomPass::BloomPass(const rv::Context& context, uint32_t width, uint32_t height) {
    bloomImage = context.createImage({
        .usage = rv::ImageUsage::Storage,
        .extent = {width, height, 1},
        .format = vk::Format::eR32G32B32A32Sfloat,
        .viewInfo = rv::ImageViewCreateInfo{},
        .debugName = "bloomImage",
    });

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
    descSet->update();

    pipeline = context.createComputePipeline({
        .descSetLayout = descSet->getLayout(),
        .pushSize = sizeof(BloomConstants),
        .computeShader = shader,
    });
}

void BloomPass::render(const rv::CommandBufferHandle& commandBuffer,
                       uint32_t countX,
                       uint32_t countY,
                       BloomConstants info) {
    commandBuffer->bindDescriptorSet(pipeline, descSet);
    commandBuffer->bindPipeline(pipeline);
    commandBuffer->pushConstants(pipeline, &info);
    commandBuffer->dispatch(countX, countY, 1);
    commandBuffer->imageBarrier(bloomImage, vk::PipelineStageFlagBits::eComputeShader,
                                vk::PipelineStageFlagBits::eComputeShader,
                                vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);
}