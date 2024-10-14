#pragma once
#include <glm/gtc/matrix_inverse.hpp>
#include <reactive/reactive.hpp>

#include "mesh.hpp"
#include "node.hpp"
#include "physical_camera.hpp"

struct InfiniteLight {
    float theta = 0.0f;
    float phi = 0.0f;
    glm::vec3 color = {0.0f, 0.0f, 0.0f};
    float intensity = 0.0f;

    glm::vec3 getDirection() const {
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        return {sinTheta * sinPhi, cosTheta, sinTheta * cosPhi};
    }
};

struct EnvironmentLight {
    rv::ImageHandle texture;
    glm::vec3 color = {0.0f, 0.0f, 0.0f};
    float intensity = 1.0f;
    float phi = 0.0f;
    bool useTexture = false;
    bool isVisible = true;
};

class Scene {
    friend class LoaderJson;
    friend class LoaderGltf;
    friend class LoaderObj;
    friend class LoaderAlembic;

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

    void updateBottomAccel(const rv::CommandBufferHandle& commandBuffer, int frame);

    void updateTopAccel(const rv::CommandBufferHandle& commandBuffer, int frame);

    void updateMaterialBuffer(const rv::CommandBufferHandle& commandBuffer);

    uint32_t getMaxFrame() const;

    const PhysicalCamera& getCamera() const { return m_camera; }

    EnvironmentLight& getEnvironmentLight() { return m_envLight; }

    InfiniteLight& getInfiniteLight() { return m_infiniteLight; }

    const rv::BufferHandle& getNodeDataBuffer() const { return m_nodeDataBuffer; }

    const rv::BufferHandle& getMaterialDataBuffer() const { return m_materialBuffer; }

    const rv::TopAccelHandle& getTopAccel() const { return m_topAccel; }

    const std::vector<rv::ImageHandle>& get2dTextures() const { return m_textures2d; }

    const std::vector<rv::ImageHandle>& get3dTextures() const { return m_textures3d; }

    void update(glm::vec2 dragLeft, float scroll);

    bool drawAttributes();

private:
    //  Scene
    std::vector<Node> m_nodes;
    std::vector<Mesh> m_meshes;
    std::vector<rv::ImageHandle> m_textures2d;
    std::vector<rv::ImageHandle> m_textures3d;

    // Accel
    std::vector<rv::BottomAccelHandle> m_bottomAccels;
    std::vector<rv::AccelInstance> m_accelInstances;
    rv::TopAccelHandle m_topAccel;

    // Light
    EnvironmentLight m_envLight;
    InfiniteLight m_infiniteLight;

    // Buffer
    std::vector<NodeData> m_nodeData;
    rv::BufferHandle m_nodeDataBuffer;

    std::vector<Material> m_materials;
    rv::BufferHandle m_materialBuffer;

    // Camera
    PhysicalCamera m_camera;
};
