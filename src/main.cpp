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
    assert(length > 0, "Failed to query the executable path.");
    return fs::path(filepath).remove_filename();
}

fs::path getShaderSourceDirectory() {
    return getExecutableDirectory().parent_path().parent_path().parent_path() / "shader";
}

fs::path getSpvDirectory() {
    return getExecutableDirectory() / "spv";
}

fs::path getSpvFilePath(const std::string& shaderFileName, const std::string& entryPoint = "main") {
    auto glslFile = getShaderSourceDirectory() / shaderFileName;
    std::string spvFileName =
        glslFile.stem().string() + "_" + entryPoint + glslFile.extension().string() + ".spv";
    return getSpvDirectory() / spvFileName;
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
    auto glslWriteTime = File::getLastWriteTimeWithIncludeFiles(glslFile);
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
        File::writeBinary(spvFile, spvCode);
    } else {
        File::readBinary(spvFile, spvCode);
    }
    return spvCode;
}

class HelloApp : public App {
public:
    HelloApp()
        : App({
              .width = 1920,
              .height = 1080,
              .title = "HelloCompute",
          }) {}

    void onStart() override {
        spdlog::info("Executable directory: {}", getExecutableDirectory().string());
        spdlog::info("Shader source directory: {}", getShaderSourceDirectory().string());
        spdlog::info("SPIR-V directory: {}", getSpvDirectory().string());

        loadVDB();

        renderImage = context.createImage({
            .usage = ImageUsage::GeneralStorage,
            .width = width,
            .height = height,
        });

        noiseImage = context.createImage({
            .usage = ImageUsage::GeneralStorage,
            .width = 512,
            .height = 512,
            .depth = 512,
        });

        std::vector<uint32_t> code = compileOrReadShader("render.comp");

        Shader compShader = context.createShader({
            .code = code,
            .stage = vk::ShaderStageFlagBits::eCompute,
        });

        descSet = context.createDescriptorSet({
            .shaders = {&compShader},
            .images = {{"outputImage", renderImage}},
        });

        pipeline = context.createComputePipeline({
            .computeShader = compShader,
            .descSetLayout = descSet.getLayout(),
            .pushSize = sizeof(PushConstants),
        });

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
        ImGui::Combo("Shape", &pushConstants.shape, "Cube\0Sphere");
        ImGui::Checkbox("Enable noise", reinterpret_cast<bool*>(&pushConstants.enableNoise));
        if (pushConstants.enableNoise) {
            ImGui::SliderInt("fBM octave", &pushConstants.octave, 1, 8);
            ImGui::SliderFloat("fBM gain", &pushConstants.gain, 0.0, 1.0);
            ImGui::SliderFloat("Noise freq.", &pushConstants.noiseFreq, 0.1, 10.0);
            ImGui::DragFloat4("Remap", pushConstants.remapValue, 0.01, -2.0, 2.0);
        }
        ImGui::ColorPicker4("Absorption coefficient", pushConstants.absorption);
        ImGui::SliderFloat("Light intensity", &pushConstants.lightIntensity, 0.0, 10.0);
        if (pushConstants.frame > 1) {
            ImGui::Text("GPU time: %f ms", gpuTimer.elapsedInMilli());
        }
        commandBuffer.bindDescriptorSet(descSet, pipeline);
        commandBuffer.bindPipeline(pipeline);
        commandBuffer.pushConstants(pipeline, &pushConstants);
        commandBuffer.beginTimestamp(gpuTimer);
        commandBuffer.dispatch(pipeline, width / 8, height / 8, 1);
        commandBuffer.endTimestamp(gpuTimer);
        commandBuffer.copyImage(renderImage.getImage(), getCurrentColorImage(),
                                vk::ImageLayout::eGeneral, vk::ImageLayout::ePresentSrcKHR, width,
                                height);
    }

    void loadVDB() {
        // Initialize the OpenVDB library
        openvdb::initialize();

        // Create a VDB file object
        openvdb::io::File file("../asset/nebula_doxia.filecache1_v3.0001.vdb");

        // Open the file
        file.open();

        // Read the "Alpha" grid (float data)
        openvdb::GridBase::Ptr grid = file.readGrid("initialValue");
        file.close();

        // Cast the grid to a FloatGrid pointer
        FloatGrid::Ptr floatGrid = openvdb::gridPtrCast<FloatGrid>(grid);

        // Create a 3D vector to hold the data

        openvdb::CoordBBox bbox = grid->evalActiveVoxelBoundingBox();
        std::cout << "Min: " << bbox.min() << "\n";
        std::cout << "Max: " << bbox.max() << "\n";
        // Min: [-182, -192, -197]
        // Max: [ 307, 287, 316 ]

        openvdb::Coord volumeAreaSize = bbox.max() - bbox.min();
        uint32_t voxelCount = volumeAreaSize.x() * volumeAreaSize.y() * volumeAreaSize.z();
        std::cout << "Voxel count: " << voxelCount << std::endl;

        std::vector<float> gridData(voxelCount);

        // Fill in the 3D vector with the "Alpha" grid data
        // Note: Replace X_SIZE, Y_SIZE, Z_SIZE with the actual dimensions of your grid
        CPUTimer timer;
        for (int32_t z = 0; z < volumeAreaSize.z(); ++z) {
            for (int32_t y = 0; y < volumeAreaSize.y(); ++y) {
                for (int32_t x = 0; x < volumeAreaSize.x(); ++x) {
                    openvdb::Coord xyz(x, y, z);
                    float value = floatGrid->getAccessor().getValue(xyz);
                    int32_t index =
                        x + volumeAreaSize.x() * y + (volumeAreaSize.x() * volumeAreaSize.y() * z);
                    gridData[index] = value;
                    // if (value.x() > 0.0) {
                    //     std::cout << "index: " << x << ", " << y << ", " << z
                    //               << " | value: " << value << std::endl;
                    // }
                }
            }
            std::cout << z << "/" << volumeAreaSize.z() << std::endl;
        }
        std::cout << "VDB loaded: " << timer.elapsedInMilli() << "ms" << std::endl;

        uint32_t byteSize = voxelCount * sizeof(float);
        std::cout << "Byte size: " << byteSize << std::endl;

        // DeviceBuffer stagingBuffer = context.createDeviceBuffer({
        //     .usage = BufferUsage::Staging,
        //     .size = byteSize,
        //     .data = gridData.data(),
        // });
        // std::cout << "Device buffer created" << std::endl;

        HostBuffer stagingBuffer = context.createHostBuffer({
            .usage = BufferUsage::Staging,
            .size = byteSize,
            .data = gridData.data(),
        });
        std::cout << "Staging buffer created" << std::endl;

        // TODO: fix aspect and layout
        vdbImage = context.createImage({
            .usage = ImageUsage::GeneralStorage,
            .width = static_cast<uint32_t>(volumeAreaSize.x()),
            .height = static_cast<uint32_t>(volumeAreaSize.y()),
            .depth = static_cast<uint32_t>(volumeAreaSize.z()),
            .format = vk::Format::eR32Sfloat,
            .type = vk::ImageType::e3D,
        });
        std::cout << "Texture created" << std::endl;

        vk::ImageSubresourceLayers subresourceLayers;
        subresourceLayers.setAspectMask(vk::ImageAspectFlagBits::eDepth);
        subresourceLayers.setMipLevel(0);
        subresourceLayers.setBaseArrayLayer(0);
        subresourceLayers.setLayerCount(1);

        vk::Extent3D extent;
        extent.setWidth(static_cast<uint32_t>(volumeAreaSize.x()));
        extent.setHeight(static_cast<uint32_t>(volumeAreaSize.y()));
        extent.setDepth(static_cast<uint32_t>(volumeAreaSize.z()));

        vk::BufferImageCopy region;
        region.imageSubresource = subresourceLayers;
        region.imageOffset = 0u;
        region.imageExtent = extent;

        context.oneTimeSubmit([&](vk::CommandBuffer commandBuffer) {
            Image::setImageLayout(commandBuffer, vdbImage.getImage(),
                                  vk::ImageLayout::eTransferDstOptimal);
            commandBuffer.copyBufferToImage(stagingBuffer.getBuffer(), vdbImage.getImage(),
                                            vk::ImageLayout::eTransferDstOptimal, region);
        });
        std::cout << "Texture filled" << std::endl;
    }

    Image renderImage;
    Image noiseImage;
    Image vdbImage;
    DescriptorSet descSet;
    ComputePipeline pipeline;
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
