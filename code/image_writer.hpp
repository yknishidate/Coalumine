#pragma once

#include <future>
#include <iostream>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <stb_image_write.h>
#include <glm/glm.hpp>
#include <reactive/reactive.hpp>

// 画像フォーマットはRGBA8とする
class ImageWriter {
public:
    ImageWriter(const rv::Context& context, uint32_t width, uint32_t height, uint32_t imageCount)
        : m_width{width}, m_height{height} {
        m_writeTasks.resize(imageCount);
        m_imageSavingBuffers.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; i++) {
            m_imageSavingBuffers[i] = context.createBuffer({
                .usage = rv::BufferUsage::Staging,
                .memory = rv::MemoryUsage::Host,
                .size = m_width * m_height * 4 * sizeof(uint8_t),
                .debugName = "imageSavingBuffer",
            });
        }
    }

    void writeImage(uint32_t index, uint32_t frame) {
        auto* pixels = static_cast<uint8_t*>(m_imageSavingBuffers[index]->map());
        std::string img = std::format("{:03}.jpg", frame);
        m_writeTasks[index] = std::async(std::launch::async, [=]() {
            stbi_write_jpg(img.c_str(), m_width, m_height, 4, pixels, 90);
            spdlog::info("Saved: {}", frame);
        });
    }

    auto getBuffer(uint32_t imageIndex) { return m_imageSavingBuffers[imageIndex]; }

    void wait(uint32_t imageIndex) {
        if (m_writeTasks[imageIndex].valid()) {
            m_writeTasks[imageIndex].get();
        }
    }

    void waitAll() {
        for (auto& task : m_writeTasks) {
            if (task.valid()) {
                task.get();
            }
        }
    }

    uint32_t m_width;
    uint32_t m_height;
    std::vector<rv::BufferHandle> m_imageSavingBuffers;
    std::vector<std::future<void>> m_writeTasks;
};
