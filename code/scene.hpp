#pragma once

#include <reactive/Graphics/Buffer.hpp>
#include <reactive/Graphics/Context.hpp>
#include <reactive/Scene/Object.hpp>

#include <tiny_gltf.h>

#include "../shader/share.h"

struct Address {
    // vk::DeviceAddress vertices;
    // vk::DeviceAddress indices;
    vk::DeviceAddress materials;
};

struct KeyFrame {
    float time = 0.0f;
    glm::vec3 translation = {0.0f, 0.0f, 0.0f};
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale = {1.0f, 1.0f, 1.0f};
};

enum {
    MATERIAL_TYPE_DIFFUSE = 0,
    MATERIAL_TYPE_METAL = 1,
    MATERIAL_TYPE_GLASS = 2,
};

class Node {
public:
    int meshIndex = -1;
    // uint32_t materialType = MATERIAL_TYPE_DIFFUSE;
    glm::vec3 translation = glm::vec3{0.0, 0.0, 0.0};
    glm::quat rotation = glm::quat{0.0, 0.0, 0.0, 0.0};
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
public:
    Scene() = default;

    void loadFromFile(const rv::Context& context, const std::filesystem::path& filepath);

    void createNodeDataBuffer(const rv::Context& context);

    // void createNormalMatrixBuffer(const rv::Context& context);

    void loadDomeLightTexture(const rv::Context& context, const std::filesystem::path& filepath);

    void createDomeLightTexture(const rv::Context& context,
                                const float* data,
                                uint32_t width,
                                uint32_t height,
                                uint32_t channel);

    void createDomeLightTexture(const rv::Context& context,
                                uint32_t width,
                                uint32_t height,
                                const void* data);

    void loadNodes(const rv::Context& context, tinygltf::Model& gltfModel);

    void loadMeshes(const rv::Context& context, tinygltf::Model& gltfModel);

    void loadMaterials(const rv::Context& context, tinygltf::Model& gltfModel);

    void loadAnimation(const rv::Context& context, const tinygltf::Model& model);

    void buildAccels(const rv::Context& context);

    bool shouldUpdate(int frame) const;

    void updateTopAccel(int frame);

    int addMaterial(const rv::Context& context, const Material& material);

    int addMesh(const rv::Mesh& mesh, int materialIndex);

    int addNode(const Node& node);

    // void initMaterialIndexBuffer(const rv::Context& context);

    // void initAddressBuffer(const rv::Context& context);

    std::vector<Node> nodes;

    struct Mesh {
        rv::BufferHandle vertexBuffer;
        rv::BufferHandle indexBuffer;
        uint32_t vertexCount;
        uint32_t triangleCount;
        int materialIndex = -1;
    };

    std::vector<Mesh> meshes;

    std::vector<rv::BottomAccelHandle> bottomAccels;
    rv::TopAccelHandle topAccel;
    rv::ImageHandle domeLightTexture;

    std::vector<NodeData> nodeData;
    rv::BufferHandle nodeDataBuffer;

    std::vector<Material> materials;
    rv::BufferHandle materialBuffer;

    bool cameraExists = false;
    glm::vec3 cameraTranslation;
    glm::quat cameraRotation;
    float cameraYFov;
};
