﻿#include <future>
#include <random>

#include "../shader/share.h"
#include "reactive/App.hpp"

#define NOMINMAX
#define TINYGLTF_IMPLEMENTATION
#include "reactive/common.hpp"
#include "render_pass.hpp"
#include "scene.hpp"

class Renderer {
public:
    Renderer(const Context& context, uint32_t width, uint32_t height, App* app)
        : width{width}, height{height} {
        // Output ray tracing props
        auto rtProps =
            context
                .getPhysicalDeviceProperties2<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
        maxRayRecursionDepth = rtProps.maxRayRecursionDepth;
        spdlog::info("MaxRayRecursionDepth: {}", maxRayRecursionDepth);

        rv::CPUTimer timer;
        loadMaterialTestScene(context);
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
            .layout = vk::ImageLayout::eGeneral,
            .debugName = "baseImage",
        });

        createPipelines(context);

        orbitalCamera = OrbitalCamera{app, width, height};
        // orbitalCamera.phi = 25.0f;
        // orbitalCamera.theta = 30.0f;
        orbitalCamera.distance = 12.0f;
        orbitalCamera.fovY = glm::radians(30.0f);
        currentCamera = &orbitalCamera;

        if (scene.cameraExists) {
            fpsCamera = FPSCamera{app, width, height};
            fpsCamera.position = scene.cameraTranslation;
            glm::vec3 eulerAngles = glm::eulerAngles(scene.cameraRotation);

            // TODO: fix this, if(pitch > 90) { pitch = 90 - (pitch - 90); yaw += 180; }
            fpsCamera.pitch = -glm::degrees(eulerAngles.x);
            if (glm::degrees(eulerAngles.x) < -90.0f || 90.0f < glm::degrees(eulerAngles.x)) {
                fpsCamera.pitch = -glm::degrees(eulerAngles.x) + 180;
            }
            fpsCamera.yaw = glm::mod(glm::degrees(eulerAngles.y), 360.0f);
            // fpsCamera.yaw = glm::mod(glm::degrees(-eulerAngles.y) + 180, 360.0f);
            fpsCamera.fovY = scene.cameraYFov;
            currentCamera = &fpsCamera;
        }
    }

    void loadMaterialTestScene(const Context& context) {
        // Add mesh
        MeshHandle sphereMesh = context.createSphereMesh({
            .numSlices = 64,
            .numStacks = 64,
            .radius = 0.5,
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

    void loadCleanScene(const Context& context) {
        scene.loadFromFile(context, getAssetDirectory() / "clean_scene_v3_180_2.gltf");

        // Add materials
        Material diffuseMaterial;
        diffuseMaterial.baseColorFactor = glm::vec4{1.0, 1.0, 1.0, 1.0};
        Material glassMaterial;
        glassMaterial.baseColorFactor = glm::vec4{1.0, 1.0, 1.0, 0.0};
        glassMaterial.roughnessFactor = 0.1;
        Material metalMaterial;
        metalMaterial.baseColorFactor = glm::vec4{1.0, 1.0, 1.0, 1.0};
        metalMaterial.metallicFactor = 1.0;
        metalMaterial.roughnessFactor = 0.2;
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

        rayTracingPipeline = context.createRayTracingPipeline({
            .rgenShaders = shaders[0],
            .missShaders = {shaders, 1, 2},
            .chitShaders = shaders[3],
            .descSetLayout = descSet->getLayout(),
            .pushSize = sizeof(PushConstants),
            .maxRayRecursionDepth = maxRayRecursionDepth,
        });
    }

    void update() {
        RV_ASSERT(currentCamera, "currentCamera is nullptr");
        currentCamera->processInput();
        pushConstants.frame++;
        pushConstants.invView = currentCamera->getInvView();
        pushConstants.invProj = currentCamera->getInvProj();
    }

    void reset() { pushConstants.frame = 0; }

    void render(const CommandBuffer& commandBuffer,
                bool playAnimation,
                bool enableBloom,
                int blurIteration) {
        // Update
        // if (!playAnimation || !scene.shouldUpdate(pushConstants.frame)) {
        if (!playAnimation) {
            spdlog::info("Skipped: {}", pushConstants.frame);
            return;
        }
        scene.updateTopAccel(commandBuffer.commandBuffer, pushConstants.frame);

        // Ray tracing
        commandBuffer.bindDescriptorSet(descSet, rayTracingPipeline);
        commandBuffer.bindPipeline(rayTracingPipeline);
        commandBuffer.pushConstants(rayTracingPipeline, &pushConstants);
        commandBuffer.traceRays(rayTracingPipeline, width, height, 1);

        commandBuffer.imageBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                   vk::PipelineStageFlagBits::eComputeShader, {}, baseImage,
                                   vk::AccessFlagBits::eShaderWrite,
                                   vk::AccessFlagBits::eShaderRead);
        commandBuffer.imageBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                   vk::PipelineStageFlagBits::eComputeShader, {},
                                   bloomPass.bloomImage, vk::AccessFlagBits::eShaderWrite,
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
    uint32_t maxRayRecursionDepth;

    Scene scene;

    CompositeInfo compositeInfo;
    CompositePass compositePass;
    BloomInfo bloomInfo;
    BloomPass bloomPass;

    ImageHandle baseImage;

    DescriptorSetHandle descSet;
    RayTracingPipelineHandle rayTracingPipeline;

    Camera* currentCamera = nullptr;
    FPSCamera fpsCamera;
    OrbitalCamera orbitalCamera;

    PushConstants pushConstants;
};

class DebugRenderer : public App {
public:
    DebugRenderer()
        : App({
              .width = 1920,
              .height = 1080,
              .title = "rtcamp9",
              .layers = Layer::Validation,
              .extensions = Extension::RayTracing,
          }) {
        spdlog::info("Executable directory: {}", getExecutableDirectory().string());
        spdlog::info("Shader source directory: {}", getShaderSourceDirectory().string());
        spdlog::info("SPIR-V directory: {}", getSpvDirectory().string());
        fs::create_directory(getSpvDirectory());

        if (shouldRecompile("base.rgen", "main")) {
            compileShader("base.rgen", "main");
        }
        if (shouldRecompile("base.rchit", "main")) {
            compileShader("base.rchit", "main");
        }
        if (shouldRecompile("base.rmiss", "main")) {
            compileShader("base.rmiss", "main");
        }
        if (shouldRecompile("shadow.rmiss", "main")) {
            compileShader("shadow.rmiss", "main");
        }
        if (shouldRecompile("blur.comp", "main")) {
            compileShader("blur.comp", "main");
        }
        if (shouldRecompile("composite.comp", "main")) {
            compileShader("composite.comp", "main");
        }

        renderer = std::make_unique<Renderer>(context, width, height, this);
    }

    void onStart() override {
        gpuTimer = context.createGPUTimer({});
        imageSavingBuffer = context.createBuffer({
            .usage = BufferUsage::Staging,
            .memory = MemoryUsage::Host,
            .size = width * height * 4 * sizeof(uint8_t),
            .debugName = "imageSavingBuffer",
        });
    }

    void onUpdate() override { renderer->update(); }

    void recreatePipelinesIfShadersWereUpdated() const {
        bool shouldRecreate = false;
        try {
            if (shouldRecompile("base.rgen", "main")) {
                compileShader("base.rgen", "main");
                shouldRecreate = true;
            }
            if (shouldRecompile("base.rchit", "main")) {
                compileShader("base.rchit", "main");
                shouldRecreate = true;
            }
            if (shouldRecompile("base.rmiss", "main")) {
                compileShader("base.rmiss", "main");
                shouldRecreate = true;
            }
            if (shouldRecompile("shadow.rmiss", "main")) {
                compileShader("shadow.rmiss", "main");
                shouldRecreate = true;
            }
        } catch (const std::exception& e) {
            spdlog::error(e.what());
        }
        if (shouldRecreate) {
            while (true) {
                try {
                    renderer->createPipelines(context);
                    renderer->reset();
                    return;
                } catch (const std::exception& e) {
                    spdlog::error(e.what());
                }
            }
        }
    }

    void onRender(const CommandBuffer& commandBuffer) override {
        static int imageIndex = 0;
        static bool enableBloom = false;
        static int blurIteration = 32;
        static bool playAnimation = true;
        static bool open = true;
        if (open) {
            ImGui::Begin("Settings", &open);
            ImGui::Combo("Image", &imageIndex, "Render\0Bloom");
            ImGui::SliderInt("Sample count", &renderer->pushConstants.sampleCount, 1, 512);
            if (ImGui::Button("Save image")) {
                saveImage();
            }

            // Dome light
            if (ImGui::SliderFloat("Dome light phi", &renderer->pushConstants.domeLightPhi, 0.0,
                                   360.0)) {
                renderer->reset();
            }

            // Infinite light
            static glm::vec4 defaultInfiniteLightDirection =
                renderer->pushConstants.infiniteLightDirection;
            ImGui::SliderFloat4(
                "Infinite light direction",
                reinterpret_cast<float*>(&renderer->pushConstants.infiniteLightDirection), -1.0,
                1.0);
            ImGui::SliderFloat("Infinite light intensity",
                               &renderer->pushConstants.infiniteLightIntensity, 0.0f, 1.0f);
            if (ImGui::Button("Reset infinite light")) {
                renderer->pushConstants.infiniteLightDirection = defaultInfiniteLightDirection;
            }

            // Bloom
            ImGui::Checkbox("Enable bloom", &enableBloom);
            if (enableBloom) {
                ImGui::SliderFloat("Bloom intensity", &renderer->compositeInfo.bloomIntensity, 0.0,
                                   10.0);
                ImGui::SliderFloat("Bloom threshold", &renderer->pushConstants.bloomThreshold, 0.0,
                                   10.0);
                ImGui::SliderInt("Blur iteration", &blurIteration, 0, 64);
                ImGui::SliderInt("Blur size", &renderer->bloomInfo.blurSize, 0, 64);
            }

            // Tone mapping
            ImGui::Checkbox("Enable tone mapping",
                            reinterpret_cast<bool*>(&renderer->compositeInfo.enableToneMapping));
            if (renderer->compositeInfo.enableToneMapping) {
                ImGui::SliderFloat("Exposure", &renderer->compositeInfo.exposure, 0.0, 5.0);
            }

            // Gamma correction
            ImGui::Checkbox(
                "Enable gamma correction",
                reinterpret_cast<bool*>(&renderer->compositeInfo.enableGammaCorrection));
            if (renderer->compositeInfo.enableGammaCorrection) {
                ImGui::SliderFloat("Gamma", &renderer->compositeInfo.gamma, 0.0, 5.0);
            }

            ImGui::Checkbox("Play animation", &playAnimation);

            // Show GPU time
            if (renderer->pushConstants.frame > 1) {
                ImGui::Text("GPU time: %f ms", gpuTimer->elapsedInMilli());
            }
            ImGui::End();
        }

        // Check shader files
        recreatePipelinesIfShadersWereUpdated();

        commandBuffer.beginTimestamp(gpuTimer);
        renderer->render(commandBuffer, playAnimation, enableBloom, blurIteration);
        commandBuffer.endTimestamp(gpuTimer);

        // Copy to swapchain image
        commandBuffer.copyImage(renderer->compositePass.finalImageBGRA, getCurrentColorImage(),
                                vk::ImageLayout::eGeneral, vk::ImageLayout::ePresentSrcKHR);

        // Copy to buffer
        ImageHandle outputImage = renderer->compositePass.finalImageRGBA;
        commandBuffer.transitionLayout(outputImage, vk::ImageLayout::eTransferSrcOptimal);
        commandBuffer.copyImageToBuffer(outputImage, imageSavingBuffer);
        commandBuffer.transitionLayout(outputImage, vk::ImageLayout::eGeneral);
    }

    void saveImage() {
        auto* pixels = static_cast<uint8_t*>(imageSavingBuffer->map());
        std::string frame = std::to_string(renderer->pushConstants.frame);
        std::string zeros = std::string(std::max(0, 3 - static_cast<int>(frame.size())), '0');
        std::string img = zeros + frame + ".jpg";
        writeTask = std::async(std::launch::async, [=]() {
            stbi_write_jpg(img.c_str(), width, height, 4, pixels, 90);
        });
    }

    std::unique_ptr<Renderer> renderer;
    GPUTimerHandle gpuTimer;
    BufferHandle imageSavingBuffer;
    std::future<void> writeTask;
};

int main() {
    try {
        DebugRenderer debugRenderer{};
        debugRenderer.run();
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}
