#pragma once
#include <future>
#include <random>

#include "../shader/share.h"

#include <reactive/reactive.hpp>

#include <stb_image_write.h>

#include "image_generator.hpp"
#include "render_pass.hpp"
#include "scene.hpp"

class Renderer {
public:
    Renderer(const rv::Context& context,
             uint32_t width,
             uint32_t height,
             const std::filesystem::path& scenePath)
        : m_width{width}, m_height{height} {
        // Load scene
        rv::CPUTimer timer;
        m_scene.loadFromFile(context, scenePath);
        m_scene.createMaterialBuffer(context);
        m_scene.createNodeDataBuffer(context);
        m_scene.createDummyTextures(context);
        m_scene.camera.setAspect(width / static_cast<float>(height));
        spdlog::info("Load scene: {} ms", timer.elapsedInMilli());

        // Build BVH
        timer.restart();
        m_scene.buildAccels(context);
        spdlog::info("Build accels: {} ms", timer.elapsedInMilli());

        m_baseImage = context.createImage({
            .usage = rv::ImageUsage::Storage,
            .extent = {width, height, 1},
            .format = vk::Format::eR32G32B32A32Sfloat,
            .viewInfo = rv::ImageViewCreateInfo{},
            .debugName = "baseImage",
        });

        context.oneTimeSubmit([&](auto commandBuffer) {
            commandBuffer->transitionLayout(m_baseImage, vk::ImageLayout::eGeneral);
        });

        createPipelines(context);

        // Env light
        m_pushConstants.useEnvLightTexture = m_scene.useEnvLightTexture;
        m_pushConstants.envLightColor = {m_scene.envLightColor, 1.0f};
        m_pushConstants.envLightIntensity = m_scene.envLightIntensity;
        m_pushConstants.visibleEnvLightTexture = static_cast<int>(m_scene.visibleEnvLightTexture);

        // Infinite light
        m_pushConstants.infiniteLightColor.xyz = m_scene.infiniteLightColor;
        m_pushConstants.infiniteLightDirection = m_scene.infiniteLightDir;
        m_pushConstants.infiniteLightIntensity = m_scene.infiniteLightIntensity;
    }

    void createPipelines(const rv::Context& context) {
        std::vector<rv::ShaderHandle> shaders(4);
        shaders[0] = context.createShader({
            .code = readShader("base.rgen", "main"),
            .stage = vk::ShaderStageFlagBits::eRaygenKHR,
        });
        shaders[1] = context.createShader({
            .code = readShader("base.rmiss", "main"),
            .stage = vk::ShaderStageFlagBits::eMissKHR,
        });
        shaders[2] = context.createShader({
            .code = readShader("shadow.rmiss", "main"),
            .stage = vk::ShaderStageFlagBits::eMissKHR,
        });
        shaders[3] = context.createShader({
            .code = readShader("base.rchit", "main"),
            .stage = vk::ShaderStageFlagBits::eClosestHitKHR,
        });

        m_bloomPass = {context, m_width, m_height};
        m_compositePass = {context, m_baseImage, m_bloomPass.getOutputImage(), m_width, m_height};

        m_descSet = context.createDescriptorSet({
            .shaders = shaders,
            .buffers =
                {
                    {"NodeDataBuffer", m_scene.nodeDataBuffer},
                    {"MaterialBuffer", m_scene.materialBuffer},
                },
            .images =
                {
                    {"baseImage", m_baseImage},
                    {"bloomImage", m_bloomPass.getOutputImage()},
                    {"envLightTexture", m_scene.envLightTexture},
                    {"textures2d", m_scene.textures2d},
                    {"textures3d", m_scene.textures3d},
                },
            .accels = {{"topLevelAS", m_scene.topAccel}},
        });
        m_descSet->update();

        m_rayTracingPipeline = context.createRayTracingPipeline({
            .rgenGroup = {shaders[0]},
            .missGroups = {{shaders[1]}, {shaders[2]}},
            .hitGroups = {{shaders[3]}},
            .descSetLayout = m_descSet->getLayout(),
            .pushSize = sizeof(RayTracingConstants),
            .maxRayRecursionDepth = 31,
        });
    }

    void update(glm::vec2 dragLeft, float scroll) {
        if (dragLeft != glm::vec2(0.0f) || scroll != 0.0f) {
            m_scene.camera.processMouseDragLeft(dragLeft);
            m_scene.camera.processMouseScroll(scroll);
        }

        m_pushConstants.cameraForward = glm::vec4(m_scene.camera.getFront(), 1.0f);
        m_pushConstants.cameraPos = glm::vec4(m_scene.camera.getPosition(), 1.0f);
        m_pushConstants.cameraRight = glm::vec4(m_scene.camera.getRight(), 1.0f);
        m_pushConstants.cameraUp = glm::vec4(m_scene.camera.getUp(), 1.0f);
        m_pushConstants.cameraImageDistance = m_scene.camera.getImageDistance();
        m_pushConstants.cameraLensRadius = m_scene.camera.m_lensRadius;
        m_pushConstants.cameraObjectDistance = m_scene.camera.m_objectDistance;
    }

    void reset() { m_pushConstants.accumCount = 0; }

    void render(const rv::CommandBufferHandle& commandBuffer,
                int frame,
                bool enableBloom,
                int blurIteration) {
        // Update
        m_scene.updateMaterialBuffer(commandBuffer);

        if (m_lastFrame != frame) {
            m_scene.updateBottomAccel(frame);
            m_scene.updateTopAccel(frame);

            for (int i = 0; i < m_scene.meshes.size(); i++) {
                if (m_scene.meshes[i].hasAnimation()) {
                    commandBuffer->updateBottomAccel(m_scene.bottomAccels[i]);
                }
            }

            commandBuffer->memoryBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                                         vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                                         vk::AccessFlagBits::eAccelerationStructureWriteKHR,
                                         vk::AccessFlagBits::eAccelerationStructureReadKHR);

            commandBuffer->updateTopAccel(m_scene.topAccel);

            commandBuffer->memoryBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                                         vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                         vk::AccessFlagBits::eAccelerationStructureWriteKHR,
                                         vk::AccessFlagBits::eAccelerationStructureReadKHR);

            m_lastFrame = frame;
        }

        // Ray tracing
        commandBuffer->bindDescriptorSet(m_rayTracingPipeline, m_descSet);
        commandBuffer->bindPipeline(m_rayTracingPipeline);
        commandBuffer->pushConstants(m_rayTracingPipeline, &m_pushConstants);
        commandBuffer->traceRays(m_rayTracingPipeline, m_width, m_height, 1);

        commandBuffer->imageBarrier(m_baseImage,  //
                                    vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                    vk::PipelineStageFlagBits::eComputeShader,
                                    vk::AccessFlagBits::eShaderWrite,
                                    vk::AccessFlagBits::eShaderRead);
        commandBuffer->imageBarrier(m_bloomPass.getOutputImage(),  //
                                    vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                    vk::PipelineStageFlagBits::eComputeShader,
                                    vk::AccessFlagBits::eShaderWrite,
                                    vk::AccessFlagBits::eShaderRead);

        // Blur
        if (enableBloom) {
            for (int i = 0; i < blurIteration; i++) {
                m_bloomPass.render(commandBuffer, m_width / 8, m_height / 8, m_bloomInfo);
            }
        }

        m_compositePass.render(commandBuffer, m_width / 8, m_height / 8, m_compositeInfo);

        if (m_pushConstants.enableAccum) {
            m_pushConstants.accumCount++;
        }
    }

    uint32_t m_width;
    uint32_t m_height;

    Scene m_scene;

    CompositeConstants m_compositeInfo;
    CompositePass m_compositePass;
    BloomConstants m_bloomInfo;
    BloomPass m_bloomPass;

    rv::ImageHandle m_baseImage;

    rv::DescriptorSetHandle m_descSet;
    rv::RayTracingPipelineHandle m_rayTracingPipeline;

    RayTracingConstants m_pushConstants;

    int m_lastFrame = 0;
};
