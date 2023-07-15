#include "../shader/share.h"
#include "App.hpp"

#define NOMINMAX
#include <Windows.h>
#undef near
#undef far
#undef RGB

#define IMATH_HALF_NO_LOOKUP_TABLE
#include <openvdb/openvdb.h>

using FloatGrid = openvdb::FloatGrid;
using Vec3SGrid = openvdb::Vec3SGrid;

namespace fs = std::filesystem;
fs::path getExecutableDirectory() {
    TCHAR filepath[1024];
    auto length = GetModuleFileName(NULL, filepath, 1024);
    assert(length > 0 && "Failed to query the executable path.");
    return fs::path(filepath).remove_filename();
}

fs::path getShaderSourceDirectory() {
    return getExecutableDirectory().parent_path().parent_path().parent_path() / "shader";
}

fs::path getSpvDirectory() {
    return getExecutableDirectory() / "spv";
}

fs::path getSpvFilePath(const std::string& shaderFileName, const std::string& entryPoint) {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    std::string spvFileName =
        glslFile.stem().string() + "_" + entryPoint + glslFile.extension().string() + ".spv";
    return getSpvDirectory() / spvFileName;
}

bool shouldRecompile(const std::string& shaderFileName, const std::string& entryPoint) {
    if (shaderFileName.empty()) {
        return false;
    }
    fs::create_directory(getSpvDirectory());
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    if (!fs::exists(glslFile)) {
        return false;
    }
    auto spvFile = getSpvFilePath(shaderFileName, entryPoint);
    auto glslWriteTime = File::getLastWriteTimeWithIncludeFiles(glslFile);
    return !fs::exists(spvFile) || glslWriteTime > fs::last_write_time(spvFile);
}

bool shouldRecompile(const std::string& shaderFileName,
                     const std::vector<std::string>& entryPoints) {
    for (auto& entryPoint : entryPoints) {
        if (shouldRecompile(shaderFileName, entryPoint)) {
            return true;
        }
    }
    return false;
}

std::vector<uint32_t> compileShader(const std::string& shaderFileName,
                                    const std::string& entryPoint) {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    auto spvFile = getSpvFilePath(shaderFileName, entryPoint);
    std::vector<uint32_t> spvCode;
    spdlog::info("Compile shader: {}", spvFile.string());
    spvCode = Compiler::compileToSPV(glslFile.string(), {{entryPoint, "main"}});
    File::writeBinary(spvFile, spvCode);
    return spvCode;
}

std::vector<uint32_t> readShader(const std::string& shaderFileName, const std::string& entryPoint) {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    auto spvFile = getSpvFilePath(shaderFileName, entryPoint);
    std::vector<uint32_t> spvCode;
    File::readBinary(spvFile, spvCode);
    return spvCode;
}

class HelloApp : public App {
public:
    HelloApp()
        : App({
              .width = 1920,
              .height = 1080,
              .title = "HelloCompute",
              .enableValidation = true,
          }) {}

    void createPipelines() {
        std::vector<Shader> shaders(entryPoints.size());
        std::vector<const Shader*> shaderPtrs(entryPoints.size());
        for (int i = 0; i < entryPoints.size(); i++) {
            std::vector<uint32_t> code;
            if (shouldRecompile(shaderFileName, entryPoints[i])) {
                code = compileShader(shaderFileName, entryPoints[i]);
            } else {
                code = readShader(shaderFileName, entryPoints[i]);
            }
            shaders[i] = context.createShader({
                .code = code,
                .stage = vk::ShaderStageFlagBits::eCompute,
            });
            shaderPtrs[i] = &shaders[i];
        }

        descSet = context.createDescriptorSet({
            .shaders = shaderPtrs,
            .images =
                {
                    {"baseImage", baseImage},
                    {"volumeImage", volumeImage},
                    {"bloomImage", bloomImage},
                    {"finalImage", finalImage},
                },
        });

        for (int i = 0; i < entryPoints.size(); i++) {
            pipelines[entryPoints[i]] = context.createComputePipeline({
                .computeShader = shaders[i],
                .descSetLayout = descSet.getLayout(),
                .pushSize = sizeof(PushConstants),
            });
        }
    }

    void onStart() override {
        spdlog::info("Executable directory: {}", getExecutableDirectory().string());
        spdlog::info("Shader source directory: {}", getShaderSourceDirectory().string());
        spdlog::info("SPIR-V directory: {}", getSpvDirectory().string());

        loadVDB();

        baseImage = context.createImage({
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eTransferSrc,
            .initialLayout = vk::ImageLayout::eGeneral,
            .aspect = vk::ImageAspectFlagBits::eColor,
            .width = width,
            .height = height,
            .format = vk::Format::eR32G32B32A32Sfloat,
        });

        bloomImage = context.createImage({
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eTransferSrc,
            .initialLayout = vk::ImageLayout::eGeneral,
            .aspect = vk::ImageAspectFlagBits::eColor,
            .width = width,
            .height = height,
            .format = vk::Format::eR32G32B32A32Sfloat,
        });

        finalImage = context.createImage({
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eTransferSrc,
            .initialLayout = vk::ImageLayout::eGeneral,
            .aspect = vk::ImageAspectFlagBits::eColor,
            .width = width,
            .height = height,
        });

        createPipelines();

        camera = OrbitalCamera{this, width, height};

        gpuTimer = context.createGPUTimer({});
    }

    void onUpdate() override {
        camera.processInput();
        pushConstants.frame++;
        pushConstants.invView = camera.getInvView();
        pushConstants.invProj = camera.getInvProj();
    }

    void onRender(const CommandBuffer& commandBuffer) override {
        static int imageIndex = 0;
        static int blurIteration = 32;
        ImGui::Combo("Image", &imageIndex, "Render\0Bloom");
        ImGui::Checkbox("Enable noise", reinterpret_cast<bool*>(&pushConstants.enableNoise));
        ImGui::ColorPicker4("Absorption color", pushConstants.absorption);
        ImGui::SliderFloat("Absorption intensity", &pushConstants.absorptionIntensity, 0.0, 10.0);
        ImGui::SliderFloat("Scattering intensity", &pushConstants.scatterIntensity, 0.0, 10.0);
        ImGui::SliderFloat("Emission intensity", &pushConstants.emissionIntensity, 0.0, 50.0);

        ImGui::SliderFloat("Light intensity", &pushConstants.lightIntensity, 0.0, 10.0);

        // Post effects
        ImGui::Checkbox("Enable tone mapping",
                        reinterpret_cast<bool*>(&pushConstants.enableToneMapping));
        ImGui::Checkbox("Enable gamma correction",
                        reinterpret_cast<bool*>(&pushConstants.enableGammaCorrection));

        // Bloom
        ImGui::Checkbox("Enable bloom", reinterpret_cast<bool*>(&pushConstants.enableBloom));
        if (pushConstants.enableBloom) {
            ImGui::SliderFloat("Bloom intensity", &pushConstants.bloomIntensity, 0.0, 10.0);
            ImGui::SliderFloat("Bloom threshold", &pushConstants.bloomThreshold, 0.0, 2.0);
            ImGui::SliderInt("Blur iteration", &blurIteration, 0, 64);
            ImGui::SliderInt("Blur size", &pushConstants.blurSize, 0, 64);
        }

        // Flow noise
        ImGui::Checkbox("Enable flow noise",
                        reinterpret_cast<bool*>(&pushConstants.enableFlowNoise));
        if (pushConstants.enableFlowNoise) {
            ImGui::SliderFloat("Flow speed", &pushConstants.flowSpeed, 0.0, 0.2);
        }

        if (pushConstants.frame > 1) {
            ImGui::Text("GPU time: %f ms", gpuTimer.elapsedInMilli());
        }
        if (shouldRecompile(shaderFileName, entryPoints)) {
            try {
                createPipelines();
            } catch (const std::exception& e) {
                spdlog::error(e.what());
            }
        }

        commandBuffer.bindDescriptorSet(descSet, pipelines["main_base"]);

        commandBuffer.beginTimestamp(gpuTimer);

        // Base rendering
        commandBuffer.bindPipeline(pipelines["main_base"]);
        commandBuffer.pushConstants(pipelines["main_base"], &pushConstants);
        commandBuffer.dispatch(pipelines["main_base"], width / 8, height / 8, 1);

        commandBuffer.imageBarrier(
            vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
            {}, baseImage, vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);

        commandBuffer.imageBarrier(
            vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
            {}, bloomImage, vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);

        // Blur
        if (pushConstants.enableBloom) {
            for (int i = 0; i < blurIteration; i++) {
                commandBuffer.bindPipeline(pipelines["main_blur"]);
                commandBuffer.pushConstants(pipelines["main_blur"], &pushConstants);
                commandBuffer.dispatch(pipelines["main_blur"], width / 8, height / 8, 1);

                commandBuffer.imageBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                           vk::PipelineStageFlagBits::eComputeShader, {},
                                           bloomImage, vk::AccessFlagBits::eShaderWrite,
                                           vk::AccessFlagBits::eShaderRead);
            }
        }

        // Composite
        commandBuffer.bindPipeline(pipelines["main_composite"]);
        commandBuffer.pushConstants(pipelines["main_composite"], &pushConstants);
        commandBuffer.dispatch(pipelines["main_composite"], width / 8, height / 8, 1);

        commandBuffer.endTimestamp(gpuTimer);

        commandBuffer.copyImage(finalImage.getImage(), getCurrentColorImage(),
                                vk::ImageLayout::eGeneral, vk::ImageLayout::ePresentSrcKHR, width,
                                height);
    }

    void loadVDB() {
        openvdb::initialize();

        openvdb::io::File file("../asset/nebula_doxia.filecache1_v13.0001.vdb");
        file.open();

        openvdb::GridBase::Ptr grid = file.readGrid("emission");
        file.close();

        // Cast the grid to a FloatGrid pointer
        FloatGrid::Ptr floatGrid = openvdb::gridPtrCast<FloatGrid>(grid);

        // Create a 3D vector to hold the data
        openvdb::CoordBBox bbox = grid->evalActiveVoxelBoundingBox();
        std::cout << "Min: " << bbox.min() << "\n";
        std::cout << "Max: " << bbox.max() << "\n";

        openvdb::Coord volumeAreaSize = bbox.max() - bbox.min();
        uint32_t voxelCount = volumeAreaSize.x() * volumeAreaSize.y() * volumeAreaSize.z();
        std::cout << "Voxel count: " << voxelCount << std::endl;

        pushConstants.volumeSize[0] = volumeAreaSize.x() * 0.01f;
        pushConstants.volumeSize[1] = volumeAreaSize.y() * 0.01f;
        pushConstants.volumeSize[2] = volumeAreaSize.z() * 0.01f;
        pushConstants.volumeSize[3] = 1.0f;

        std::vector<float> gridData(voxelCount);
        CPUTimer timer;
        for (int32_t z = 0; z < volumeAreaSize.z(); ++z) {
            for (int32_t y = 0; y < volumeAreaSize.y(); ++y) {
                for (int32_t x = 0; x < volumeAreaSize.x(); ++x) {
                    openvdb::Coord xyz(x, y, z);
                    float value = floatGrid->getAccessor().getValue(bbox.min() + xyz);
                    int32_t index =
                        x + volumeAreaSize.x() * y + (volumeAreaSize.x() * volumeAreaSize.y() * z);
                    gridData[index] = value;
                }
            }
            std::cout << z << "/" << volumeAreaSize.z() << std::endl;
        }
        std::cout << "VDB loaded: " << timer.elapsedInMilli() << "ms" << std::endl;

        uint32_t byteSize = voxelCount * sizeof(float);
        std::cout << "Byte size: " << byteSize << std::endl;

        HostBuffer stagingBuffer = context.createHostBuffer({
            .usage = BufferUsage::Staging,
            .size = byteSize,
            .data = gridData.data(),
        });
        std::cout << "Staging buffer created" << std::endl;

        volumeImage = context.createImage({
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
            .initialLayout = vk::ImageLayout::eTransferDstOptimal,
            .aspect = vk::ImageAspectFlagBits::eColor,
            .width = static_cast<uint32_t>(volumeAreaSize.x()),
            .height = static_cast<uint32_t>(volumeAreaSize.y()),
            .depth = static_cast<uint32_t>(volumeAreaSize.z()),
            .format = vk::Format::eR32Sfloat,
        });
        std::cout << "Texture created" << std::endl;

        vk::ImageSubresourceLayers subresourceLayers;
        subresourceLayers.setAspectMask(vk::ImageAspectFlagBits::eColor);
        subresourceLayers.setMipLevel(0);
        subresourceLayers.setBaseArrayLayer(0);
        subresourceLayers.setLayerCount(1);

        vk::Extent3D extent;
        extent.setWidth(static_cast<uint32_t>(volumeAreaSize.x()));
        extent.setHeight(static_cast<uint32_t>(volumeAreaSize.y()));
        extent.setDepth(static_cast<uint32_t>(volumeAreaSize.z()));

        vk::BufferImageCopy region;
        region.imageSubresource = subresourceLayers;
        region.imageExtent = extent;

        context.oneTimeSubmit([&](vk::CommandBuffer commandBuffer) {
            // Image::setImageLayout(commandBuffer, volumeImage.getImage(),
            //                       vk::ImageLayout::eTransferDstOptimal);
            commandBuffer.copyBufferToImage(stagingBuffer.getBuffer(), volumeImage.getImage(),
                                            vk::ImageLayout::eTransferDstOptimal, region);
            Image::setImageLayout(commandBuffer, volumeImage.getImage(), vk::ImageLayout::eGeneral);
        });
        std::cout << "Texture filled" << std::endl;
    }

    std::string shaderFileName = "render.comp";
    std::vector<std::string> entryPoints = {"main_base", "main_blur", "main_composite"};

    Image baseImage;
    Image volumeImage;
    Image bloomImage;
    Image finalImage;

    DescriptorSet descSet;
    std::unordered_map<std::string, ComputePipeline> pipelines;
    OrbitalCamera camera;
    PushConstants pushConstants;
    GPUTimer gpuTimer;
};

int main() {
    try {
        HelloApp app{};
        app.run();
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}
