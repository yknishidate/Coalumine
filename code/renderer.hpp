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
        spdlog::info("Load scene: {} ms", timer.elapsedInMilli());

        // Build BVH
        timer.restart();
        m_scene.buildAccels(context);
        spdlog::info("Build accels: {} ms", timer.elapsedInMilli());

        m_baseImage = context.createImage({
            .usage = rv::ImageUsage::Storage,
            .extent = {width, height, 1},
            .format = vk::Format::eR32G32B32A32Sfloat,
            .debugName = "baseImage",
        });
        m_baseImage->createImageView();

        context.oneTimeSubmit([&](auto commandBuffer) {
            commandBuffer->transitionLayout(m_baseImage, vk::ImageLayout::eGeneral);
        });

        createPipelines(context);

        m_orbitalCamera = {rv::Camera::Type::Orbital, width / static_cast<float>(height)};
        m_orbitalCamera.setDistance(12.0f);
        m_orbitalCamera.setFovY(glm::radians(30.0f));
        m_currentCamera = &m_orbitalCamera;

        if (m_scene.cameraExists) {
            m_fpsCamera = {rv::Camera::Type::FirstPerson, width / static_cast<float>(height)};
            m_fpsCamera.setPosition(m_scene.cameraTranslation);
            m_fpsCamera.setEulerRotation(glm::eulerAngles(m_scene.cameraRotation));
            m_fpsCamera.setFovY(m_scene.cameraYFov);
            m_currentCamera = &m_fpsCamera;
        }

        // Env light
        m_pushConstants.useEnvLightTexture = m_scene.useEnvLightTexture;
        m_pushConstants.envLightColor = {m_scene.envLightColor, 1.0f};

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

        m_bloomPass = BloomPass(context, m_width, m_height);
        m_compositePass =
            CompositePass(context, m_baseImage, m_bloomPass.bloomImage, m_width, m_height);

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
                    {"bloomImage", m_bloomPass.bloomImage},
                    {"envLightTexture", m_scene.envLightTexture},
                },
            .accels = {{"topLevelAS", m_scene.topAccel}},
        });
        m_descSet->update();

        m_rayTracingPipeline = context.createRayTracingPipeline({
            .rgenGroup = {shaders[0]},
            .missGroups = {{shaders[1]}, {shaders[2]}},
            .hitGroups = {{shaders[3]}},
            .descSetLayout = m_descSet->getLayout(),
            .pushSize = sizeof(PushConstants),
            .maxRayRecursionDepth = 31,
        });
    }

    void update(glm::vec2 dragLeft, float scroll) {
        assert(m_currentCamera && "m_currentCamera is nullptr");

        if (dragLeft != glm::vec2(0.0f) || scroll != 0.0f) {
            m_currentCamera->processMouseDragLeft(dragLeft);
            m_currentCamera->processMouseScroll(scroll);

            m_pushConstants.frame = 0;
        } else {
            m_pushConstants.frame++;
        }

        m_pushConstants.invView = m_currentCamera->getInvView();
        m_pushConstants.invProj = m_currentCamera->getInvProj();
    }

    void reset() { m_pushConstants.frame = 0; }

    void render(const rv::CommandBufferHandle& commandBuffer,
                bool playAnimation,
                bool enableBloom,
                int blurIteration) {
        // Update
        // if (!playAnimation || !scene.shouldUpdate(pushConstants.frame)) {
        if (!playAnimation) {
            spdlog::info("Skipped: {}", m_pushConstants.frame);
            return;
        }
        m_scene.updateTopAccel(m_pushConstants.frame);

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
        commandBuffer->imageBarrier(m_bloomPass.bloomImage,  //
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
    }

    uint32_t m_width;
    uint32_t m_height;

    Scene m_scene;

    CompositeInfo m_compositeInfo;
    CompositePass m_compositePass;
    BloomInfo m_bloomInfo;
    BloomPass m_bloomPass;

    rv::ImageHandle m_baseImage;

    rv::DescriptorSetHandle m_descSet;
    rv::RayTracingPipelineHandle m_rayTracingPipeline;

    rv::Camera* m_currentCamera = nullptr;
    rv::Camera m_fpsCamera;
    rv::Camera m_orbitalCamera;

    PushConstants m_pushConstants;
};
