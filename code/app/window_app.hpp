#pragma once
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
        static int blurIteration = 16;
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
            const uint32_t maxFrame = m_renderer->m_scene.getMaxFrame();
            if (playAnimation) {
                if (maxFrame > 0) {
                    m_frame = (m_frame + 1) % maxFrame;
                    m_renderer->reset();
                }
            }

            // Frame
            if (ImGui::SliderInt("Frame", &m_frame, 0, maxFrame - 1)) {
                m_renderer->reset();
            }

            // GPU time
            float gpuTime = pushConstants.accumCount > 1 ? m_gpuTimer->elapsedInMilli() : 0.0f;
            ImGui::Text("Accum count: %d", pushConstants.accumCount);
            ImGui::Text("GPU time: %f ms", gpuTime);

            // Save button
            if (ImGui::Button("Save image")) {
                m_imageWriter->wait(0);
                m_imageWriter->writeImage(0, m_frame);
            }

            // Recompile button
            if (ImGui::Button("Recompile")) {
                recompile();
            }

            // Material
            m_renderer->m_scene.drawAttributes();

            // Light
            // TODO: move to Scene
            if (ImGui::CollapsingHeader("Light")) {
                ImGui::Indent(16.0f);
                // Dome light
                if (ImGui::SliderFloat("Env light phi", &pushConstants.envLightPhi, 0.0, 360.0,
                                       "%.0f")) {
                    m_renderer->reset();
                }
                if (ImGui::ColorEdit3("Env light color", &pushConstants.envLightColor[0])) {
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
                    ImGui::DragFloat("Bloom intensity", &compositeInfo.bloomIntensity, 0.000001f,
                                     0.0f, 10.0f, "%.6f");
                    ImGui::SliderFloat("Bloom threshold", &pushConstants.bloomThreshold,  //
                                       0.0f, 10.0f);
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

            // Memo
            if (ImGui::CollapsingHeader("Memo")) {
                ImGui::InputTextMultiline("Text", m_inputTextBuffer, sizeof(m_inputTextBuffer));
            }

            ImGui::End();
        }

        commandBuffer->beginTimestamp(m_gpuTimer);
        m_renderer->render(commandBuffer, m_frame, enableBloom, blurIteration);
        commandBuffer->endTimestamp(m_gpuTimer);

        // Copy to swapchain image
        commandBuffer->copyImage(m_renderer->m_compositePass.getOutputImageBGRA(),
                                 getCurrentColorImage(), vk::ImageLayout::eGeneral,
                                 vk::ImageLayout::ePresentSrcKHR);

        // Copy to buffer
        const rv::ImageHandle& outputImage = m_renderer->m_compositePass.getOutputImageRGBA();
        commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eTransferSrcOptimal);
        commandBuffer->copyImageToBuffer(outputImage, m_imageWriter->getBuffer(0));
        commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eGeneral);
    }

    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<ImageWriter> m_imageWriter;

    rv::GPUTimerHandle m_gpuTimer;

    char m_inputTextBuffer[1024] = {0};
    int m_frame = 0;
};
