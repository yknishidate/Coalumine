#include "App.hpp"

#define NOMINMAX
#include <Windows.h>
#undef near
#undef far
#undef RGB

namespace fs = std::filesystem;
fs::path getExecutableDirectory() {
    TCHAR filepath[1024];
    auto length = GetModuleFileName(NULL, filepath, 1024);
    assert(length > 0, "Failed to query the executable path.");
    return fs::path(filepath).remove_filename();
}

fs::path getShaderSourceDirectory() {
    return getExecutableDirectory().parent_path().parent_path().parent_path() / "shader";
}

fs::path getSpvDirectory() {
    return getExecutableDirectory() / "spv";
}

std::vector<std::string> getAllIncludedFiles(const std::string& code) {
    std::string includePrefix = "#include \"";
    std::string includeSuffix = "\"";
    std::string::size_type startPos = 0;
    std::vector<std::string> includes;
    while (true) {
        auto includeStartPos = code.find(includePrefix, startPos);
        if (includeStartPos == std::string::npos)
            break;

        auto includeEndPos = code.find(includeSuffix, includeStartPos + includePrefix.length());
        if (includeEndPos == std::string::npos)
            break;

        std::string include =
            code.substr(includeStartPos + includePrefix.length(),
                        includeEndPos - (includeStartPos + includePrefix.length()));
        includes.push_back(include);
        startPos = includeEndPos + includeSuffix.length();
    }
    return includes;
}

fs::path getSpvFilePath(const std::string& shaderFileName, const std::string& entryPoint = "main") {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    std::string spvFileName =
        glslFile.stem().string() + "_" + entryPoint + glslFile.extension().string() + ".spv";
    return getSpvDirectory() / spvFileName;
}

inline bool isNewerThan(const fs::path& a, const fs::path& b) {
    return fs::last_write_time(a) > fs::last_write_time(b);
}

fs::file_time_type getLastWriteTime(const std::string& fileName) {
    fs::path glslDirectory = getShaderSourceDirectory();
    auto writeTime = fs::last_write_time(glslDirectory / fileName);
    std::string code = File::readFile((glslDirectory / fileName).string());
    for (auto& include : getAllIncludedFiles(code)) {
        auto includeWriteTime = getLastWriteTime(include);
        if (includeWriteTime > writeTime) {
            writeTime = includeWriteTime;
        }
    }
    return writeTime;
}

bool shouldRecompile(const std::string& shaderFileName, const std::string& entryPoint = "main") {
    if (shaderFileName.empty()) {
        return false;
    }
    fs::create_directory(getSpvDirectory());
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    if (!fs::exists(glslFile)) {
        return false;
    }
    auto spvFile = getSpvFilePath(shaderFileName, entryPoint);
    auto glslWriteTime = getLastWriteTime(shaderFileName);
    return !fs::exists(spvFile) || glslWriteTime > fs::last_write_time(spvFile);
}

std::vector<uint32_t> compileOrReadShader(const std::string& shaderFileName,
                                          const std::string& entryPoint = "main") {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    auto spvFile = getSpvFilePath(shaderFileName, entryPoint);
    std::vector<uint32_t> spvCode;
    spdlog::info("Check last write time of the shader: {}", shaderFileName);
    if (shouldRecompile(shaderFileName, entryPoint)) {
        spdlog::info("Compile shader: {}", spvFile.string());
        spvCode = Compiler::compileToSPV(glslFile.string(), {{entryPoint, "main"}});
        File::writeBinary(spvFile.string(), spvCode);
    } else {
        File::readBinary(spvFile.string(), spvCode);
    }
    return spvCode;
}

class HelloApp : public App {
public:
    HelloApp()
        : App({
              .width = 1280,
              .height = 720,
              .title = "HelloCompute",
          }) {}

    void onStart() override {
        spdlog::info("Executable directory: {}", getExecutableDirectory().string());
        spdlog::info("Shader source directory: {}", getShaderSourceDirectory().string());
        spdlog::info("SPIR-V directory: {}", getSpvDirectory().string());

        image = context.createImage({
            .usage = ImageUsage::GeneralStorage,
            .width = width,
            .height = height,
        });

        std::vector<uint32_t> code = compileOrReadShader("render.comp");

        Shader compShader = context.createShader({
            .code = code,
            .stage = vk::ShaderStageFlagBits::eCompute,
        });

        descSet = context.createDescriptorSet({
            .shaders = {&compShader},
            .images = {{"outputImage", image}},
        });

        pipeline = context.createComputePipeline({
            .computeShader = compShader,
            .descSetLayout = descSet.getLayout(),
            .pushSize = sizeof(int),
        });
    }

    void onUpdate() override { frame++; }

    void onRender(const CommandBuffer& commandBuffer) override {
        ImGui::SliderInt("Test slider", &testInt, 0, 100);
        commandBuffer.bindDescriptorSet(descSet, pipeline);
        commandBuffer.bindPipeline(pipeline);
        commandBuffer.pushConstants(pipeline, &frame);
        commandBuffer.dispatch(pipeline, width, height, 1);
        commandBuffer.copyImage(image.getImage(), getCurrentColorImage(), vk::ImageLayout::eGeneral,
                                vk::ImageLayout::ePresentSrcKHR, width, height);
    }

    Image image;
    DescriptorSet descSet;
    ComputePipeline pipeline;
    int testInt = 0;
    int frame = 0;
};

int main() {
    try {
        HelloApp app{};
        app.run();
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}
