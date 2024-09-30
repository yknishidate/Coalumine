#include <random>

#include <reactive/reactive.hpp>

#include "image_writer.hpp"
#include "render_pass.hpp"
#include "renderer.hpp"
#include "scene.hpp"

class WindowApp : public rv::App {
public:
    WindowApp(bool enableValidation,
              uint32_t width,
              uint32_t height,
              const std::filesystem::path& scenePath)
        : rv::App({
              .width = width,
              .height = height,
              .title = "Coalumine",
              .windowResizable = false,
              .vsync = false,
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

        m_renderer = std::make_unique<Renderer>(context,                  //
                                                rv::Window::getWidth(),   //
                                                rv::Window::getHeight(),  //
                                                scenePath);
        m_imageWriter = std::make_unique<ImageWriter>(context,                 //
                                                      rv::Window::getWidth(),  //
                                                      rv::Window::getHeight(), 1);
    }

    void onStart() override { m_gpuTimer = context.createGPUTimer({}); }

    void onUpdate(float dt) override {  //
        // RendererはWindowに依存させたくないため。DebugAppが処理する
        auto dragLeft = rv::Window::getMouseDragLeft();
        auto scroll = rv::Window::getMouseScroll();
        if (dragLeft != glm::vec2(0.0f) || scroll != 0.0f) {
            m_renderer->reset();
        }
        m_renderer->update(dragLeft, scroll);
    }

    void recompile() const {
        try {
            compileShader("base.rgen", "main");
            compileShader("base.rchit", "main");
            compileShader("base.rmiss", "main");
            compileShader("shadow.rmiss", "main");
            m_renderer->createPipelines(context);
            m_renderer->reset();
        } catch (const std::exception& e) {
            spdlog::error(e.what());
        }
    }

    void onRender(const rv::CommandBufferHandle& commandBuffer) override {
        auto& pushConstants = m_renderer->m_pushConstants;

        static int imageIndex = 0;
        static bool enableBloom = false;
        static int blurIteration = 32;
        static bool playAnimation = true;
        static bool open = true;
        if (open) {
            ImGui::Begin("Settings", &open);
            ImGui::Combo("Image", &imageIndex, "Render\0Bloom");
            ImGui::SliderInt("Sample count", &pushConstants.sampleCount, 1, 512);

            // Accumulation
            if (ImGui::Checkbox("Enable accum",
                                reinterpret_cast<bool*>(&pushConstants.enableAccum))) {
                m_renderer->reset();
            }

            // Adaptive sampling
            if (ImGui::Checkbox("Enable adaptive sampling",
                                reinterpret_cast<bool*>(&pushConstants.enableAdaptiveSampling))) {
                m_renderer->reset();
            }

            // Animation
            ImGui::Checkbox("Play animation", &playAnimation);
            if (playAnimation) {
                const uint32_t maxFrame = m_renderer->m_scene.getMaxFrame();
                if (maxFrame > 0) {
                    frame = (frame + 1) % maxFrame;
                    m_renderer->reset();
                }
            }

            // Frame
            ImGui::Text("Frame: %d", frame);

            // GPU time
            float gpuTime = pushConstants.accumCount > 1 ? m_gpuTimer->elapsedInMilli() : 0.0f;
            ImGui::Text("GPU time: %f ms", gpuTime);

            // Save button
            if (ImGui::Button("Save image")) {
                m_imageWriter->wait(0);
                m_imageWriter->writeImage(0, frame, pushConstants.accumCount);
            }

            // Recompile button
            if (ImGui::Button("Recompile")) {
                recompile();
            }

            // Material
            if (ImGui::CollapsingHeader("Material")) {
                size_t count = m_renderer->m_scene.materials.size();
                for (size_t i = 0u; i < count; i++) {
                    auto& mat = m_renderer->m_scene.materials[i];
                    if (ImGui::TreeNode(std::format("Material {}", i).c_str())) {
                        if (ImGui::ColorEdit3("BaseColor", &mat.baseColorFactor[0])) {
                            m_renderer->reset();
                        }
                        if (ImGui::SliderFloat("Roughness", &mat.roughnessFactor, 0.01f, 1.0f,
                                               "%.2f")) {
                            m_renderer->reset();
                        }
                        if (ImGui::SliderFloat("IOR", &mat.ior, 1.0f, 3.0f)) {
                            m_renderer->reset();
                        }
                        if (ImGui::SliderFloat("Disp.", &mat.dispersion, 0.0f, 0.5f)) {
                            m_renderer->reset();
                        }

                        ImGui::TreePop();
                    }
                }
            }

            // Light
            if (ImGui::CollapsingHeader("Light")) {
                ImGui::Indent(16.0f);
                // Dome light
                if (ImGui::SliderFloat("Env light phi", &pushConstants.envLightPhi, 0.0, 360.0,
                                       "%.0f")) {
                    m_renderer->reset();
                }
                if (ImGui::SliderFloat("Env light intensity", &pushConstants.envLightIntensity,
                                       0.0f, 10.0f)) {
                    m_renderer->reset();
                }

                // Infinite light
                if (ImGui::SliderFloat3("Infinite light direction",
                                        &pushConstants.infiniteLightDirection[0], -1.0, 1.0)) {
                    m_renderer->reset();
                }
                if (ImGui::SliderFloat("Infinite light intensity",
                                       &pushConstants.infiniteLightIntensity, 0.0f, 1.0f)) {
                    m_renderer->reset();
                }
                ImGui::Unindent(16.0f);
            }

            // Post process
            if (ImGui::CollapsingHeader("Post process")) {
                ImGui::Indent(16.0f);
                auto& compositeInfo = m_renderer->m_compositeInfo;
                auto& bloomInfo = m_renderer->m_bloomInfo;

                // Bloom
                ImGui::Checkbox("Enable bloom", &enableBloom);
                if (enableBloom) {
                    ImGui::SliderFloat("Bloom intensity",
                                       &compositeInfo.bloomIntensity,  //
                                       0.0f, 10.0f);
                    ImGui::SliderFloat("Bloom threshold", &pushConstants.bloomThreshold, 0.0f,
                                       10.0f);
                    ImGui::SliderInt("Blur iteration", &blurIteration, 0, 64);
                    ImGui::SliderInt("Blur size", &bloomInfo.blurSize, 0, 64);
                }

                // Tone mapping
                ImGui::Checkbox("Enable tone mapping",
                                reinterpret_cast<bool*>(&compositeInfo.enableToneMapping));
                if (compositeInfo.enableToneMapping) {
                    ImGui::SliderFloat("Exposure", &compositeInfo.exposure, 0.0f, 5.0f);
                }

                // Gamma correction
                ImGui::Checkbox("Enable gamma correction",
                                reinterpret_cast<bool*>(&compositeInfo.enableGammaCorrection));
                if (compositeInfo.enableGammaCorrection) {
                    ImGui::SliderFloat("Gamma", &compositeInfo.gamma, 0.0, 5.0);
                }
                ImGui::Unindent(16.0f);
            }

            // Camera
            if (ImGui::CollapsingHeader("Camera")) {
                ImGui::Indent(16.0f);
                auto& camera = m_renderer->m_scene.camera;
                float fovY = glm::degrees(camera.getFovY());
                if (ImGui::DragFloat("FOV Y", &fovY, 1.0f, 0.0f, 180.0f)) {
                    camera.setFovY(glm::radians(fovY));
                    m_renderer->reset();
                }
                if (ImGui::DragFloat("Lens radius", &camera.m_lensRadius, 0.01f, 0.0f, 1.0f)) {
                    m_renderer->reset();
                }
                if (ImGui::DragFloat("Object distance", &camera.m_objectDistance, 0.01f, 0.0f)) {
                    m_renderer->reset();
                }
                ImGui::Unindent(16.0f);
            }

            // Memo
            ImGui::InputTextMultiline("Memo", m_inputTextBuffer, sizeof(m_inputTextBuffer));

            ImGui::End();
        }

        m_renderer->m_scene.updateMaterialBuffer(commandBuffer);

        commandBuffer->beginTimestamp(m_gpuTimer);
        m_renderer->render(commandBuffer, frame, enableBloom, blurIteration);
        commandBuffer->endTimestamp(m_gpuTimer);

        // Copy to swapchain image
        commandBuffer->copyImage(m_renderer->m_compositePass.finalImageBGRA, getCurrentColorImage(),
                                 vk::ImageLayout::eGeneral, vk::ImageLayout::ePresentSrcKHR);

        // Copy to buffer
        rv::ImageHandle outputImage = m_renderer->m_compositePass.finalImageRGBA;
        commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eTransferSrcOptimal);
        commandBuffer->copyImageToBuffer(outputImage, m_imageWriter->getBuffer(0));
        commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eGeneral);
    }

    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<ImageWriter> m_imageWriter;

    rv::GPUTimerHandle m_gpuTimer;

    char m_inputTextBuffer[1024] = {0};
    int frame = 0;
};

class HeadlessApp {
public:
    HeadlessApp(bool enableValidation,
                uint32_t width,
                uint32_t height,
                const std::filesystem::path& scenePath)
        : m_width{width}, m_height{height} {
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
        m_context.initInstance(enableValidation, layers, instanceExtensions, VK_API_VERSION_1_3);
        m_context.initPhysicalDevice();

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

        vk::PhysicalDeviceScalarBlockLayoutFeatures scalarBlockLayoutFeatures{true};

        vk::PhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures{true};

        vk::PhysicalDeviceSynchronization2Features synchronization2Features{true};

        rv::StructureChain featuresChain;
        featuresChain.add(descFeatures);
        featuresChain.add(storage8BitFeatures);
        featuresChain.add(shaderFloat16Int8Features);
        featuresChain.add(bufferDeviceAddressFeatures);
        featuresChain.add(scalarBlockLayoutFeatures);
        featuresChain.add(shaderObjectFeatures);
        featuresChain.add(synchronization2Features);

        vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{true};
        vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{true};
        vk::PhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{true};
        featuresChain.add(rayTracingPipelineFeatures);
        featuresChain.add(accelerationStructureFeatures);
        featuresChain.add(rayQueryFeatures);

        m_context.initDevice(deviceExtensions, deviceFeatures, featuresChain.pFirst, true);
        m_commandBuffers.resize(m_imageCount);
        for (auto& commandBuffer : m_commandBuffers) {
            commandBuffer = m_context.allocateCommandBuffer();
        }

        m_renderer = std::make_unique<Renderer>(m_context, width, height, scenePath);
        m_imageWriter = std::make_unique<ImageWriter>(m_context, width, height, m_imageCount);
    }

    void run() {
        rv::CPUTimer timer;

        m_renderer->m_pushConstants.sampleCount = 128;
        for (uint32_t i = 0; i < m_totalFrames; i++) {
            m_imageWriter->wait(m_imageIndex);

            m_renderer->update({0.0f, 0.0f}, 0.0f);

            auto& commandBuffer = m_commandBuffers[m_imageIndex];
            commandBuffer->begin();

            bool playAnimation = true;
            bool enableBloom = false;
            int blurIteration = 32;
            m_renderer->render(commandBuffer, playAnimation, enableBloom, blurIteration);

            commandBuffer->imageBarrier(
                m_renderer->m_compositePass.finalImageRGBA,  //
                vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer,
                vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead);

            // Copy to buffer
            rv::ImageHandle outputImage = m_renderer->m_compositePass.finalImageRGBA;
            commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eTransferSrcOptimal);
            commandBuffer->copyImageToBuffer(outputImage, m_imageWriter->getBuffer(m_imageIndex));
            commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eGeneral);

            // End command buffer
            m_commandBuffers[m_imageIndex]->end();

            // Submit
            m_context.submit(commandBuffer);
            m_context.getQueue().waitIdle();

            m_imageWriter->writeImage(m_imageIndex, frame, m_renderer->m_pushConstants.accumCount);

            m_imageIndex = (m_imageIndex + 1) % m_imageCount;
        }

        m_context.getDevice().waitIdle();
        m_imageWriter->waitAll();

        spdlog::info("Total time: {} s", timer.elapsedInMilli() / 1000);
    }

private:
    rv::Context m_context;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<ImageWriter> m_imageWriter;

    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_totalFrames = 180;
    uint32_t m_imageCount = 3;
    uint32_t m_imageIndex = 0;
    std::vector<rv::CommandBufferHandle> m_commandBuffers{};
    std::vector<rv::ImageHandle> m_images{};
    int frame = 0;
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

        const auto scenePath = getAssetDirectory() / std::format("scenes/{}.json", sceneName);
        if (mode == "window" || mode == "w") {
            WindowApp app{true, 1920, 1080, scenePath};
            app.run();
        } else if (mode == "headless" || mode == "h") {
            HeadlessApp app{false, 1920, 1080, scenePath};
            app.run();
        } else {
            throw std::runtime_error("Invalid mode. Please input \"window\" or \"headless\".");
        }
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}
