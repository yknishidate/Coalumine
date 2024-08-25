#pragma once

#include <reactive/reactive.hpp>

class Scene;
struct Mesh;

class LoaderObj {
public:
    static void loadFromFile(Scene& scene,
                             const rv::Context& context,
                             const std::filesystem::path& filepath);

    static void loadMesh(Mesh& mesh,
                         const rv::Context& context,
                         const std::filesystem::path& filepath);
};
