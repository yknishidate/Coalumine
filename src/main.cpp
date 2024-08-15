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

    void update() {
        assert(currentCamera && "currentCamera is nullptr");

        auto dragLeft = Window::getMouseDragLeft();
        auto scroll = Window::getMouseScroll();
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

class WindowApp : public App {
public:
    WindowApp()
        : App({
              .width = 1920,
              .height = 1080,
              .title = "Coalumine",
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

        renderer = std::make_unique<Renderer>(context, Window::getWidth(), Window::getHeight());
    }

    void onStart() override {
        gpuTimer = context.createGPUTimer({});
        imageSavingBuffer = context.createBuffer({
            .usage = BufferUsage::Staging,
            .memory = MemoryUsage::Host,
            .size = Window::getWidth() * Window::getHeight() * 4 * sizeof(uint8_t),
            .debugName = "imageSavingBuffer",
        });
    }

    void onUpdate(float dt) override {  //
        renderer->update();
    }

    void recompile() const {
        try {
            compileShader("base.rgen", "main");
            compileShader("base.rchit", "main");
            compileShader("base.rmiss", "main");
            compileShader("shadow.rmiss", "main");
            renderer->createPipelines(context);
            renderer->reset();
        } catch (const std::exception& e) {
            spdlog::error(e.what());
        }
    }

    void onRender(const CommandBufferHandle& commandBuffer) override {
        static int imageIndex = 0;
        static bool enableBloom = false;
        static bool enableAccum = renderer->pushConstants.enableAccum;
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

            if (ImGui::Checkbox("Enable accum", &enableAccum)) {
                renderer->pushConstants.enableAccum = enableAccum;
                renderer->reset();
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

            if (ImGui::Button("Recompile")) {
                recompile();
            }

            ImGui::End();
        }

        commandBuffer->beginTimestamp(gpuTimer);
        renderer->render(commandBuffer, playAnimation, enableBloom, blurIteration);
        commandBuffer->endTimestamp(gpuTimer);

        // Copy to swapchain image
        commandBuffer->copyImage(renderer->compositePass.finalImageBGRA, getCurrentColorImage(),
                                 vk::ImageLayout::eGeneral, vk::ImageLayout::ePresentSrcKHR);

        // Copy to buffer
        ImageHandle outputImage = renderer->compositePass.finalImageRGBA;
        commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eTransferSrcOptimal);
        commandBuffer->copyImageToBuffer(outputImage, imageSavingBuffer);
        commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eGeneral);
    }

    void saveImage() {
        auto* pixels = static_cast<uint8_t*>(imageSavingBuffer->map());
        std::string frame = std::to_string(renderer->pushConstants.frame);
        std::string zeros = std::string(std::max(0, 3 - static_cast<int>(frame.size())), '0');
        std::string img = zeros + frame + ".jpg";
        writeTask = std::async(std::launch::async, [=]() {
            stbi_write_jpg(img.c_str(), Window::getWidth(), Window::getHeight(), 4, pixels, 90);
        });
    }

    std::unique_ptr<Renderer> renderer;
    GPUTimerHandle gpuTimer;
    BufferHandle imageSavingBuffer;
    std::future<void> writeTask;
};

int main() {
    try {
        WindowApp app{};
        app.run();
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}
