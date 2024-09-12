#pragma once

#include <reactive/reactive.hpp>

#include "../shader/share.h"

struct Mesh {
    rv::BufferHandle vertexBuffer;
    rv::BufferHandle indexBuffer;
    uint32_t vertexCount;
    uint32_t triangleCount;
    int materialIndex = -1;
    rv::AABB aabb;
};

struct KeyFrame {
    float time = 0.0f;
    glm::vec3 translation = {0.0f, 0.0f, 0.0f};
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale = {1.0f, 1.0f, 1.0f};
};

class Node {
public:
    int meshIndex = -1;
    int materialIndex = -1;  // オーバーライド用
    glm::vec3 translation = glm::vec3{0.0, 0.0, 0.0};
    glm::quat rotation = glm::quat{1.0, 0.0, 0.0, 0.0};
    glm::vec3 scale = glm::vec3{1.0, 1.0, 1.0};
    std::vector<KeyFrame> keyFrames;

    glm::mat4 computeTransformMatrix(int frame) const {
        if (keyFrames.empty()) {
            glm::mat4 T = glm::translate(glm::mat4{1.0}, translation);
            glm::mat4 R = glm::mat4_cast(rotation);
            glm::mat4 S = glm::scale(glm::mat4{1.0}, scale);
            return T * R * S;
        }
        int index = frame % keyFrames.size();
        glm::mat4 T = glm::translate(glm::mat4{1.0}, keyFrames[index].translation);
        glm::mat4 R = glm::mat4_cast(keyFrames[index].rotation);
        glm::mat4 S = glm::scale(glm::mat4{1.0}, keyFrames[index].scale);
        return T * R * S;
    }

    glm::mat4 computeNormalMatrix(int frame) const {
        if (keyFrames.empty()) {
            return glm::mat4{1.0};
        }

        int index = frame % keyFrames.size();
        return glm::mat4_cast(keyFrames[index].rotation);
    }
};

class Scene {
    friend class LoaderJson;
    friend class LoaderGltf;
    friend class LoaderObj;

public:
    Scene() = default;

    void loadFromFile(const rv::Context& context, const std::filesystem::path& filepath);

    void createMaterialBuffer(const rv::Context& context);

    void createNodeDataBuffer(const rv::Context& context);

    void loadEnvLightTexture(const rv::Context& context, const std::filesystem::path& filepath);

    void createEnvLightTexture(const rv::Context& context,
                               const float* data,
                               uint32_t width,
                               uint32_t height,
                               uint32_t channel);

    void buildAccels(const rv::Context& context);

    bool shouldUpdate(int frame) const;

    void updateTopAccel(int frame);

    void updateMaterialBuffer(const rv::CommandBufferHandle& commandBuffer);

    // Scene
    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<rv::ImageHandle> textures3d;

    // Accel
    std::vector<rv::BottomAccelHandle> bottomAccels;
    rv::TopAccelHandle topAccel;

    // Light
    rv::ImageHandle envLightTexture;
    glm::vec3 envLightColor;
    float envLightIntensity = 1.0f;
    bool useEnvLightTexture = false;

    glm::vec3 infiniteLightDir = {};
    glm::vec3 infiniteLightColor = {};
    float infiniteLightIntensity = 0.0f;

    // Buffer
    std::vector<NodeData> nodeData;
    rv::BufferHandle nodeDataBuffer;

    std::vector<Material> materials;
    rv::BufferHandle materialBuffer;

    // Camera
    bool cameraExists = false;
    glm::vec3 cameraTranslation;
    glm::quat cameraRotation;
    float cameraYFov;
    float cameraScale = 1.0f;
};
