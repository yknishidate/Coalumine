#pragma once
#include <filesystem>

namespace fs = std::filesystem;

fs::path getExecutableDirectory();

inline fs::path getShaderSourceDirectory() {
    // executable:  project/build/preset/config/*.exe
    // shader:     project/shader/*.glsl
    const auto projectRoot =
        getExecutableDirectory().parent_path().parent_path().parent_path().parent_path();
    return projectRoot / "shader";
}

inline fs::path getSpvDirectory() {
    return getExecutableDirectory() / "spv";
}

inline fs::path getSpvFilePath(const std::string& shaderFileName, const std::string& entryPoint) {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    std::string spvFileName =
        glslFile.stem().string() + "_" + entryPoint + glslFile.extension().string() + ".spv";
    return getSpvDirectory() / spvFileName;
}

inline fs::path getAssetDirectory() {
    return getExecutableDirectory() / "asset";
}
