#include <future>
#include "../shader/share.h"
#include "App.hpp"

#define NOMINMAX
#define TINYGLTF_IMPLEMENTATION
#include "common.hpp"
#include "render_pass.hpp"
#include "scene.hpp"

class HelloApp : public App {
public:
    HelloApp()
        : App({
              .width = 1920,
              .height = 1080,
              .title = "rtcamp9",
              .enableValidation = true,
              .enableRayTracing = true,
          }) {}

    void createPipelines() {
        bloomPass = BloomPass(context, width, height);
        compositePass = CompositePass(context, baseImage, bloomPass.bloomImage, width, height);

        std::vector<Shader> shaders(3);
        shaders[0] = context.createShader({
            .code = compileShader("base.rgen", "main"),
            .stage = vk::ShaderStageFlagBits::eRaygenKHR,
        });
        shaders[1] = context.createShader({
            .code = compileShader("base.rmiss", "main"),
            .stage = vk::ShaderStageFlagBits::eMissKHR,
        });
        shaders[2] = context.createShader({
            .code = compileShader("base.rchit", "main"),
            .stage = vk::ShaderStageFlagBits::eClosestHitKHR,
        });

        descSet = context.createDescriptorSet({
            .shaders = shaders,
            .buffers =
                {
                    {"VertexBuffers", scene.vertexBuffers},
                    {"IndexBuffers", scene.indexBuffers},
                    {"AddressBuffer", scene.addressBuffer},
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
            .rgenShader = shaders[0],
            .missShader = shaders[1],
            .chitShader = shaders[2],
            .descSetLayout = descSet.getLayout(),
            .pushSize = sizeof(PushConstants),
            .maxRayRecursionDepth = maxRayRecursionDepth,
        });
    }

    void onStart() override {
        spdlog::info("Executable directory: {}", getExecutableDirectory().string());
        spdlog::info("Shader source directory: {}", getShaderSourceDirectory().string());
        spdlog::info("SPIR-V directory: {}", getSpvDirectory().string());
        fs::create_directory(getSpvDirectory());

        // Output ray tracing props
        auto rtProps =
            context
                .getPhysicalDeviceProperties2<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
        maxRayRecursionDepth = rtProps.maxRayRecursionDepth;
        spdlog::info("RayTracingPipelineProperties::maxRayRecursionDepth: {}",
                     maxRayRecursionDepth);

        scene.loadFromFile(context);
        scene.buildAccels(context);
        scene.loadDomeLightTexture(context);

        baseImage = context.createImage({
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eTransferSrc,
            .initialLayout = vk::ImageLayout::eGeneral,
            .aspect = vk::ImageAspectFlagBits::eColor,
            .width = width,
            .height = height,
            .format = vk::Format::eR32G32B32A32Sfloat,
        });

        createPipelines();

        orbitalCamera = OrbitalCamera{this, width, height};
        currentCamera = &orbitalCamera;
        if (scene.cameraExists) {
            fpsCamera = FPSCamera{this, width, height};
            fpsCamera.position = scene.cameraTranslation;
            glm::vec3 eulerAngles = glm::eulerAngles(scene.cameraRotation);
            spdlog::info("glTF Camera: pitch={}, yaw={}", glm::degrees(eulerAngles.x),
                         glm::degrees(eulerAngles.y));

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

        gpuTimer = context.createGPUTimer({});

        imageSavingBuffer = context.createHostBuffer({
            .usage = BufferUsage::Staging,
            .size = width * height * 4 * sizeof(uint8_t),
        });
    }

    void onUpdate() override {
        currentCamera->processInput();
        pushConstants.frame++;
        pushConstants.invView = currentCamera->getInvView();
        pushConstants.invProj = currentCamera->getInvProj();
    }

    void recreatePipelinesIfShadersWereUpdated() {
        bool shouldRecreate = false;
        shouldRecreate |= shouldRecompile("base.rgen", "main");
        shouldRecreate |= shouldRecompile("base.rchit", "main");
        shouldRecreate |= shouldRecompile("base.rmiss", "main");
        if (shouldRecreate) {
            try {
                createPipelines();
            } catch (const std::exception& e) {
                spdlog::error(e.what());
            }
        }
    }

    void onRender(const CommandBuffer& commandBuffer) override {
        static int imageIndex = 0;
        static bool enableBloom = false;
        static int blurIteration = 32;
        ImGui::Combo("Image", &imageIndex, "Render\0Bloom");

        // Bloom
        ImGui::Checkbox("Enable bloom", &enableBloom);
        if (enableBloom) {
            ImGui::SliderFloat("Bloom intensity", &compositeInfo.bloomIntensity, 0.0, 10.0);
            ImGui::SliderFloat("Bloom threshold", &pushConstants.bloomThreshold, 0.0, 10.0);
            ImGui::SliderInt("Blur iteration", &blurIteration, 0, 64);
            ImGui::SliderInt("Blur size", &bloomInfo.blurSize, 0, 64);
        }

        // Tone mapping
        ImGui::Checkbox("Enable tone mapping",
                        reinterpret_cast<bool*>(&compositeInfo.enableToneMapping));
        if (compositeInfo.enableToneMapping) {
            ImGui::SliderFloat("Exposure", &compositeInfo.exposure, 0.0, 5.0);
        }

        // Gamma correction
        ImGui::Checkbox("Enable gamma correction",
                        reinterpret_cast<bool*>(&compositeInfo.enableGammaCorrection));
        if (compositeInfo.enableGammaCorrection) {
            ImGui::SliderFloat("Gamma", &compositeInfo.gamma, 0.0, 5.0);
        }

        // Show GPU time
        if (pushConstants.frame > 1) {
            ImGui::Text("GPU time: %f ms", gpuTimer.elapsedInMilli());
        }

        // Save image
        if (ImGui::Button("Save image")) {
            saveImage();
        }

        // Check shader files
        recreatePipelinesIfShadersWereUpdated();

        commandBuffer.beginTimestamp(gpuTimer);

        // Update
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

        commandBuffer.endTimestamp(gpuTimer);

        commandBuffer.copyImage(compositePass.getOutputImageBGRA(), getCurrentColorImage(),
                                vk::ImageLayout::eGeneral, vk::ImageLayout::ePresentSrcKHR, width,
                                height);
    }

    void saveImage() {
        // If a previous write task was launched, wait for it to complete
        if (writeTask.valid()) {
            writeTask.get();
        }

        auto* pixels = static_cast<uint8_t*>(imageSavingBuffer.map());
        context.oneTimeSubmit([&](vk::CommandBuffer commandBuffer) {
            Image::setImageLayout(commandBuffer, compositePass.getOutputImageRGBA(),
                                  vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
                                  vk::ImageAspectFlagBits::eColor, 1);

            vk::BufferImageCopy copyInfo;
            copyInfo.setImageExtent({width, height, 1});
            copyInfo.setImageSubresource({vk::ImageAspectFlagBits::eColor, 0, 0, 1});
            commandBuffer.copyImageToBuffer(compositePass.getOutputImageRGBA(),
                                            vk::ImageLayout::eTransferSrcOptimal,
                                            imageSavingBuffer.getBuffer(), copyInfo);

            Image::setImageLayout(commandBuffer, compositePass.getOutputImageRGBA(),
                                  vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral,
                                  vk::ImageAspectFlagBits::eColor, 1);
        });

        std::string frame = std::to_string(pushConstants.frame);
        std::string zeros = std::string(std::max(0, 3 - (int)frame.size()), '0');
        std::string img = zeros + frame + ".png";
        writeTask = std::async(std::launch::async, [=]() {
            stbi_write_png(img.c_str(), width, height, 4, pixels, width * 4);
        });
    }

    uint32_t maxRayRecursionDepth;

    Scene scene;

    CompositeInfo compositeInfo;
    CompositePass compositePass;
    BloomInfo bloomInfo;
    BloomPass bloomPass;

    Image baseImage;

    DescriptorSet descSet;
    RayTracingPipeline rayTracingPipeline;

    Camera* currentCamera = nullptr;
    FPSCamera fpsCamera;
    OrbitalCamera orbitalCamera;

    PushConstants pushConstants;
    GPUTimer gpuTimer;

    HostBuffer imageSavingBuffer;
    std::future<void> writeTask;
};

int main() {
    try {
        HelloApp app{};
        app.run();
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}
