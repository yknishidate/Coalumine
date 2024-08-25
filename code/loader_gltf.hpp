#pragma once

#include <tiny_gltf.h>
#include <reactive/reactive.hpp>

class Scene;

class LoaderGltf {
public:
    static void loadFromFile(Scene& scene,
                             const rv::Context& context,
                             const std::filesystem::path& filepath);

    static void loadNodes(Scene& scene, const rv::Context& context, tinygltf::Model& gltfModel);

    static void loadMeshes(Scene& scene, const rv::Context& context, tinygltf::Model& gltfModel);

    static void loadMaterials(Scene& scene, const rv::Context& context, tinygltf::Model& gltfModel);

    static void loadAnimation(Scene& scene,
                              const rv::Context& context,
                              const tinygltf::Model& model);
};
