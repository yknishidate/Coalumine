#pragma once

#include <reactive/reactive.hpp>

class Scene;

class LoaderAlembic {
public:
    static void loadFromFile(Scene& scene,
                             const rv::Context& context,
                             const std::filesystem::path& filepath);
};
