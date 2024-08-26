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
        : width{width}, height{height} {
        // Load scene
        rv::CPUTimer timer;
        scene.loadFromFile(context, scenePath);
        scene.createMaterialBuffer(context);
        scene.createNodeDataBuffer(context);
        spdlog::info("Load scene: {} ms", timer.elapsedInMilli());

        // Build BVH
        timer.restart();
        scene.buildAccels(context);
        spdlog::info("Build accels: {} ms", timer.elapsedInMilli());

        baseImage = context.createImage({
            .usage = rv::ImageUsage::Storage,
            .extent = {width, height, 1},
            .format = vk::Format::eR32G32B32A32Sfloat,
            .debugName = "baseImage",
        });
        baseImage->createImageView();

        context.oneTimeSubmit([&](auto commandBuffer) {
            commandBuffer->transitionLayout(baseImage, vk::ImageLayout::eGeneral);
        });

        createPipelines(context);

        orbitalCamera = {rv::Camera::Type::Orbital, width / static_cast<float>(height)};
        orbitalCamera.setDistance(12.0f);
        orbitalCamera.setFovY(glm::radians(30.0f));
        currentCamera = &orbitalCamera;

        if (scene.cameraExists) {
            fpsCamera = {rv::Camera::Type::FirstPerson, width / static_cast<float>(height)};
            fpsCamera.setPosition(scene.cameraTranslation);
            glm::vec3 eulerAngles = glm::eulerAngles(scene.cameraRotation);

            // TODO: fix this, if(pitch > 90) { pitch = 90 - (pitch - 90); yaw += 180; }
            fpsCamera.setPitch(-glm::degrees(eulerAngles.x));
            if (glm::degrees(eulerAngles.x) < -90.0f || 90.0f < glm::degrees(eulerAngles.x)) {
                fpsCamera.setPitch(-glm::degrees(eulerAngles.x) + 180);
            }
            fpsCamera.setYaw(glm::mod(glm::degrees(eulerAngles.y), 360.0f));
            fpsCamera.setFovY(scene.cameraYFov);
            currentCamera = &fpsCamera;
        }
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

        bloomPass = BloomPass(context, width, height);
        compositePass = CompositePass(context, baseImage, bloomPass.bloomImage, width, height);

        descSet = context.createDescriptorSet({
            .shaders = shaders,
            .buffers =
                {
                    {"NodeDataBuffer", scene.nodeDataBuffer},
                    {"MaterialBuffer", scene.materialBuffer},
                },
            .images =
                {
                    {"baseImage", baseImage},
                    {"bloomImage", bloomPass.bloomImage},
                    {"envLightTexture", scene.envLightTexture},
                },
            .accels = {{"topLevelAS", scene.topAccel}},
        });
        descSet->update();

        rayTracingPipeline = context.createRayTracingPipeline({
            .rgenGroup = {shaders[0]},
            .missGroups = {{shaders[1]}, {shaders[2]}},
            .hitGroups = {{shaders[3]}},
            .descSetLayout = descSet->getLayout(),
            .pushSize = sizeof(PushConstants),
            .maxRayRecursionDepth = 31,
        });
    }

    void update(glm::vec2 dragLeft, float scroll) {
        assert(currentCamera && "currentCamera is nullptr");

        if (dragLeft != glm::vec2(0.0f) || scroll != 0.0f) {
            currentCamera->processMouseDragLeft(dragLeft);
            currentCamera->processMouseScroll(scroll);

            pushConstants.frame = 0;
        } else {
            pushConstants.frame++;
        }

        pushConstants.invView = currentCamera->getInvView();
        pushConstants.invProj = currentCamera->getInvProj();

        // Env light
        pushConstants.useEnvLightTexture = scene.useEnvLightTexture;
        pushConstants.envLightColor = {scene.envLightColor, 1.0f};

        // Infinite light
        pushConstants.infiniteLightColor.xyz = scene.infiniteLightColor;
        pushConstants.infiniteLightDirection = scene.infiniteLightDir;
        pushConstants.infiniteLightIntensity = scene.infiniteLightIntensity;
    }

    void reset() { pushConstants.frame = 0; }

    void render(const rv::CommandBufferHandle& commandBuffer,
                bool playAnimation,
                bool enableBloom,
                int blurIteration) {
        // Update
        // if (!playAnimation || !scene.shouldUpdate(pushConstants.frame)) {
        if (!playAnimation) {
            spdlog::info("Skipped: {}", pushConstants.frame);
            return;
        }
        scene.updateTopAccel(pushConstants.frame);

        // Ray tracing
        commandBuffer->bindDescriptorSet(rayTracingPipeline, descSet);
        commandBuffer->bindPipeline(rayTracingPipeline);
        commandBuffer->pushConstants(rayTracingPipeline, &pushConstants);
        commandBuffer->traceRays(rayTracingPipeline, width, height, 1);

        commandBuffer->imageBarrier(baseImage,  //
                                    vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                    vk::PipelineStageFlagBits::eComputeShader,
                                    vk::AccessFlagBits::eShaderWrite,
                                    vk::AccessFlagBits::eShaderRead);
        commandBuffer->imageBarrier(bloomPass.bloomImage,  //
                                    vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                    vk::PipelineStageFlagBits::eComputeShader,
                                    vk::AccessFlagBits::eShaderWrite,
                                    vk::AccessFlagBits::eShaderRead);

        // Blur
        if (enableBloom) {
            for (int i = 0; i < blurIteration; i++) {
                bloomPass.render(commandBuffer, width / 8, height / 8, bloomInfo);
            }
        }

        compositePass.render(commandBuffer, width / 8, height / 8, compositeInfo);
    }

    uint32_t width;
    uint32_t height;

    Scene scene;

    CompositeInfo compositeInfo;
    CompositePass compositePass;
    BloomInfo bloomInfo;
    BloomPass bloomPass;

    rv::ImageHandle baseImage;

    rv::DescriptorSetHandle descSet;
    rv::RayTracingPipelineHandle rayTracingPipeline;

    rv::Camera* currentCamera = nullptr;
    rv::Camera fpsCamera;
    rv::Camera orbitalCamera;

    PushConstants pushConstants;
};
