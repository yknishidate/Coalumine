#include <future>
#include <iostream>
#include <random>

#include <reactive/reactive.hpp>

#include <stb_image_write.h>

#include "render_pass.hpp"
#include "renderer.hpp"
#include "scene.hpp"

class WindowApp : public App {
public:
    WindowApp(bool enableValidation, uint32_t width, uint32_t height)
        : App({
              .width = width,
              .height = height,
              .title = "Coalumine",
              .layers = enableValidation ? Layer::Validation : ArrayProxy<Layer>{},
              .extensions = Extension::RayTracing,
              .style = UIStyle::Gray,
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
        // RendererはWindowに依存させたくないため。DebugAppが処理する
        auto dragLeft = Window::getMouseDragLeft();
        auto scroll = Window::getMouseScroll();

        renderer->update(dragLeft, scroll);
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
        auto& pushConstants = renderer->pushConstants;

        static int imageIndex = 0;
        static bool enableBloom = false;
        static bool enableAccum = pushConstants.enableAccum;
        static int blurIteration = 32;
        static bool playAnimation = true;
        static bool open = true;
        if (open) {
            ImGui::Begin("Settings", &open);
            ImGui::Combo("Image", &imageIndex, "Render\0Bloom");
            ImGui::SliderInt("Sample count", &pushConstants.sampleCount, 1, 512);
            if (ImGui::Button("Save image")) {
                saveImage();
            }

            // Dome light
            if (ImGui::SliderFloat("Dome light phi", &pushConstants.domeLightPhi, 0.0, 360.0)) {
                renderer->reset();
            }

            // Infinite light
            static glm::vec4 defaultInfiniteLightDirection = pushConstants.infiniteLightDirection;
            ImGui::SliderFloat4("Infinite light direction",
                                reinterpret_cast<float*>(&pushConstants.infiniteLightDirection),
                                -1.0, 1.0);
            ImGui::SliderFloat("Infinite light intensity", &pushConstants.infiniteLightIntensity,
                               0.0f, 1.0f);
            if (ImGui::Button("Reset infinite light")) {
                pushConstants.infiniteLightDirection = defaultInfiniteLightDirection;
            }

            if (ImGui::Checkbox("Enable accum", &enableAccum)) {
                pushConstants.enableAccum = enableAccum;
                renderer->reset();
            }

            // Bloom
            ImGui::Checkbox("Enable bloom", &enableBloom);
            if (enableBloom) {
                ImGui::SliderFloat("Bloom intensity", &renderer->compositeInfo.bloomIntensity,  //
                                   0.0f, 10.0f);
                ImGui::SliderFloat("Bloom threshold", &pushConstants.bloomThreshold, 0.0f, 10.0f);
                ImGui::SliderInt("Blur iteration", &blurIteration, 0, 64);
                ImGui::SliderInt("Blur size", &renderer->bloomInfo.blurSize, 0, 64);
            }

            // Tone mapping
            ImGui::Checkbox("Enable tone mapping",
                            reinterpret_cast<bool*>(&renderer->compositeInfo.enableToneMapping));
            if (renderer->compositeInfo.enableToneMapping) {
                ImGui::SliderFloat("Exposure", &renderer->compositeInfo.exposure, 0.0f, 5.0f);
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
            if (pushConstants.frame > 1) {
                ImGui::Text("GPU time: %f ms", gpuTimer->elapsedInMilli());
            }

            if (ImGui::Button("Recompile")) {
                recompile();
            }

            ImGui::InputTextMultiline("Memo", inputTextBuffer, sizeof(inputTextBuffer));

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
    char inputTextBuffer[1024] = {0};
};

class HeadlessApp {
public:
    HeadlessApp(bool enableValidation, uint32_t width, uint32_t height)
        : width{width}, height{height} {
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
        commandBuffers.resize(imageCount);
        for (auto& commandBuffer : commandBuffers) {
            commandBuffer = context.allocateCommandBuffer();
        }

        writeTasks.resize(imageCount);
        imageSavingBuffers.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; i++) {
            imageSavingBuffers[i] = context.createBuffer({
                .usage = BufferUsage::Staging,
                .memory = MemoryUsage::Host,
                .size = width * height * 4 * sizeof(uint8_t),
                .debugName = "imageSavingBuffer",
            });
        }

        renderer = std::make_unique<Renderer>(context, width, height);
    }

    void run() {
        CPUTimer timer;

        renderer->pushConstants.sampleCount = 128;
        for (uint32_t i = 0; i < totalFrames; i++) {
            // Wait image saving
            if (writeTasks[imageIndex].valid()) {
                writeTasks[imageIndex].get();
            }

            renderer->update({0.0f, 0.0f}, 0.0f);

            auto& commandBuffer = commandBuffers[imageIndex];
            commandBuffer->begin();

            bool playAnimation = true;
            bool enableBloom = false;
            int blurIteration = 32;
            renderer->render(commandBuffer, playAnimation, enableBloom, blurIteration);

            commandBuffer->imageBarrier(
                renderer->compositePass.finalImageRGBA,  //
                vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer,
                vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead);

            // Copy to buffer
            ImageHandle outputImage = renderer->compositePass.finalImageRGBA;
            commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eTransferSrcOptimal);
            commandBuffer->copyImageToBuffer(outputImage, imageSavingBuffers[imageIndex]);
            commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eGeneral);

            // End command buffer
            commandBuffers[imageIndex]->end();

            // Submit
            context.submit(commandBuffer);
            context.getQueue().waitIdle();

            saveImage(imageIndex);

            imageIndex = (imageIndex + 1) % imageCount;
        }

        context.getDevice().waitIdle();
        for (auto& writeTask : writeTasks) {
            if (writeTask.valid()) {
                writeTask.get();
            }
        }

        spdlog::info("Total time: {} s", timer.elapsedInMilli() / 1000);
    }

    void saveImage(uint32_t index) {
        auto* pixels = static_cast<uint8_t*>(imageSavingBuffers[index]->map());
        std::string frame = std::to_string(renderer->pushConstants.frame);
        std::string zeros = std::string(std::max(0, 3 - static_cast<int>(frame.size())), '0');
        std::string img = zeros + frame + ".jpg";
        writeTasks[index] = std::async(std::launch::async, [=]() {
            stbi_write_jpg(img.c_str(), width, height, 4, pixels, 90);
            spdlog::info("Saved: {}/{}", frame, totalFrames);
        });
    }

private:
    Context context;
    std::unique_ptr<Renderer> renderer;

    uint32_t width;
    uint32_t height;
    uint32_t totalFrames = 180;
    uint32_t imageCount = 3;
    uint32_t imageIndex = 0;
    std::vector<CommandBufferHandle> commandBuffers{};
    std::vector<ImageHandle> images{};

    std::vector<BufferHandle> imageSavingBuffers;
    std::vector<std::future<void>> writeTasks;
};

int main(int argc, char* argv[]) {
    try {
        // 実行モード "window", "headless" は、
        // コマンドライン引数で与えるか、ランタイムのユーザー入力で与えることができる
        std::string mode;
        if (argc == 2) {
            mode = argv[1];
        } else {
            std::cout << "Which mode? (\"window\" or \"headless\")\n";
            std::cin >> mode;
        }

        if (mode == "window") {
            WindowApp app{true, 1920, 1080};
            app.run();
        } else if (mode == "headless") {
            HeadlessApp app{false, 1920, 1080};
            app.run();
        } else {
            throw std::runtime_error("Invalid mode. Please input \"window\" or \"headless\".");
        }
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}
