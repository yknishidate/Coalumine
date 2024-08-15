#include <future>
#include <random>

#include "../shader/share.h"

#include <reactive/reactive.hpp>

#include <stb_image_write.h>

#include "render_pass.hpp"
#include "scene.hpp"

glm::vec3 colorRamp5(float value,
                     glm::vec3 color0,
                     glm::vec3 color1,
                     glm::vec3 color2,
                     glm::vec3 color3,
                     glm::vec3 color4) {
    if (value == 0.0)
        return glm::vec3(0.0);

    float knot0 = 0.0f;
    float knot1 = 0.2f;
    float knot2 = 0.4f;
    float knot3 = 0.8f;
    float knot4 = 1.0f;
    if (value < knot0) {
        return color0;
    } else if (value < knot1) {
        float t = (value - knot0) / (knot1 - knot0);
        return mix(color0, color1, t);
    } else if (value < knot2) {
        float t = (value - knot1) / (knot2 - knot1);
        return mix(color1, color2, t);
    } else if (value < knot3) {
        float t = (value - knot2) / (knot3 - knot2);
        return mix(color2, color3, t);
    } else if (value < knot4) {
        float t = (value - knot3) / (knot4 - knot3);
        return mix(color3, color4, t);
    } else {
        return color4;
    }
}

class Renderer {
public:
    Renderer(const Context& context, uint32_t width, uint32_t height)
        : width{width}, height{height} {
        rv::CPUTimer timer;
        loadMaterialTestScene(context);
        // loadRTCamp9Scene(context);
        spdlog::info("Load scene: {} ms", timer.elapsedInMilli());

        timer.restart();
        scene.buildAccels(context);
        spdlog::info("Build accels: {} ms", timer.elapsedInMilli());

        scene.initMaterialIndexBuffer(context);
        scene.initAddressBuffer(context);

        baseImage = context.createImage({
            .usage = ImageUsage::Storage,
            .extent = {width, height, 1},
            .format = vk::Format::eR32G32B32A32Sfloat,
            .debugName = "baseImage",
        });
        baseImage->createImageView();

        context.oneTimeSubmit([&](auto commandBuffer) {
            commandBuffer->transitionLayout(baseImage, vk::ImageLayout::eGeneral);
        });

        createPipelines(context);

        orbitalCamera = {Camera::Type::Orbital, width / static_cast<float>(height)};
        orbitalCamera.setDistance(12.0f);
        orbitalCamera.setFovY(glm::radians(30.0f));
        currentCamera = &orbitalCamera;

        if (scene.cameraExists) {
            fpsCamera = {Camera::Type::FirstPerson, width / static_cast<float>(height)};
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

    void loadMaterialTestScene(const Context& context) {
        // Add mesh
        Mesh sphereMesh = Mesh::createSphereMesh(  //
            context,                               //
            {
                .numSlices = 64,
                .numStacks = 64,
                .radius = 0.5,
                .usage = MeshUsage::RayTracing,
                .name = "sphereMesh",
            });
        // Add material, mesh, node
        for (int i = 0; i < 8; i++) {
            Material metal;
            metal.baseColorFactor = glm::vec4{0.9, 0.9, 0.9, 1.0};
            metal.metallicFactor = 1.0f;
            metal.roughnessFactor = i / 7.0f;
            int matIndex = scene.addMaterial(context, metal);
            int meshIndex = scene.addMesh(sphereMesh, matIndex);
            Node node;
            node.meshIndex = meshIndex;
            node.translation = glm::vec3{(i - 3.5) * 1.25, -1.5, 0.0};
            scene.addNode(node);
        }
        for (int i = 0; i < 8; i++) {
            Material metal;
            metal.baseColorFactor = glm::vec4{0.9, 0.9, 0.9, 0.0};
            metal.metallicFactor = 0.0f;
            metal.roughnessFactor = i / 7.0f;
            int matIndex = scene.addMaterial(context, metal);
            int meshIndex = scene.addMesh(sphereMesh, matIndex);
            Node node;
            node.meshIndex = meshIndex;
            node.translation = glm::vec3{(i - 3.5) * 1.25, 1.5, 0.0};
            scene.addNode(node);
        }
        scene.createNormalMatrixBuffer(context);
        scene.loadDomeLightTexture(context, getAssetDirectory() / "studio_small_03_4k.hdr");
    }

    void loadRTCamp9Scene(const Context& context) {
        scene.loadFromFile(context, getAssetDirectory() / "clean_scene_v3_180_2.gltf");

        // Add materials
        Material diffuseMaterial;
        diffuseMaterial.baseColorFactor = glm::vec4{1.0, 1.0, 1.0, 1.0};
        Material glassMaterial;
        glassMaterial.baseColorFactor = glm::vec4{1.0, 1.0, 1.0, 0.0};
        glassMaterial.roughnessFactor = 0.1f;
        Material metalMaterial;
        metalMaterial.baseColorFactor = glm::vec4{1.0, 1.0, 1.0, 1.0};
        metalMaterial.metallicFactor = 1.0f;
        metalMaterial.roughnessFactor = 0.2f;
        int diffuseMaterialIndex = scene.addMaterial(context, diffuseMaterial);
        int glassMaterialIndex = scene.addMaterial(context, glassMaterial);
        int metalMaterialIndex = scene.addMaterial(context, metalMaterial);

        // Set materials
        std::mt19937 rng(12345);
        std::uniform_real_distribution<float> dist1(0.0f, 1.0f);
        for (auto& materialIndex : scene.materialIndices) {
            if (materialIndex == -1) {
                double randVal = dist1(rng);
                if (randVal < 0.33) {
                    materialIndex = diffuseMaterialIndex;
                } else if (randVal < 0.66) {
                    materialIndex = metalMaterialIndex;
                } else {
                    materialIndex = glassMaterialIndex;
                }
            }
        }

        uint32_t textureWidth = 1000;
        uint32_t textureHeight = 100;
        uint32_t textureChannel = 4;

        std::vector<glm::vec4> data(textureWidth * textureHeight * textureChannel);
        for (uint32_t x = 0; x < textureWidth; x++) {
            glm::vec3 color = colorRamp5(x / static_cast<float>(textureWidth),         //
                                         glm::vec3(225, 245, 253) / glm::vec3(255.0),  //
                                         glm::vec3(1, 115, 233) / glm::vec3(255.0),    //
                                         glm::vec3(2, 37, 131) / glm::vec3(255.0),     //
                                         glm::vec3(0, 3, 49) / glm::vec3(255.0),       //
                                         glm::vec3(0, 0, 3) / glm::vec3(255.0));
            for (uint32_t y = 0; y < textureHeight; y++) {
                data[y * textureWidth + x] = glm::vec4(color, 0.0);
            }
        }

        scene.createDomeLightTexture(context, reinterpret_cast<float*>(data.data()),  //
                                     textureWidth, textureHeight, textureChannel);
    }

    void createPipelines(const Context& context) {
        std::vector<ShaderHandle> shaders(4);
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
                    {"VertexBuffers", scene.vertexBuffers},
                    {"IndexBuffers", scene.indexBuffers},
                    {"AddressBuffer", scene.addressBuffer},
                    {"MaterialIndexBuffer", scene.materialIndexBuffer},
                    {"NormalMatrixBuffer", scene.normalMatrixBuffer},
                },
            .images =
                {
                    {"baseImage", baseImage},
                    {"bloomImage", bloomPass.bloomImage},
                    {"domeLightTexture", scene.domeLightTexture},
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
    }

    void reset() { pushConstants.frame = 0; }

    void render(const CommandBufferHandle& commandBuffer,
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

    ImageHandle baseImage;

    DescriptorSetHandle descSet;
    RayTracingPipelineHandle rayTracingPipeline;

    Camera* currentCamera = nullptr;
    Camera fpsCamera;
    Camera orbitalCamera;

    PushConstants pushConstants;
};
