#pragma once
#include <glm/gtc/matrix_inverse.hpp>
#include <reactive/reactive.hpp>

#include "../../shader/share.h"

// TODO:
// enum class AnimationMode {
//    kOnce,
//    kRepeat,
// };

struct Mesh {
    struct KeyFrameMesh {
        rv::BufferHandle vertexBuffer;
        rv::BufferHandle indexBuffer;
        uint32_t vertexCount;
        uint32_t triangleCount;
    };

    uint32_t getMaxVertexCount() const {
        uint32_t maxCount = 0;
        for (const auto& frame : keyFrames) {
            maxCount = std::max(maxCount, frame.vertexCount);
        }
        return maxCount;
    }

    uint32_t getMaxTriangleCount() const {
        uint32_t maxCount = 0;
        for (const auto& frame : keyFrames) {
            maxCount = std::max(maxCount, frame.triangleCount);
        }
        return maxCount;
    }

    bool hasAnimation() const { return keyFrames.size() > 1; }

    std::vector<KeyFrameMesh> keyFrames;

    const KeyFrameMesh& getKeyFrameMesh(int frame) {
        return keyFrames[std::clamp(frame, 0, static_cast<int>(keyFrames.size() - 1))];
    }

    int materialIndex = -1;
    rv::AABB aabb;
};
