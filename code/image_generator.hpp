#pragma once

#include <glm/glm.hpp>
#include <vector>

class ImageGenerator {
public:
    struct Knot {
        float position;
        glm::vec3 color;
    };

    static std::vector<glm::vec4> gradientHorizontal(uint32_t width,
                                                     uint32_t height,
                                                     uint32_t depth,
                                                     uint32_t channel,
                                                     const std::vector<Knot>& knots) {
        std::vector<glm::vec4> data(width * height * depth * channel);
        for (uint32_t x = 0; x < width; x++) {
            glm::vec3 color = colorRamp(x / static_cast<float>(width), knots);
            for (uint32_t z = 0; z < depth; z++) {
                for (uint32_t y = 0; y < height; y++) {
                    uint32_t index = z * (width * height) + y * width + x;
                    data[index] = glm::vec4(color, 0.0);
                }
            }
        }
        return data;
    }

    static std::vector<glm::vec4> gradientHorizontal(uint32_t width,
                                                     uint32_t height,
                                                     uint32_t channel,
                                                     const std::vector<Knot>& knots) {
        return gradientHorizontal(width, height, 1, channel, knots);
    }

    static std::vector<glm::vec4> gradientVertical(uint32_t width,
                                                   uint32_t height,
                                                   uint32_t depth,
                                                   uint32_t channel,
                                                   const std::vector<Knot>& knots) {
        std::vector<glm::vec4> data(width * height * depth * channel);
        for (uint32_t y = 0; y < height; y++) {
            glm::vec3 color = colorRamp(y / static_cast<float>(height), knots);
            for (uint32_t z = 0; z < depth; z++) {
                for (uint32_t x = 0; x < width; x++) {
                    uint32_t index = z * (width * height) + y * width + x;
                    data[index] = glm::vec4(color, 0.0);
                }
            }
        }
        return data;
    }

    static std::vector<glm::vec4> gradientVertical(uint32_t width,
                                                   uint32_t height,
                                                   uint32_t channel,
                                                   const std::vector<Knot>& knots) {
        return gradientVertical(width, height, 1, channel, knots);
    }

    static std::vector<glm::vec4> gradientDepth(uint32_t width,
                                                uint32_t height,
                                                uint32_t depth,
                                                uint32_t channel,
                                                const std::vector<Knot>& knots) {
        std::vector<glm::vec4> data(width * height * depth * channel);
        for (uint32_t z = 0; z < depth; z++) {
            glm::vec3 color = colorRamp(z / static_cast<float>(depth), knots);
            for (uint32_t y = 0; y < height; y++) {
                for (uint32_t x = 0; x < width; x++) {
                    uint32_t index = z * (width * height) + y * width + x;
                    data[index] = glm::vec4(color, 0.0);
                }
            }
        }
        return data;
    }

    static glm::vec3 colorRamp(float value, const std::vector<Knot>& knots) {
        if (knots.empty()) {
            return glm::vec3(0.0f);  // デフォルトの色 (黒) を返す
        }

        // ノットの数が1つの場合、常にその色を返す
        if (knots.size() == 1) {
            return knots[0].color;
        }

        // value が最小のノット以下なら最初の色を返す
        if (value <= knots.front().position) {
            return knots.front().color;
        }

        // value が最大のノット以上なら最後の色を返す
        if (value >= knots.back().position) {
            return knots.back().color;
        }

        // それ以外の場合、該当する区間を探して線形補間
        for (size_t i = 1; i < knots.size(); ++i) {
            const float currPos = knots[i].position;
            const float prevPos = knots[i - 1].position;
            if (value < currPos) {
                float t = (value - prevPos) / (currPos - prevPos);
                return glm::mix(knots[i - 1].color, knots[i].color, t);
            }
        }

        // 万が一のケースでは最後の色を返す
        return knots.back().color;
    }
};
