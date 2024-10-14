﻿#pragma once
#include <stb_image_write.h>
#include <future>
#include <random>
#include <reactive/reactive.hpp>

#include "../shader/share.h"
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
        m_scene.initialize(context, scenePath, width, height);

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
        m_pushConstants.useEnvLightTexture = m_scene.m_useEnvLightTexture;
        m_pushConstants.envLightColor = {m_scene.m_envLightColor, 1.0f};
        m_pushConstants.envLightIntensity = m_scene.m_envLightIntensity;
        m_pushConstants.isEnvLightTextureVisible =
            static_cast<int>(m_scene.m_isEnvLightTextureVisible);

        // Infinite light
        m_pushConstants.infiniteLightColor.xyz = m_scene.m_infiniteLightColor;
        m_pushConstants.infiniteLightDirection = m_scene.m_infiniteLightDir;
        m_pushConstants.infiniteLightIntensity = m_scene.m_infiniteLightIntensity;
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
                    {"NodeDataBuffer", m_scene.m_nodeDataBuffer},
                    {"MaterialBuffer", m_scene.m_materialBuffer},
                },
            .images =
                {
                    {"baseImage", m_baseImage},
                    {"bloomImage", m_bloomPass.getOutputImage()},
                    {"envLightTexture", m_scene.m_envLightTexture},
                    {"textures2d", m_scene.m_textures2d},
                    {"textures3d", m_scene.m_textures3d},
                },
            .accels = {{"topLevelAS", m_scene.m_topAccel}},
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
            m_scene.m_camera.processMouseDragLeft(dragLeft);
            m_scene.m_camera.processMouseScroll(scroll);
        }

        m_pushConstants.cameraForward = glm::vec4(m_scene.m_camera.getFront(), 1.0f);
        m_pushConstants.cameraPos = glm::vec4(m_scene.m_camera.getPosition(), 1.0f);
        m_pushConstants.cameraRight = glm::vec4(m_scene.m_camera.getRight(), 1.0f);
        m_pushConstants.cameraUp = glm::vec4(m_scene.m_camera.getUp(), 1.0f);
        m_pushConstants.cameraImageDistance = m_scene.m_camera.getImageDistance();
        m_pushConstants.cameraLensRadius = m_scene.m_camera.m_lensRadius;
        m_pushConstants.cameraObjectDistance = m_scene.m_camera.m_objectDistance;
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

            for (int i = 0; i < m_scene.m_meshes.size(); i++) {
                if (m_scene.m_meshes[i].hasAnimation()) {
                    commandBuffer->updateBottomAccel(m_scene.m_bottomAccels[i]);
                }
            }

            commandBuffer->memoryBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                                         vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                                         vk::AccessFlagBits::eAccelerationStructureWriteKHR,
                                         vk::AccessFlagBits::eAccelerationStructureReadKHR);

            commandBuffer->updateTopAccel(m_scene.m_topAccel);

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
