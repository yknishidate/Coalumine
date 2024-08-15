#pragma once

#include <reactive/Graphics/Buffer.hpp>
#include <reactive/Graphics/Context.hpp>
#include <reactive/Scene/Object.hpp>

#include <tiny_gltf.h>

using namespace rv;

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
    uint32_t materialType = MATERIAL_TYPE_DIFFUSE;
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

struct Material {
    // Textures
    int baseColorTextureIndex{-1};
    int metallicRoughnessTextureIndex{-1};
    int normalTextureIndex{-1};
    int occlusionTextureIndex{-1};

    int emissiveTextureIndex{-1};
    float metallicFactor{0.0f};
    float roughnessFactor{0.0f};
    float _dummy0;

    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float _dummy1;
};

class Scene {
public:
    Scene() = default;

    void loadFromFile(const Context& context, const std::filesystem::path& filepath);

    void createNormalMatrixBuffer(const Context& context);

    void loadDomeLightTexture(const Context& context, const std::filesystem::path& filepath);

    void createDomeLightTexture(const Context& context,
                                const float* data,
                                uint32_t width,
                                uint32_t height,
                                uint32_t channel);

    void createDomeLightTexture(const Context& context,
                                uint32_t width,
                                uint32_t height,
                                const void* data);

    void loadNodes(const Context& context, tinygltf::Model& gltfModel);

    void loadMeshes(const Context& context, tinygltf::Model& gltfModel);

    void loadMaterials(const Context& context, tinygltf::Model& gltfModel);

    void loadAnimation(const Context& context, const tinygltf::Model& model);

    void buildAccels(const Context& context);

    bool shouldUpdate(int frame) const;

    void updateTopAccel(int frame);

    int addMaterial(const Context& context, const Material& material);

    int addMesh(const Mesh& mesh, int materialIndex);

    int addNode(const Node& node);

    void initMaterialIndexBuffer(const Context& context);

    void initAddressBuffer(const Context& context);

    std::vector<Node> nodes;
    std::vector<BufferHandle> vertexBuffers;
    std::vector<BufferHandle> indexBuffers;
    std::vector<uint32_t> vertexCounts;
    std::vector<uint32_t> triangleCounts;

    std::vector<BottomAccelHandle> bottomAccels;
    TopAccelHandle topAccel;
    ImageHandle domeLightTexture;

    std::vector<int> materialIndices;
    BufferHandle materialIndexBuffer;

    std::vector<Material> materials;
    BufferHandle materialBuffer;

    Address address;
    BufferHandle addressBuffer;

    bool cameraExists = false;
    glm::vec3 cameraTranslation;
    glm::quat cameraRotation;
    float cameraYFov;

    std::vector<glm::mat4> normalMatrices;
    BufferHandle normalMatrixBuffer;
};
