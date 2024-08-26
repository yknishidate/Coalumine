#include "loader_json.hpp"

#include <fstream>
#include <random>

#include <nlohmann/json.hpp>

#include "image_generator.hpp"
#include "loader_gltf.hpp"
#include "loader_obj.hpp"
#include "scene.hpp"

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
    const int materialOffset = static_cast<int>(scene.materials.size());
    const int meshOffset = static_cast<int>(scene.meshes.size());

    // "objects"セクションのパース
    for (const auto& object : jsonData["objects"]) {
        Node node;
        node.meshIndex = meshOffset + object["mesh_index"];
        if (const auto& itr = object.find("material_index"); itr != object.end()) {
            node.materialIndex = materialOffset + *itr;
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
        scene.nodes.push_back(node);
    }

    // "meshes"セクションのパース
    const auto& meshes = jsonData["meshes"];
    scene.meshes.reserve(meshes.size());
    for (const auto& mesh : meshes) {
        std::filesystem::path objPath = filepath.parent_path() / mesh["obj"];

        scene.meshes.push_back({});
        LoaderObj::loadMesh(scene.meshes.back(), context, objPath);
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
        scene.materials.push_back(mat);
    }

    if (const auto& defaultMat = jsonData.find("default_material"); defaultMat != jsonData.end()) {
        if (defaultMat->at("type") == "random") {
            const auto& matIndices = defaultMat->at("material_indices");

            std::mt19937 rng(defaultMat->at("seed"));
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            for (auto& mesh : scene.meshes) {
                if (mesh.materialIndex == -1) {
                    double randomValue = dist(rng);
                    int indexIndex = static_cast<int>(std::floor(randomValue * matIndices.size()));
                    mesh.materialIndex = materialOffset + matIndices[indexIndex];
                }
            }
        }
    }

    //// "camera"セクションのパース
    // std::string cameraType = jsonData["camera"]["type"];
    // float fovY = jsonData["camera"]["fov_y"];
    // float distance = jsonData["camera"]["distance"];
    // float phi = jsonData["camera"]["phi"];
    // float theta = jsonData["camera"]["theta"];

    // "environment_light"セクションのパース
    if (const auto& light = jsonData.find("environment_light"); light != jsonData.end()) {
        const auto& type = light->at("type");
        if (type == "texture") {
            std::filesystem::path texPath = filepath.parent_path() / light->at("texture");
            scene.loadEnvLightTexture(context, texPath);
            scene.useEnvLightTexture = true;
        } else if (type == "procedural") {
            auto params = light->at("procedural_parameters");
            if (params["method"] == "gradient_horizontal") {
                uint32_t width = params["width"];
                uint32_t height = params["height"];

                std::vector<ImageGenerator::Knot> knots;
                for (const auto& knot : params["knots"]) {
                    const auto& color = knot["color"];
                    knots.push_back({knot["position"],
                                     {color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f}});
                }

                const auto& data = ImageGenerator::gradientHorizontal(width, height, 4, knots);
                scene.createEnvLightTexture(context, static_cast<const float*>(&data[0][0]),  //
                                            width, height, 4);
                scene.useEnvLightTexture = true;
            }
        } else if (type == "solid") {
            const float dummy = 0.0f;
            scene.createEnvLightTexture(context, &dummy, 1, 1, 4);

            scene.useEnvLightTexture = false;
            scene.envLightColor = {light->at("color")[0], light->at("color")[1],
                                   light->at("color")[2]};
        }
    }

    if (const auto& light = jsonData.find("infinite_light"); light != jsonData.end()) {
        if (const auto& dir = light->find("direction"); dir != light->end()) {
            scene.infiniteLightDir = glm::normalize(glm::vec3{dir->at(0), dir->at(1), dir->at(2)});
        }
        if (const auto& color = light->find("color"); color != light->end()) {
            scene.infiniteLightColor = {color->at(0), color->at(1), color->at(2)};
        }
        if (const auto& intensity = light->find("intensity"); intensity != light->end()) {
            scene.infiniteLightIntensity = *intensity;
        }
    }
}
