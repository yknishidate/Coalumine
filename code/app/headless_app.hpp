#pragma once
#include <random>
#include <reactive/reactive.hpp>

#include "image_writer.hpp"
#include "render_pass.hpp"
#include "renderer.hpp"
#include "scene/scene.hpp"

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

        m_totalFrames = m_renderer->m_scene.getMaxFrame();
    }

    void run() {
        rv::CPUTimer renderTimer;

        for (uint32_t i = 0; i < m_totalFrames; i++) {
            m_imageWriter->wait(m_imageIndex);

            m_renderer->update({0.0f, 0.0f}, 0.0f);

            auto& commandBuffer = m_commandBuffers[m_imageIndex];
            commandBuffer->begin();

            bool enableBloom = false;
            int blurIteration = 32;
            m_renderer->render(commandBuffer, m_frame, enableBloom, blurIteration);

            commandBuffer->imageBarrier(
                m_renderer->m_compositePass.getOutputImageRGBA(),  //
                vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer,
                vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead);

            // Copy to buffer
            rv::ImageHandle outputImage = m_renderer->m_compositePass.getOutputImageRGBA();
            commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eTransferSrcOptimal);
            commandBuffer->copyImageToBuffer(outputImage, m_imageWriter->getBuffer(m_imageIndex));
            commandBuffer->transitionLayout(outputImage, vk::ImageLayout::eGeneral);

            // End command buffer
            m_commandBuffers[m_imageIndex]->end();

            // Submit
            m_context.submit(commandBuffer);
            m_context.getQueue().waitIdle();

            m_imageWriter->writeImage(m_imageIndex, m_frame);

            m_imageIndex = (m_imageIndex + 1) % m_imageCount;
            m_frame++;

            if (m_timer.elapsedInMilli() > kTimeLimit) {
                spdlog::warn("Time over...: {}", m_timer.elapsedInMilli());
                break;
            }
        }

        m_context.getDevice().waitIdle();
        m_imageWriter->waitAll();

        spdlog::info("Total render time: {} s", renderTimer.elapsedInMilli() / 1000);
    }

private:
    static constexpr float kTimeLimit = 250000.0f;  // [ms]
    rv::CPUTimer m_timer;

    rv::Context m_context;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<ImageWriter> m_imageWriter;

    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_totalFrames = 0;
    uint32_t m_imageCount = 3;
    uint32_t m_imageIndex = 0;
    std::vector<rv::CommandBufferHandle> m_commandBuffers{};
    std::vector<rv::ImageHandle> m_images{};
    int m_frame = 0;
};
