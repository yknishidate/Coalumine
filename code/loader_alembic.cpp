#include "loader_alembic.hpp"
#include "scene.hpp"

#include <Imath/ImathVec.h>

#include <Alembic/AbcCoreFactory/All.h>
#include <Alembic/AbcGeom/All.h>

using Alembic::Abc::IArchive;
using Alembic::Abc::IObject;
using Alembic::AbcGeom::IN3fGeomParam;
using Alembic::AbcGeom::Int32ArraySamplePtr;
using Alembic::AbcGeom::IPolyMesh;
using Alembic::AbcGeom::IPolyMeshSchema;
using Alembic::AbcGeom::ISampleSelector;
using Alembic::AbcGeom::IV2fGeomParam;
using Alembic::AbcGeom::IXform;
using Alembic::AbcGeom::IXformSchema;
using Alembic::AbcGeom::kWrapExisting;
using Alembic::AbcGeom::N3fArraySamplePtr;
using Alembic::AbcGeom::P3fArraySamplePtr;
using Alembic::AbcGeom::UInt32ArraySamplePtr;
using Alembic::AbcGeom::V2fArraySamplePtr;
using Alembic::AbcGeom::XformSample;

namespace {
void loadVerticesAndIndices(const IPolyMeshSchema& meshSchema,
                            size_t frame,
                            std::vector<rv::Vertex>& _vertices,
                            std::vector<uint32_t>& _indices,
                            rv::AABB& aabb) {
    // 頂点座標
    IPolyMeshSchema::Sample meshSample;
    meshSchema.get(meshSample, frame);
    P3fArraySamplePtr positions = meshSample.getPositions();
    size_t numVertices = positions->size();

    // 法線
    N3fArraySamplePtr normals;
    if (meshSchema.getNormalsParam().valid()) {
        IN3fGeomParam normalsParam = meshSchema.getNormalsParam();
        IN3fGeomParam::Sample normalsSample;
        normalsParam.getExpanded(normalsSample, frame);
        normals = normalsSample.getVals();
    }

    //// テクスチャ座標
    // V2fArraySamplePtr uv;
    // bool isUvIndexed = false;
    // if (meshSchema.getUVsParam().valid()) {
    //     IV2fGeomParam uvsParam = meshSchema.getUVsParam();
    //     isUvIndexed = uvsParam.isIndexed();
    //     IV2fGeomParam::Sample uvSample;
    //     uvsParam.getExpanded(uvSample, frame);
    //     uv = uvSample.getVals();
    // }

    // インデックス（フェイスの頂点インデックス情報）
    Int32ArraySamplePtr indices = meshSample.getFaceIndices();
    size_t numIndices = indices->size();
    _indices.resize(numIndices);
    for (size_t i = 0; i < numIndices; ++i) {
        _indices[i] = (*indices)[i];
    }

    _vertices.resize(numVertices);

    glm::vec3 aabbMin;
    aabbMin.x = (*positions)[0].x;
    aabbMin.y = (*positions)[0].x;
    aabbMin.z = (*positions)[0].x;
    glm::vec3 aabbMax = aabbMin;

    // 頂点情報をrv::Vertexに格納
    for (size_t i = 0; i < numVertices; ++i) {
        _vertices[i].pos = glm::vec3((*positions)[i].x, (*positions)[i].y, (*positions)[i].z);
        aabbMin = glm::min(aabbMin, _vertices[i].pos);
        aabbMax = glm::max(aabbMax, _vertices[i].pos);
    }
    aabb = rv::AABB{aabbMin, aabbMax};

    // 法線データがある場合の処理
    if (normals) {
        switch (meshSchema.getNormalsParam().getScope()) {
            case Alembic::AbcGeom::kFacevaryingScope: {
                // 法線は各フェイスの頂点ごとに異なる（フェイス頂点に沿った法線）
                assert(numIndices == normals->getDimensions().numPoints());
                for (size_t i = 0; i < numIndices; ++i) {
                    size_t vi = _indices[i];
                    _vertices[vi].normal =
                        glm::vec3((*normals)[i].x, (*normals)[i].y, (*normals)[i].z);
                }
                break;
            }
            case Alembic::AbcGeom::kVaryingScope:
            case Alembic::AbcGeom::kVertexScope: {
                // 法線は頂点ごとに一つ（通常の頂点ごとの法線）
                for (size_t i = 0; i < numVertices; ++i) {
                    _vertices[i].normal =
                        glm::vec3((*normals)[i].x, (*normals)[i].y, (*normals)[i].z);
                }
                break;
            }
            default:
                break;
        }
    }
}

void processMesh(Scene& scene, const rv::Context& context, IPolyMesh& mesh) {
    IPolyMeshSchema& meshSchema = mesh.getSchema();

    size_t numSamples = meshSchema.getNumSamples();
    spdlog::info("numSamples: {}", numSamples);

    size_t meshIndex = scene.meshes.size();
    Mesh _mesh{};

    _mesh.keyFrames.resize(numSamples);
    for (size_t i = 0; i < numSamples; i++) {
        std::vector<rv::Vertex> vertices;
        std::vector<uint32_t> indices;
        rv::AABB aabb;
        loadVerticesAndIndices(meshSchema, i, vertices, indices, aabb);
        if (indices.empty()) {
            continue;
        }

        // Mesh追加
        _mesh.keyFrames[i].vertexBuffer = context.createBuffer({
            .usage = rv::BufferUsage::AccelVertex,
            .size = sizeof(rv::Vertex) * vertices.size(),
            .debugName = std::format("vertexBuffers[{}]", meshIndex).c_str(),
        });
        _mesh.keyFrames[i].indexBuffer = context.createBuffer({
            .usage = rv::BufferUsage::AccelIndex,
            .size = sizeof(uint32_t) * indices.size(),
            .debugName = std::format("indexBuffers[{}]", meshIndex).c_str(),
        });
        _mesh.aabb = aabb;

        context.oneTimeSubmit([&](auto commandBuffer) {
            commandBuffer->copyBuffer(_mesh.keyFrames[i].vertexBuffer, vertices.data());
            commandBuffer->copyBuffer(_mesh.keyFrames[i].indexBuffer, indices.data());
        });

        _mesh.keyFrames[i].vertexCount = static_cast<uint32_t>(vertices.size());
        _mesh.keyFrames[i].triangleCount = static_cast<uint32_t>(indices.size() / 3);
    }

    scene.meshes.push_back(_mesh);
}

// 再帰的にXformやメッシュを探索する関数
void processObjectRecursive(Scene& scene,
                            const rv::Context& context,
                            const IObject& object,
                            int parentNodeIndex,
                            uint32_t depth) {
    for (size_t i = 0; i < object.getNumChildren(); ++i) {
        IObject child = object.getChild(i);

        // Xformが見つかった場合
        if (IXform::matches(child.getHeader())) {
            std::cout << "Found Xform node: " << child.getName() << std::endl;
            IXform xform(child, kWrapExisting);
            IXformSchema xformSchema = xform.getSchema();

            size_t numSamples = xformSchema.getNumSamples();

            Node _node;
            _node.parentNode = &scene.nodes[parentNodeIndex];
            if (numSamples == 1) {
                XformSample sample;
                xformSchema.get(sample, i);

                _node.translation.x = static_cast<float>(sample.getTranslation().x);
                _node.translation.y = static_cast<float>(sample.getTranslation().y);
                _node.translation.z = static_cast<float>(sample.getTranslation().z);
                _node.scale.x = static_cast<float>(sample.getScale().x);
                _node.scale.y = static_cast<float>(sample.getScale().y);
                _node.scale.z = static_cast<float>(sample.getScale().z);
                glm::vec3 rot;
                rot.x = glm::radians(static_cast<float>(sample.getXRotation()));
                rot.y = glm::radians(static_cast<float>(sample.getYRotation()));
                rot.z = glm::radians(static_cast<float>(sample.getZRotation()));
                _node.rotation = glm::quat(rot);
            } else {
                _node.keyFrames.resize(numSamples);
                for (size_t j = 0; j < numSamples; j++) {
                    XformSample sample;
                    xformSchema.get(sample, j);

                    auto& keyFrame = _node.keyFrames[j];
                    keyFrame.translation.x = static_cast<float>(sample.getTranslation().x);
                    keyFrame.translation.y = static_cast<float>(sample.getTranslation().y);
                    keyFrame.translation.z = static_cast<float>(sample.getTranslation().z);
                    keyFrame.scale.x = static_cast<float>(sample.getScale().x);
                    keyFrame.scale.y = static_cast<float>(sample.getScale().y);
                    keyFrame.scale.z = static_cast<float>(sample.getScale().z);
                    glm::vec3 rot;
                    rot.x = glm::radians(static_cast<float>(sample.getXRotation()));
                    rot.y = glm::radians(static_cast<float>(sample.getYRotation()));
                    rot.z = glm::radians(static_cast<float>(sample.getZRotation()));
                    keyFrame.rotation = glm::quat(rot);
                }
            }
            scene.nodes.push_back(_node);

            int nodeIndex = static_cast<int>(scene.nodes.size() - 1);

            // 親ノードに追加
            scene.nodes[parentNodeIndex].childNodeIndices.push_back(nodeIndex);

            // 再帰的にXformの子オブジェクトを処理
            processObjectRecursive(scene, context, child, nodeIndex, ++depth);
        }
        // メッシュが見つかった場合
        else if (IPolyMesh::matches(child.getHeader())) {
            IPolyMesh mesh(child, kWrapExisting);
            processMesh(scene, context, mesh);

            // 追加したメッシュIDを親ノードに記録
            scene.nodes[parentNodeIndex].meshIndex = static_cast<int>(scene.meshes.size() - 1);
        }
    }
}
}  // namespace

void LoaderAlembic::loadFromFile(Scene& scene,
                                 const rv::Context& context,
                                 const std::filesystem::path& filepath) {
    // ファイルをオープン
    IArchive archive(Alembic::AbcCoreFactory::IFactory().getArchive(filepath.string()));
    IObject topObject = archive.getTop();

    scene.nodes.reserve(1000);
    scene.meshes.reserve(1000);

    scene.nodes.push_back(Node{});

    // トップレベルオブジェクトから再帰的に探索
    processObjectRecursive(scene, context, topObject, 0, 0);
    spdlog::info("");
};
