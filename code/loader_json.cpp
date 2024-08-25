#include "loader_json.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

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

    // "objects"セクションのパース
    for (const auto& object : jsonData["objects"]) {
        Node node;
        node.meshIndex = object["mesh_index"];
        if (const auto& itr = object.find("material_index"); itr != object.end()) {
            node.materialIndex = *itr;
        }
        if (const auto& itr = object.find("translation"); itr != object.end()) {
            node.translation = {itr->at(0), itr->at(1), itr->at(2)};
        }
        if (const auto& itr = object.find("scale"); itr != object.end()) {
            node.scale = {itr->at(0), itr->at(1), itr->at(2)};
        }
        if (const auto& itr = object.find("rotation"); itr != object.end()) {
            node.rotation = {glm::vec3{itr->at(0), itr->at(1), itr->at(2)}};
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

    //// "camera"セクションのパース
    // std::string cameraType = jsonData["camera"]["type"];
    // float fovY = jsonData["camera"]["fov_y"];
    // float distance = jsonData["camera"]["distance"];
    // float phi = jsonData["camera"]["phi"];
    // float theta = jsonData["camera"]["theta"];

    // "environment_light"セクションのパース
    if (const auto& light = jsonData.find("environment_light"); light != jsonData.end()) {
        if (const auto& tex = light->find("texture"); tex != light->end()) {
            std::filesystem::path texPath = filepath.parent_path() / *tex;
            scene.loadDomeLightTexture(context, texPath);
        }
    }
}
