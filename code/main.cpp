#include <random>

#include <reactive/reactive.hpp>

#include "image_writer.hpp"
#include "render_pass.hpp"
#include "renderer.hpp"
#include "scene.hpp"

class WindowApp : public rv::App {
public:
    WindowApp(bool enableValidation, uint32_t width, uint32_t height, const std::string& sceneName)
        : rv::App({
              .width = width,
              .height = height,
              .title = "Coalumine",
              .layers = enableValidation ? rv::Layer::Validation : rv::ArrayProxy<rv::Layer>{},
              .extensions = rv::Extension::RayTracing,
              .style = rv::UIStyle::Gray,
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

        renderer = std::make_unique<Renderer>(context,                  //
                                              rv::Window::getWidth(),   //
                                              rv::Window::getHeight(),  //
                                              sceneName);
        imageWriter = std::make_unique<ImageWriter>(context,                 //
                                                    rv::Window::getWidth(),  //
                                                    rv::Window::getHeight(), 1);
    }

    void onStart() override { gpuTimer = context.createGPUTimer({}); }

    void onUpdate(float dt) override {  //
        // RendererはWindowに依存させたくないため。DebugAppが処理する
        auto dragLeft = rv::Window::getMouseDragLeft();
        auto scroll = rv::Window::getMouseScroll();

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

    void onRender(const rv::CommandBufferHandle& commandBuffer) override {
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
                imageWriter->wait(0);
                imageWriter->writeImage(0, pushConstants.frame);
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
        rv::ImageHandle outputImage = renderer->compositePass.finalImageRGBA;
        commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eTransferSrcOptimal);
        commandBuffer->copyImageToBuffer(outputImage, imageWriter->getBuffer(0));
        commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eGeneral);
    }

    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<ImageWriter> imageWriter;

    rv::GPUTimerHandle gpuTimer;

    char inputTextBuffer[1024] = {0};
};

class HeadlessApp {
public:
    HeadlessApp(bool enableValidation,
                uint32_t width,
                uint32_t height,
                const std::string& sceneName)
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

        rv::StructureChain featuresChain;
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

        renderer = std::make_unique<Renderer>(context, width, height, sceneName);
        imageWriter = std::make_unique<ImageWriter>(context, width, height, imageCount);
    }

    void run() {
        rv::CPUTimer timer;

        renderer->pushConstants.sampleCount = 128;
        for (uint32_t i = 0; i < totalFrames; i++) {
            imageWriter->wait(imageIndex);

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
            rv::ImageHandle outputImage = renderer->compositePass.finalImageRGBA;
            commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eTransferSrcOptimal);
            commandBuffer->copyImageToBuffer(outputImage, imageWriter->getBuffer(imageIndex));
            commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eGeneral);

            // End command buffer
            commandBuffers[imageIndex]->end();

            // Submit
            context.submit(commandBuffer);
            context.getQueue().waitIdle();

            imageWriter->writeImage(imageIndex, renderer->pushConstants.frame);

            imageIndex = (imageIndex + 1) % imageCount;
        }

        context.getDevice().waitIdle();
        imageWriter->waitAll();

        spdlog::info("Total time: {} s", timer.elapsedInMilli() / 1000);
    }

private:
    rv::Context context;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<ImageWriter> imageWriter;

    uint32_t width;
    uint32_t height;
    uint32_t totalFrames = 180;
    uint32_t imageCount = 3;
    uint32_t imageIndex = 0;
    std::vector<rv::CommandBufferHandle> commandBuffers{};
    std::vector<rv::ImageHandle> images{};
};

int main(int argc, char* argv[]) {
    try {
        // 実行モード "window", "headless" は、
        // コマンドライン引数で与えるか、ランタイムのユーザー入力で与えることができる
        std::string mode;
        std::string sceneName;
        if (argc == 3) {
            mode = argv[1];
            sceneName = argv[2];
        } else {
            std::cout << "Which mode? (\"window\" or \"headless\")\n";
            std::cin >> mode;

            std::cout << "Which scene?\n";
            std::cin >> sceneName;
        }

        // Convert "dragon" -> "scenes/dragon.json"
        sceneName = std::format("scenes/{}.json", sceneName);

        if (mode == "window" || mode == "w") {
            WindowApp app{true, 1920, 1080, sceneName};
            app.run();
        } else if (mode == "headless" || mode == "h") {
            HeadlessApp app{false, 1920, 1080, sceneName};
            app.run();
        } else {
            throw std::runtime_error("Invalid mode. Please input \"window\" or \"headless\".");
        }
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}
