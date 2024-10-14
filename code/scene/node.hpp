#pragma once
#include <glm/gtc/matrix_inverse.hpp>
#include <reactive/reactive.hpp>

#include "mesh.hpp"

struct KeyFrame {
    float time = 0.0f;
    glm::vec3 translation = {0.0f, 0.0f, 0.0f};
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale = {1.0f, 1.0f, 1.0f};
};

class Node {
public:
    int meshIndex = -1;
    int overrideMaterialIndex = -1;  // オーバーライド用

    Node* parentNode = nullptr;
    std::vector<int> childNodeIndices;

    // TODO: remove default TRS
    glm::vec3 translation = {0.0f, 0.0f, 0.0f};
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale = {1.0f, 1.0f, 1.0f};
    std::vector<KeyFrame> keyFrames;

    // TODO: interpolation by time
    glm::mat4 computeTransformMatrix(int frame) const {
        glm::mat4 TRS{};
        if (keyFrames.empty()) {
            glm::mat4 T = glm::translate(glm::mat4{1.0}, translation);
            glm::mat4 R = glm::mat4_cast(rotation);
            glm::mat4 S = glm::scale(glm::mat4{1.0}, scale);
            TRS = T * R * S;
        } else {
            int index = frame % keyFrames.size();
            glm::mat4 T = glm::translate(glm::mat4{1.0}, keyFrames[index].translation);
            glm::mat4 R = glm::mat4_cast(keyFrames[index].rotation);
            glm::mat4 S = glm::scale(glm::mat4{1.0}, keyFrames[index].scale);
            TRS = T * R * S;
        }

        if (!parentNode) {
            return TRS;
        }
        return parentNode->computeTransformMatrix(frame) * TRS;
    }

    glm::mat4 computeNormalMatrix(int frame) const {
        return glm::mat4{glm::inverseTranspose(glm::mat3{computeTransformMatrix(frame)})};
    }
};
