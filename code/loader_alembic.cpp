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
using Alembic::AbcGeom::IV2fGeomParam;
using Alembic::AbcGeom::IXform;
using Alembic::AbcGeom::kWrapExisting;
using Alembic::AbcGeom::N3fArraySamplePtr;
using Alembic::AbcGeom::P3fArraySamplePtr;
using Alembic::AbcGeom::V2fArraySamplePtr;
using Alembic::AbcGeom::XformSample;

namespace {
void processMesh(Scene& scene, const rv::Context& context, IPolyMesh& mesh) {
    IPolyMeshSchema& meshSchema = mesh.getSchema();
    IPolyMeshSchema::Sample meshSample;
    meshSchema.get(meshSample);

    // 頂点座標
    P3fArraySamplePtr positions = meshSample.getPositions();
    size_t numVertices = positions->size();

    // 法線
    N3fArraySamplePtr normals;
    bool isNormalIndexed = false;
    if (meshSchema.getNormalsParam().valid()) {
        IN3fGeomParam normalsParam = meshSchema.getNormalsParam();
        isNormalIndexed = normalsParam.isIndexed();
        IN3fGeomParam::Sample normalsSample;
        normalsParam.getExpanded(normalsSample);
        normals = normalsSample.getVals();
    }

    // テクスチャ座標
    V2fArraySamplePtr uv;
    bool isUvIndexed;
    if (meshSchema.getUVsParam().valid()) {
        IV2fGeomParam uvsParam = meshSchema.getUVsParam();
        isUvIndexed = uvsParam.isIndexed();
        IV2fGeomParam::Sample uvSample;
        uvsParam.getExpanded(uvSample);
        uv = uvSample.getVals();
    }

    // インデックス（フェイスの頂点インデックス情報）
    Int32ArraySamplePtr indices = meshSample.getFaceIndices();
    size_t numIndices = indices->size();
    std::vector<uint32_t> _indices(numIndices);
    for (size_t i = 0; i < numIndices; ++i) {
        _indices[i] = (*indices)[i];
    }

    std::vector<rv::Vertex> _vertices(numVertices);

    // 頂点情報をrv::Vertexに格納
    for (size_t i = 0; i < numVertices; ++i) {
        _vertices[i].pos = glm::vec3((*positions)[i].x, (*positions)[i].y, (*positions)[i].z);
    }

    // 法線が存在する場合のみ
    if (normals) {
        // WARN: 位置は同じだが、法線が違う頂点を扱えていない可能性がある
        if (!isNormalIndexed) {
            for (size_t i = 0; i < numIndices; ++i) {
                size_t vi = _indices[i];
                size_t ni = i;

                _vertices[vi].normal =
                    glm::vec3((*normals)[ni].x, (*normals)[ni].y, (*normals)[ni].z);
            }
        } else {
            // TODO:
        }
    }

    // Mesh追加
    size_t meshIndex = scene.meshes.size();
    Mesh _mesh{};
    _mesh.vertexBuffer = context.createBuffer({
        .usage = rv::BufferUsage::AccelVertex,
        .size = sizeof(rv::Vertex) * _vertices.size(),
        .debugName = std::format("vertexBuffers[{}]", meshIndex).c_str(),
    });
    _mesh.indexBuffer = context.createBuffer({
        .usage = rv::BufferUsage::AccelIndex,
        .size = sizeof(uint32_t) * _indices.size(),
        .debugName = std::format("indexBuffers[{}]", meshIndex).c_str(),
    });

    context.oneTimeSubmit([&](auto commandBuffer) {
        commandBuffer->copyBuffer(_mesh.vertexBuffer, _vertices.data());
        commandBuffer->copyBuffer(_mesh.indexBuffer, _indices.data());
    });

    _mesh.vertexCount = static_cast<uint32_t>(_vertices.size());
    _mesh.triangleCount = static_cast<uint32_t>(_indices.size() / 3);
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

            XformSample sample;
            xform.getSchema().get(sample);

            Node _node;
            _node.parentNode = &scene.nodes[parentNodeIndex];
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

            //// 頂点アニメーションの確認
            // size_t numSamples = meshSchema.getNumSamples();
            // if (numSamples > 1) {
            //     // std::cout << "Mesh has vertex animations with " << numSamples << " samples."
            //     std::cout << std::format("{:<{}}Num Samples: {}", "", depth, numSamples)
            //               << std::endl;
            //
            //     // 複数のサンプルを取得して出力
            //     for (size_t frame = 1; frame < numSamples; ++frame) {
            //         meshSchema.get(meshSample, frame);
            //         P3fArraySamplePtr animVertices = meshSample.getPositions();
            //
            //         // std::cout << "Frame " << frame << std::endl;
            //         //  for (size_t j = 0; j < numVertices; ++j) {
            //         //      const Imath::V3f& animVertex = (*animVertices)[j];
            //         //      std::cout << "Vertex " << j << ": (" << animVertex.x << ", " <<
            //         //      animVertex.y
            //         //                << ", " << animVertex.z << ")" << std::endl;
            //         //  }
            //     }
            // } else {
            //     std::cout << "Mesh does not have vertex animations." << std::endl;
            // }
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
