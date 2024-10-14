#pragma once

#include <reactive/reactive.hpp>

#include "../shader/share.h"

#include <glm/gtc/matrix_inverse.hpp>

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
    std::vector<int> childNodeIndices;  // TODO: use this
    glm::vec3 translation = glm::vec3{0.0, 0.0, 0.0};
    glm::quat rotation = glm::quat{1.0, 0.0, 0.0, 0.0};
    glm::vec3 scale = glm::vec3{1.0, 1.0, 1.0};
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

class PhysicalCamera : public rv::Camera {
public:
    PhysicalCamera() = default;

    PhysicalCamera(rv::Camera::Type type, float aspect) : rv::Camera(type, aspect) {}

    float getImageDistance() const { return m_sensorHeight / (2.0f * std::tan(fovY / 2.0f)); }

    bool drawAttributes() {
        bool changed = false;
        if (ImGui::CollapsingHeader("Camera")) {
            ImGui::Indent(16.0f);

            // Base camera params
            if (ImGui::Combo("Type", reinterpret_cast<int*>(&type), "Orbital\0FirstPerson")) {
                changed = true;
            }
            if (ImGui::DragFloat3("Rotation", &eulerRotation[0], 0.01f)) {
                changed = true;
            }
            if (type == Type::Orbital) {
                auto& _params = std::get<OrbitalParams>(params);
                if (ImGui::DragFloat3("Target", &_params.target[0], 0.1f)) {
                    changed = true;
                }
                if (ImGui::DragFloat("Distance", &_params.distance, 0.1f)) {
                    changed = true;
                }
            } else {
                // TODO: FirstPerson
            }

            // Physical params
            float fovYDeg = glm::degrees(fovY);
            if (ImGui::DragFloat("FOV Y", &fovYDeg, 1.0f, 0.0f, 180.0f)) {
                fovY = glm::radians(fovYDeg);
                changed = true;
            }
            if (ImGui::DragFloat("Lens radius", &m_lensRadius, 0.01f, 0.0f, 1.0f)) {
                changed = true;
            }
            if (ImGui::DragFloat("Object distance", &m_objectDistance, 0.01f, 0.0f)) {
                changed = true;
            }

            ImGui::Unindent(16.0f);
        }
        return changed;
    }

    float m_lensRadius = 0.0f;
    float m_objectDistance = 5.0f;
    static constexpr float m_sensorHeight = 1.0f;
};

class Scene {
    friend class LoaderJson;
    friend class LoaderGltf;
    friend class LoaderObj;

public:
    Scene() = default;

    void initialize(const rv::Context& context,
                    const std::filesystem::path& scenePath,
                    uint32_t width,
                    uint32_t height);

    void loadFromFile(const rv::Context& context, const std::filesystem::path& filepath);

    void createMaterialBuffer(const rv::Context& context);

    void createNodeDataBuffer(const rv::Context& context);

    void loadEnvLightTexture(const rv::Context& context, const std::filesystem::path& filepath);

    void createDummyTextures(const rv::Context& context);

    void createEnvLightTexture(const rv::Context& context,
                               const float* data,
                               uint32_t width,
                               uint32_t height,
                               uint32_t channel);

    void buildAccels(const rv::Context& context);

    bool shouldUpdate(int frame) const;

    void updateAccelInstances(int frame);

    void updateBottomAccel(int frame);

    void updateTopAccel(int frame);

    void updateMaterialBuffer(const rv::CommandBufferHandle& commandBuffer);

    uint32_t getMaxFrame() const;

    // Scene
    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<rv::ImageHandle> textures2d;
    std::vector<rv::ImageHandle> textures3d;

    // Accel
    std::vector<rv::BottomAccelHandle> bottomAccels;
    std::vector<rv::AccelInstance> accelInstances;
    rv::TopAccelHandle topAccel;

    // Light
    rv::ImageHandle envLightTexture;
    glm::vec3 envLightColor;
    float envLightIntensity = 1.0f;
    bool useEnvLightTexture = false;
    bool visibleEnvLightTexture = true;

    glm::vec3 infiniteLightDir = {};
    glm::vec3 infiniteLightColor = {};
    float infiniteLightIntensity = 0.0f;

    // Buffer
    std::vector<NodeData> nodeData;
    rv::BufferHandle nodeDataBuffer;

    std::vector<Material> materials;
    rv::BufferHandle materialBuffer;

    // Camera
    PhysicalCamera camera;
};
