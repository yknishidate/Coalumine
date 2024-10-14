#pragma once

#include <spdlog/spdlog.h>
#include <cassert>
#include <reactive/Compiler/Compiler.hpp>

#include "filepath.hpp"

inline bool shouldRecompile(const std::string& shaderFileName, const std::string& entryPoint) {
    assert(!shaderFileName.empty());
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    if (!fs::exists(glslFile)) {
        spdlog::warn("GLSL file doesn't exists: {}", glslFile.string());
        return false;
    }
    auto spvFile = getSpvFilePath(shaderFileName, entryPoint);
    auto glslWriteTime = rv::Compiler::getLastWriteTimeWithIncludeFiles(glslFile);
    return !fs::exists(spvFile) || glslWriteTime > fs::last_write_time(spvFile);
}

inline std::vector<uint32_t> compileShader(const std::string& shaderFileName,
                                           const std::string& entryPoint) {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    auto spvFile = getSpvFilePath(shaderFileName, entryPoint);
    spdlog::info("Compile shader: {}", spvFile.string());
    std::vector<uint32_t> spvCode = rv::Compiler::compileToSPV(glslFile.string());
    rv::File::writeBinary(spvFile, spvCode);
    return spvCode;
}

inline std::vector<uint32_t> readShader(const std::string& shaderFileName,
                                        const std::string& entryPoint) {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    auto spvFile = getSpvFilePath(shaderFileName, entryPoint);
    std::vector<uint32_t> spvCode;
    rv::File::readBinary(spvFile, spvCode);
    return spvCode;
}
