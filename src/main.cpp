#include <future>
#include <random>

#include "../shader/share.h"
#include "App.hpp"

#define NOMINMAX
#define TINYGLTF_IMPLEMENTATION
#include "common.hpp"
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
        spdlog::info("RayTracingPipelineProperties::maxRayRecursionDepth: {}",
                     maxRayRecursionDepth);

        scene.loadFromFile(context);
        scene.buildAccels(context);
        scene.loadDomeLightTexture(context);

        // Add materials
        Material diffuseMaterial;
        diffuseMaterial.baseColorFactor = glm::vec4{1.0, 1.0, 1.0, 1.0};
        Material glassMaterial;
        glassMaterial.baseColorFactor = glm::vec4{1.0, 1.0, 1.0, 0.0};
        glassMaterial.roughnessFactor = 0.3;
        Material metalMaterial;
        metalMaterial.baseColorFactor = glm::vec4{1.0, 1.0, 1.0, 1.0};
        metalMaterial.metallicFactor = 1.0;
        metalMaterial.roughnessFactor = 0.3;
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

        scene.initMaterialIndexBuffer(context);
        scene.initAddressBuffer(context);

        baseImage = context.createImage({
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eTransferSrc,
            .initialLayout = vk::ImageLayout::eGeneral,
            .aspect = vk::ImageAspectFlagBits::eColor,
            .width = width,
            .height = height,
            .format = vk::Format::eR32G32B32A32Sfloat,
        });

        createPipelines(context);

        orbitalCamera = OrbitalCamera{app, width, height};
        orbitalCamera.phi = 25.0f;
        orbitalCamera.theta = 30.0f;
        currentCamera = &orbitalCamera;

        if (scene.cameraExists) {
            fpsCamera = FPSCamera{app, width, height};
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
    }

    void createPipelines(const Context& context) {
        // TODO: remove compilation
        std::vector<Shader> shaders(4);
        shaders[0] = context.createShader({
            .code = compileShader("base.rgen", "main"),
            .stage = vk::ShaderStageFlagBits::eRaygenKHR,
        });
        shaders[1] = context.createShader({
            .code = compileShader("base.rmiss", "main"),
            .stage = vk::ShaderStageFlagBits::eMissKHR,
        });
        shaders[2] = context.createShader({
            .code = compileShader("shadow.rmiss", "main"),
            .stage = vk::ShaderStageFlagBits::eMissKHR,
        });
        shaders[3] = context.createShader({
            .code = compileShader("base.rchit", "main"),
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
                    {"lowDomeLightTexture", scene.lowDomeLightTexture},
                },
            .accels = {{"topLevelAS", scene.topAccel}},
        });

        rayTracingPipeline = context.createRayTracingPipeline({
            .rgenShaders = shaders[0],
            .missShaders = {shaders, 1, 2},
            .chitShaders = shaders[3],
            .descSetLayout = descSet.getLayout(),
            .pushSize = sizeof(PushConstants),
            .maxRayRecursionDepth = maxRayRecursionDepth,
        });
    }

    void update() {
        currentCamera->processInput();
        pushConstants.frame++;
        pushConstants.invView = currentCamera->getInvView();
        pushConstants.invProj = currentCamera->getInvProj();
    }

    void render(const CommandBuffer& commandBuffer,
                bool playAnimation,
                bool enableBloom,
                int blurIteration) {
        // Update
        if (playAnimation) {
            scene.updateTopAccel(commandBuffer.commandBuffer, pushConstants.frame);
        }

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

    Image baseImage;

    DescriptorSet descSet;
    RayTracingPipeline rayTracingPipeline;

    Camera* currentCamera = nullptr;
    FPSCamera fpsCamera;
    OrbitalCamera orbitalCamera;

    PushConstants pushConstants;
};

class HeadlessRenderer {
public:
    HeadlessRenderer(bool enableValidation) {
        spdlog::set_pattern("[%^%l%$] %v");

        std::vector<const char*> instanceExtensions;
        if (enableValidation) {
            instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        std::vector<const char*> layers = {};
        if (enableValidation) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        }

        // NOTE: Assuming Vulkan 1.3
        context.initInstance(enableValidation, layers, instanceExtensions, VK_API_VERSION_1_3);
        context.initPhysicalDevice();

        std::vector deviceExtensions{
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_QUERY_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        };

        vk::PhysicalDeviceFeatures deviceFeatures;
        deviceFeatures.setShaderInt64(true);
        deviceFeatures.setFragmentStoresAndAtomics(true);
        deviceFeatures.setVertexPipelineStoresAndAtomics(true);
        deviceFeatures.setGeometryShader(true);
        deviceFeatures.setFillModeNonSolid(true);
        deviceFeatures.setWideLines(true);

        vk::PhysicalDeviceDescriptorIndexingFeatures descFeatures;
        descFeatures.setRuntimeDescriptorArray(true);

        vk::PhysicalDevice8BitStorageFeatures storage8BitFeatures;
        storage8BitFeatures.setStorageBuffer8BitAccess(true);

        vk::PhysicalDeviceShaderFloat16Int8Features shaderFloat16Int8Features;
        shaderFloat16Int8Features.setShaderInt8(true);

        vk::PhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{true};

        vk::PhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures{true};

        vk::PhysicalDeviceSynchronization2Features synchronization2Features{true};

        StructureChain featuresChain;
        featuresChain.add(descFeatures);
        featuresChain.add(storage8BitFeatures);
        featuresChain.add(shaderFloat16Int8Features);
        featuresChain.add(bufferDeviceAddressFeatures);
        featuresChain.add(shaderObjectFeatures);
        featuresChain.add(synchronization2Features);

        vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{true};
        vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{true};
        vk::PhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{true};
        featuresChain.add(rayTracingPipelineFeatures);
        featuresChain.add(accelerationStructureFeatures);
        featuresChain.add(rayQueryFeatures);

        context.initDevice(deviceExtensions, deviceFeatures, featuresChain.pFirst, true);
        commandBuffers = context.allocateCommandBuffers(imageCount);
    }

    void run() {}

private:
    Context context;

    uint32_t imageCount = 3;
    uint32_t imageIndex = 0;
    std::vector<vk::UniqueCommandBuffer> commandBuffers{};
    std::vector<vk::UniqueFence> fences{};
    std::vector<vk::UniqueImage> images{};
};

class HelloApp : public App {
public:
    HelloApp()
        : App({
              .width = 1920,
              .height = 1080,
              .title = "rtcamp9",
              .enableValidation = true,
              .enableRayTracing = true,
          }),
          renderer(context, width, height, this) {}

    void onStart() override {
        spdlog::info("Executable directory: {}", getExecutableDirectory().string());
        spdlog::info("Shader source directory: {}", getShaderSourceDirectory().string());
        spdlog::info("SPIR-V directory: {}", getSpvDirectory().string());
        fs::create_directory(getSpvDirectory());

        gpuTimer = context.createGPUTimer({});

        imageSavingBuffer = context.createHostBuffer({
            .usage = BufferUsage::Staging,
            .size = width * height * 4 * sizeof(uint8_t),
        });
    }

    void onUpdate() override { renderer.update(); }

    void recreatePipelinesIfShadersWereUpdated() {
        bool shouldRecreate = false;
        shouldRecreate |= shouldRecompile("base.rgen", "main");
        shouldRecreate |= shouldRecompile("base.rchit", "main");
        shouldRecreate |= shouldRecompile("base.rmiss", "main");
        if (shouldRecreate) {
            try {
                renderer.createPipelines(context);
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
        ImGui::SliderInt("Sample count", &renderer.pushConstants.sampleCount, 1, 512);
        ImGui::SliderFloat("Dome light theta", &renderer.pushConstants.domeLightTheta, 0.0, 360.0);
        ImGui::SliderFloat("Dome light phi", &renderer.pushConstants.domeLightPhi, 0.0, 360.0);

        // Bloom
        ImGui::Checkbox("Enable bloom", &enableBloom);
        if (enableBloom) {
            ImGui::SliderFloat("Bloom intensity", &renderer.compositeInfo.bloomIntensity, 0.0,
                               10.0);
            ImGui::SliderFloat("Bloom threshold", &renderer.pushConstants.bloomThreshold, 0.0,
                               10.0);
            ImGui::SliderInt("Blur iteration", &blurIteration, 0, 64);
            ImGui::SliderInt("Blur size", &renderer.bloomInfo.blurSize, 0, 64);
        }

        // Tone mapping
        ImGui::Checkbox("Enable tone mapping",
                        reinterpret_cast<bool*>(&renderer.compositeInfo.enableToneMapping));
        if (renderer.compositeInfo.enableToneMapping) {
            ImGui::SliderFloat("Exposure", &renderer.compositeInfo.exposure, 0.0, 5.0);
        }

        // Gamma correction
        ImGui::Checkbox("Enable gamma correction",
                        reinterpret_cast<bool*>(&renderer.compositeInfo.enableGammaCorrection));
        if (renderer.compositeInfo.enableGammaCorrection) {
            ImGui::SliderFloat("Gamma", &renderer.compositeInfo.gamma, 0.0, 5.0);
        }

        static bool playAnimation = true;
        ImGui::Checkbox("Play animation", &playAnimation);

        // Show GPU time
        if (renderer.pushConstants.frame > 1) {
            ImGui::Text("GPU time: %f ms", gpuTimer.elapsedInMilli());
        }

        // Save image
        if (ImGui::Button("Save image")) {
            saveImage();
        }

        // Check shader files
        recreatePipelinesIfShadersWereUpdated();

        commandBuffer.beginTimestamp(gpuTimer);
        renderer.render(commandBuffer, playAnimation, enableBloom, blurIteration);
        commandBuffer.endTimestamp(gpuTimer);

        // Copy to swapchain image
        commandBuffer.copyImage(renderer.compositePass.getOutputImageBGRA(), getCurrentColorImage(),
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
            vk::Image outputImage = renderer.compositePass.getOutputImageRGBA();
            Image::setImageLayout(commandBuffer, outputImage, vk::ImageLayout::eGeneral,
                                  vk::ImageLayout::eTransferSrcOptimal,
                                  vk::ImageAspectFlagBits::eColor, 1);

            vk::BufferImageCopy copyInfo;
            copyInfo.setImageExtent({width, height, 1});
            copyInfo.setImageSubresource({vk::ImageAspectFlagBits::eColor, 0, 0, 1});
            commandBuffer.copyImageToBuffer(outputImage, vk::ImageLayout::eTransferSrcOptimal,
                                            imageSavingBuffer.getBuffer(), copyInfo);

            Image::setImageLayout(commandBuffer, outputImage, vk::ImageLayout::eTransferSrcOptimal,
                                  vk::ImageLayout::eGeneral, vk::ImageAspectFlagBits::eColor, 1);
        });

        std::string frame = std::to_string(renderer.pushConstants.frame);
        std::string zeros = std::string(std::max(0, 3 - (int)frame.size()), '0');
        std::string img = zeros + frame + ".jpg";
        writeTask = std::async(std::launch::async, [=]() {
            CPUTimer timer;
            stbi_write_jpg(img.c_str(), width, height, 4, pixels, 90);
            spdlog::info("Saved: {} ms", timer.elapsedInMilli());
        });
    }

    Renderer renderer;
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
