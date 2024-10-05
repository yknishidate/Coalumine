#include "loader_alembic.hpp"
#include "scene.hpp"

#include <Alembic/AbcCoreFactory/All.h>
#include <Alembic/AbcGeom/All.h>
#include <Imath/ImathVec.h>

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

namespace {
void processMesh(Scene& scene, const rv::Context& context, IPolyMesh& mesh) {
    IPolyMeshSchema& meshSchema = mesh.getSchema();
    IPolyMeshSchema::Sample meshSample;
    meshSchema.get(meshSample, 0);

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
        if (!isNormalIndexed) {
            for (size_t i = 0; i < numIndices; ++i) {
                size_t vi = _indices[i];
                size_t ni = i;

                _vertices[vi].normal =
                    glm::vec3((*normals)[ni].x, (*normals)[ni].y, (*normals)[ni].z);
            }
        }
        // size_t numNormals = isNormalIndexed ? numVertices : numIndices;
        // for (size_t i = 0; i < numNormals; ++i) {
        //     size_t ni = isNormalIndexed ? _indices[i] : i;
        //     _vertices[i].normal = glm::vec3((*normals)[ni].x, (*normals)[ni].y,
        //     (*normals)[ni].z);
        // }
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

    // とりあえず一緒にNodeも追加
    Node _node;
    _node.meshIndex = static_cast<int>(meshIndex);
    scene.nodes.push_back(_node);
}

// 再帰的にXformやメッシュを探索する関数
void processObjectRecursive(Scene& scene,
                            const rv::Context& context,
                            const IObject& object,
                            uint32_t depth) {
    for (size_t i = 0; i < object.getNumChildren(); ++i) {
        IObject child = object.getChild(i);

        // Xformが見つかった場合
        if (IXform::matches(child.getHeader())) {
            std::cout << "Found Xform node: " << child.getName() << std::endl;
            IXform xform(child, kWrapExisting);

            // 再帰的にXformの子オブジェクトを処理
            processObjectRecursive(scene, context, child, ++depth);
        }
        // メッシュが見つかった場合
        else if (IPolyMesh::matches(child.getHeader())) {
            IPolyMesh mesh(child, kWrapExisting);
            processMesh(scene, context, mesh);

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

    // トップレベルオブジェクトから再帰的に探索
    processObjectRecursive(scene, context, topObject, 0);
}
