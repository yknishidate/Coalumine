﻿#include "loader_json.hpp"

#include <fstream>
#include <random>

#include <nlohmann/json.hpp>

#include "../image_generator.hpp"
#include "../scene/scene.hpp"
#include "loader_alembic.hpp"
#include "loader_gltf.hpp"
#include "loader_obj.hpp"

void LoaderJson::loadFromFile(Scene& scene,
                              const rv::Context& context,
                              const std::filesystem::path& filepath) {
    // JSONファイルの読み込み
    std::ifstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("Failed to open file: {}", filepath.string());
        return;
    }

    nlohmann::json jsonData;
    file >> jsonData;

    // "gltf"セクションのパース
    if (const auto& gltf = jsonData.find("gltf"); gltf != jsonData.end()) {
        std::filesystem::path gltfPath = filepath.parent_path() / *gltf;

        LoaderGltf::loadFromFile(scene, context, gltfPath);
    }

    // gltf読み込み時点のオフセットを取得しておく
    const int materialOffset = static_cast<int>(scene.m_materials.size());
    const int meshOffset = static_cast<int>(scene.m_meshes.size());

    // "alembic"セクションのパース
    if (const auto& value = jsonData.find("alembic"); value != jsonData.end()) {
        std::filesystem::path path = filepath.parent_path() / *value;

        LoaderAlembic::loadFromFile(scene, context, path);
    }

    // "objects"セクションのパース
    for (const auto& object : jsonData["objects"]) {
        Node node;
        node.meshIndex = meshOffset + object["mesh_index"];
        if (const auto& itr = object.find("material_index"); itr != object.end()) {
            node.overrideMaterialIndex = materialOffset + *itr;
        }
        if (const auto& itr = object.find("translation"); itr != object.end()) {
            node.translation = {itr->at(0), itr->at(1), itr->at(2)};
        }
        if (const auto& itr = object.find("scale"); itr != object.end()) {
            node.scale = {itr->at(0), itr->at(1), itr->at(2)};
        }
        if (const auto& itr = object.find("rotation"); itr != object.end()) {
            glm::vec3 eularAngle;
            eularAngle.x = glm::radians(static_cast<float>(itr->at(0)));
            eularAngle.y = glm::radians(static_cast<float>(itr->at(1)));
            eularAngle.z = glm::radians(static_cast<float>(itr->at(2)));
            node.rotation = {eularAngle};
        }
        scene.m_nodes.push_back(node);
    }

    // "meshes"セクションのパース
    const auto& meshes = jsonData["meshes"];
    scene.m_meshes.reserve(meshes.size());
    for (const auto& mesh : meshes) {
        std::filesystem::path objPath = filepath.parent_path() / mesh["obj"];

        scene.m_meshes.push_back({});
        LoaderObj::loadMesh(scene.m_meshes.back(), context, objPath);
    }

    // "materials"セクションのパース
    for (const auto& material : jsonData["materials"]) {
        Material mat;
        if (const auto& itr = material.find("base_color"); itr != material.end()) {
            mat.baseColorFactor = {itr->at(0), itr->at(1), itr->at(2), itr->at(3)};
        }
        if (const auto& itr = material.find("emissive"); itr != material.end()) {
            mat.emissiveFactor = {itr->at(0), itr->at(1), itr->at(2)};
        }
        if (const auto& itr = material.find("metallic"); itr != material.end()) {
            mat.metallicFactor = *itr;
        }
        if (const auto& itr = material.find("roughness"); itr != material.end()) {
            mat.roughnessFactor = *itr;
        }
        if (const auto& itr = material.find("ior"); itr != material.end()) {
            mat.ior = *itr;
        }
        if (const auto& itr = material.find("dispersion"); itr != material.end()) {
            mat.dispersion = *itr;
        }

        // textures
        if (const auto& itr = material.find("base_color_texture"); itr != material.end()) {
            if (itr->at("projection") == "2d") {
                mat.baseColorTextureIndex = itr->at("texture_index");
            }
            if (itr->at("projection") == "3d") {
                mat.baseColorTextureIndex = TEXTURE_TYPE_OFFSET + itr->at("texture_index");
            }
        }
        if (const auto& itr = material.find("emissive_texture"); itr != material.end()) {
            if (itr->at("projection") == "2d") {
                mat.emissiveTextureIndex = itr->at("texture_index");
            }
            if (itr->at("projection") == "3d") {
                mat.emissiveTextureIndex = TEXTURE_TYPE_OFFSET + itr->at("texture_index");
            }
        }
        if (const auto& itr = material.find("metallic_roughness_texture"); itr != material.end()) {
            if (itr->at("projection") == "2d") {
                mat.metallicRoughnessTextureIndex = itr->at("texture_index");
            }
            if (itr->at("projection") == "3d") {
                mat.metallicRoughnessTextureIndex = TEXTURE_TYPE_OFFSET + itr->at("texture_index");
            }
        }
        scene.m_materials.push_back(mat);
    }

    for (const auto& material_override : jsonData["material_overrides"]) {
        int nodeIndex = material_override["node_index"];
        int materialIndex = material_override["material_index"];
        scene.m_nodes[nodeIndex].overrideMaterialIndex = materialIndex;
    }

    if (const auto& defaultMat = jsonData.find("default_material"); defaultMat != jsonData.end()) {
        if (defaultMat->at("type") == "random") {
            const auto& matIndices = defaultMat->at("material_indices");

            std::mt19937 rng(defaultMat->at("seed"));
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            for (auto& mesh : scene.m_meshes) {
                if (mesh.materialIndex == -1) {
                    double randomValue = dist(rng);
                    int indexIndex = static_cast<int>(std::floor(randomValue * matIndices.size()));
                    mesh.materialIndex = materialOffset + matIndices[indexIndex];
                }
            }
        }
    }

    // "camera"セクションのパース
    if (const auto& camera = jsonData.find("camera"); camera != jsonData.end()) {
        scene.m_camera = {rv::Camera::Type::Orbital, 1.0f};
        if (const auto& fovY = camera->find("fov_y"); fovY != camera->end()) {
            scene.m_camera.setFovY(glm::radians(static_cast<float>(*fovY)));
        }
        if (const auto& value = camera->find("distance"); value != camera->end()) {
            scene.m_camera.setDistance(static_cast<float>(*value));
        }
        if (const auto& rotation = camera->find("rotation"); rotation != camera->end()) {
            scene.m_camera.setEulerRotation(
                glm::vec3(rotation->at(0), rotation->at(1), rotation->at(2)));
        }
        if (const auto& values = camera->find("target"); values != camera->end()) {
            scene.m_camera.setTarget(glm::vec3(values->at(0), values->at(1), values->at(2)));
        }
        if (const auto& speed = camera->find("speed"); speed != camera->end()) {
            scene.m_camera.setDollySpeed(static_cast<float>(*speed));
        }
        if (const auto& value = camera->find("lens_radius"); value != camera->end()) {
            scene.m_camera.m_lensRadius = static_cast<float>(*value);
        }
        if (const auto& value = camera->find("object_distance"); value != camera->end()) {
            scene.m_camera.m_objectDistance = static_cast<float>(*value);
        }
    }

    // "environment_light"セクションのパース
    if (const auto& light = jsonData.find("environment_light"); light != jsonData.end()) {
        const auto& type = light->at("type");
        if (type == "texture") {
            std::filesystem::path texPath = filepath.parent_path() / light->at("texture");
            scene.loadEnvLightTexture(context, texPath);
            scene.m_envLight.useTexture = true;
        } else if (type == "procedural") {
            auto params = light->at("procedural_parameters");
            if (params["method"] == "gradient_horizontal") {
                uint32_t width = params["width"];
                uint32_t height = params["height"];

                std::vector<ImageGenerator::Knot> knots;
                for (const auto& knot : params["knots"]) {
                    const auto& color = knot["color"];
                    knots.push_back({knot["position"],
                                     {color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f,
                                      color[3] / 255.0f}});
                }

                const auto& data = ImageGenerator::gradientHorizontal(width, height, 4, knots);
                scene.createEnvLightTexture(context, static_cast<const float*>(&data[0][0]),  //
                                            width, height, 4);
                scene.m_envLight.useTexture = true;
            }
        } else if (type == "solid") {
            const float dummy = 0.0f;
            scene.createEnvLightTexture(context, &dummy, 1, 1, 4);
            scene.m_envLight.useTexture = false;
        }
        if (const auto& values = light->find("color"); values != light->end()) {
            scene.m_envLight.color = {values->at(0), values->at(1), values->at(2)};
        }
        if (const auto& intensity = light->find("intensity"); intensity != light->end()) {
            scene.m_envLight.intensity = *intensity;
        }
        if (const auto& value = light->find("visible_texture"); value != light->end()) {
            scene.m_envLight.isVisible = static_cast<bool>(*value);
        }
    }

    if (const auto& light = jsonData.find("infinite_light"); light != jsonData.end()) {
        auto& infLight = scene.m_infiniteLight;
        if (const auto& value = light->find("theta"); value != light->end()) {
            infLight.theta = static_cast<float>(*value);
        }
        if (const auto& value = light->find("phi"); value != light->end()) {
            infLight.phi = static_cast<float>(*value);
        }
        if (const auto& color = light->find("color"); color != light->end()) {
            infLight.color = {color->at(0), color->at(1), color->at(2)};
        }
        if (const auto& intensity = light->find("intensity"); intensity != light->end()) {
            infLight.intensity = *intensity;
        }
    }

    if (const auto& textures = jsonData.find("3d_textures"); textures != jsonData.end()) {
        for (const auto& texture : *textures) {
            uint32_t width = texture["width"];
            uint32_t height = texture["height"];
            uint32_t depth = 1;
            if (const auto d = texture.find("depth"); d != texture.end()) {
                depth = *d;
            }

            std::vector<ImageGenerator::Knot> knots;
            for (const auto& knot : texture["knots"]) {
                const auto& color = knot["color"];
                knots.push_back(
                    {knot["position"],
                     {color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, color[3] / 255.0f}});
            }
            std::vector<glm::vec4> data;
            if (texture["method"] == "gradient_x") {
                data = ImageGenerator::gradientHorizontal(width, height, depth, 4, knots);
            } else if (texture["method"] == "gradient_y") {
                data = ImageGenerator::gradientVertical(width, height, depth, 4, knots);
            }

            auto newTexture = context.createImage({
                .usage = rv::ImageUsage::Sampled,
                .extent = {width, height, 1},
                .imageType = vk::ImageType::e3D,
                .format = vk::Format::eR32G32B32A32Sfloat,
                .viewInfo = rv::ImageViewCreateInfo{},
                .samplerInfo =
                    rv::SamplerCreateInfo{
                        .addressMode = vk::SamplerAddressMode::eClampToEdge,
                    },
                .debugName = std::format("texture3d[{}]", scene.m_textures3d.size()),
            });

            rv::BufferHandle stagingBuffer = context.createBuffer({
                .usage = rv::BufferUsage::Staging,
                .memory = rv::MemoryUsage::Host,
                .size = width * height * 4 * sizeof(float),
                .debugName = "stagingBuffer",
            });
            stagingBuffer->copy(data.data());

            context.oneTimeSubmit([&](auto commandBuffer) {
                commandBuffer->transitionLayout(newTexture, vk::ImageLayout::eTransferDstOptimal);
                commandBuffer->copyBufferToImage(stagingBuffer, newTexture);
                commandBuffer->transitionLayout(newTexture,
                                                vk::ImageLayout::eShaderReadOnlyOptimal);
            });

            scene.m_textures3d.push_back(newTexture);
        }
        // if (const auto& dir = light->find("direction"); dir != light->end()) {
        //     scene.infiniteLightDir = glm::normalize(glm::vec3{dir->at(0), dir->at(1),
        //     dir->at(2)});
        // }
        // if (const auto& color = light->find("color"); color != light->end()) {
        //     scene.infiniteLightColor = {color->at(0), color->at(1), color->at(2)};
        // }
        // if (const auto& intensity = light->find("intensity"); intensity != light->end()) {
        //     scene.infiniteLightIntensity = *intensity;
        // }
    }
}
