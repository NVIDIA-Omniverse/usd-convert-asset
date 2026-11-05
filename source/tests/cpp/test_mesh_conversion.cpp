// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "../../library/exporters/assimp/assimp_exporter.h"
#include "../../library/exporters/usd/usd_exporter.h"
#include "../../library/importers/assimp/assimp_importer.h"
#include "../../library/importers/importer.h"
#include "../../library/importers/usd/usd_importer.h"
#include "../../library/utils/stage_utils.h"
#include "test_utils.h"

#include <pxr/usd/usd/primRange.h>

#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <cstdlib>
#include <cstring>
#include <doctest/doctest.h>
#include <experimental/filesystem>
#include <fstream>
#include <numeric>

namespace
{
std::vector<std::string> conversionLogs;
} // namespace

TEST_CASE("Mesh optimization handles invalid point indices")
{
    auto stage = std::make_shared<Stage>();
    auto mesh = TestUtils::MakeTriangle();
    mesh->faceVertexIndices = { 0, 1, -1, 2147483647 };
    stage->meshes.push_back(mesh);

    CHECK_NOTHROW(StageUtils::OptimizeMeshes(stage));
}
TEST_CASE("Assimp skipped meshes never alias accepted meshes")
{
    // Cover both the all-skipped case and the case where a valid mesh follows a skipped mesh.
    for (bool includeValidMesh : { false, true })
    {
        CAPTURE(includeValidMesh);
        aiScene scene;
        scene.mRootNode = new aiNode("Root");
        scene.mNumMeshes = includeValidMesh ? 2 : 1;
        scene.mMeshes = new aiMesh*[scene.mNumMeshes];
        // This empty mesh is invalid and must not reserve an index in the imported mesh list.
        scene.mMeshes[0] = new aiMesh();
        if (includeValidMesh)
        {
            // Place the valid mesh after the skipped mesh to expose source-to-imported index mismatches.
            auto mesh = scene.mMeshes[1] = new aiMesh();
            mesh->mNumVertices = 3;
            mesh->mVertices = new aiVector3D[3]{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
            mesh->mNumFaces = 1;
            mesh->mFaces = new aiFace[1];
            mesh->mFaces[0].mNumIndices = 3;
            mesh->mFaces[0].mIndices = new unsigned int[3]{ 0, 1, 2 };
            mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
        }
        scene.mRootNode->mNumMeshes = scene.mNumMeshes;
        scene.mRootNode->mMeshes = new unsigned int[scene.mNumMeshes];
        std::iota(scene.mRootNode->mMeshes, scene.mRootNode->mMeshes + scene.mNumMeshes, 0);
        // Serialize the synthetic scene so the test exercises the normal Assimp importer path.
        Assimp::Exporter writer;
        static const aiExportDataBlob* data = nullptr;
        data = writer.ExportToBlob(&scene, "assbin");
        REQUIRE_MESSAGE(data, writer.GetErrorString());
        OmniConverterCallbacks callbacks;
        callbacks.readCallback = [](const char*, OmniConverterBlob* blob)
        {
            blob->size = data->size;
            blob->buffer = new uint8_t[blob->size];
            blob->deleter = gBlobDefaultDataDeleter;
            std::memcpy(blob->buffer, data->data, blob->size);
            return true;
        };
        auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
        context->converterContext = OmniConverterContext("input.assbin", "out.usda", callbacks, ".", OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS);
        AssimpImporter importer;
        auto status = OmniConverterStatus::OK;
        std::string error;
        auto stage = importer.ImportStage(context, status, error);
        REQUIRE_MESSAGE(stage, error);
        REQUIRE(stage->rootNode);
        REQUIRE(stage->meshes.size() == size_t(includeValidMesh));
        REQUIRE(stage->rootNode->staticMeshInstances.size() == size_t(includeValidMesh));
        if (includeValidMesh)
        {
            // The instance must reference the compacted imported index, not source mesh index 1.
            CHECK(stage->rootNode->staticMeshInstances[0] == 0);
        }
    }
}
TEST_CASE("Mesh merging preserves ineligible meshes and merges static siblings")
{
    for (int kind = 0; kind != 5; ++kind)
    {
        CAPTURE(kind);
        auto stage = std::make_shared<Stage>();
        stage->rootNode = std::make_shared<StageNode>("Root");
        auto excludedNode = std::make_shared<StageNode>("Excluded");
        excludedNode->parent = stage->rootNode;
        stage->rootNode->children.push_back(excludedNode);
        auto excluded = TestUtils::MakeTriangle();
        stage->meshes = { TestUtils::MakeTriangle(), excluded, TestUtils::MakeTriangle() };
        stage->rootNode->staticMeshInstances = { 0, 2 };
        excludedNode->staticMeshInstances = { 1 };
        if (kind == 0)
        {
            excluded->pointCacheTimesamples.push_back(excluded->points);
        }
        else if (kind == 1)
        {
            stage->skinMeshes.push_back(std::make_shared<SkinMesh>(1));
        }
        else if (kind == 2)
        {
            excludedNode->transformAnimationTracks.resize(1);
        }
        else if (kind == 3)
        {
            stage->rootNode->staticMeshInstances.push_back(1);
        }
        else
        {
            auto child = std::make_shared<StageNode>("AnimatedChild");
            child->parent = excludedNode;
            child->staticMeshInstances = { 1 };
            excludedNode->staticMeshInstances.clear();
            excludedNode->children.push_back(child);
            excludedNode->transformAnimationTracks.resize(1);
        }
        StageUtils::MergeMeshes(stage, "Merged");
        REQUIRE(stage->meshes.size() == 2);
        bool foundMerged = false;
        for (const auto& mesh : stage->meshes)
        {
            if (mesh != excluded)
            {
                CHECK(mesh->points.size() == 6);
                CHECK(mesh->faceVertexCounts.size() == 2);
                foundMerged = true;
            }
        }
        CHECK(foundMerged);
        CHECK(excluded->points.size() == 3);
        if (kind == 0)
        {
            CHECK(excluded->pointCacheTimesamples.size() == 1);
        }
        if (kind == 1)
        {
            CHECK(stage->meshes[stage->skinMeshes[0]->meshIndex] == excluded);
        }
        size_t excludedInstances = 0;
        StageUtils::TraverseStageTree(
            stage->rootNode,
            [&](const StageNodePtr& node)
            {
                for (size_t index : node->staticMeshInstances)
                {
                    REQUIRE(index < stage->meshes.size());
                    excludedInstances += stage->meshes[index] == excluded;
                }
                return true;
            }
        );
        CHECK(excludedInstances == (kind == 3 ? 2 : 1));
    }
}
TEST_CASE("OBJ importer marks corner normals explicitly")
{
    namespace fs = std::experimental::filesystem;
    const std::string root = std::getenv("REPO_ROOT_PATH");
    const std::string dir = root + "/_build/tests/cpp/obj_corner_normals";
    fs::create_directories(dir);
    const std::string input = dir + "/permuted.obj";
    std::ofstream(input) << "v 0 0 0\nv 0 0 1\nv 1 0 0\nv 5 0 0\nv 6 0 0\nv 5 1 0\n"
                            "vn 0 1 0\nvn 0 0 1\nf 4//2 5//2 6//2\nf 1//1 2//1 3//1\n";
    auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
    OmniConverterCallbacks callbacks;
    callbacks.readCallback = TestUtils::FileCallbacks().readCallback;
    context->converterContext = OmniConverterContext(input, dir + "/out.obj", callbacks, dir, 0);
    auto importer = CreateImporter(context->converterContext);
    auto status = OmniConverterStatus::OK;
    std::string error;
    auto stage = importer->ImportStage(context, status, error);
    REQUIRE_MESSAGE(status == OmniConverterStatus::OK, error);
    REQUIRE(stage);
    REQUIRE(stage->meshes.size() == 1);
    auto mesh = stage->meshes[0];
    REQUIRE(mesh->hasFaceVaryingNormals);
    REQUIRE(StageUtils::CleanupMeshes(stage, error));
    for (size_t corner = 0; corner < mesh->faceVertexIndices.size(); ++corner)
    {
        const auto& point = mesh->points[mesh->faceVertexIndices[corner]];
        const auto expected = point[0] >= 5 ? PXR_NS::GfVec3f(0, 0, 1) : PXR_NS::GfVec3f(0, 1, 0);
        CHECK(mesh->normals[corner] == expected);
    }
    fs::remove_all(dir);
}
TEST_CASE("Assimp export retains bind-pose geometry without static instances")
{
    namespace fs = std::experimental::filesystem;
    const std::string directory = std::string(std::getenv("REPO_ROOT_PATH")) + "/_build/tests/cpp/skin_geometry";
    fs::create_directories(directory);
    for (const std::string extension : { "fbx", "obj" })
    {
        auto stage = std::make_shared<Stage>();
        stage->rootNode = std::make_shared<StageNode>("Root");
        stage->meshes = { TestUtils::MakeTriangle() };
        MeshGeomSubset subset;
        subset.faceIndices = { 0 };
        stage->meshes[0]->meshSubsets = { subset };
        auto skin = std::make_shared<SkinMesh>(0);
        skin->geomBindTransform.SetTranslate(PXR_NS::GfVec3d(3, 0, 0));
        stage->skinMeshes = { skin };
        stage->animationTracks.resize(1);
        OmniConverterCallbacks callbacks;
        conversionLogs.clear();
        callbacks.logCallback = [](const char* message)
        {
            conversionLogs.emplace_back(message);
        };
        callbacks.pathExistsCallback = TestUtils::FileCallbacks().pathExistsCallback;
        callbacks.binaryWriteCallback = TestUtils::WriteBinaryFile;
        const auto output = directory + "/skin." + extension;
        auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
        context->converterContext = OmniConverterContext("input.usda", output, callbacks, directory, 0);
        AssimpExporter exporter(context);
        std::string error;
        REQUIRE_MESSAGE(exporter.Export(stage, error) == OmniConverterStatus::OK, error);
        CHECK(std::any_of(
            conversionLogs.begin(),
            conversionLogs.end(),
            [](const std::string& message)
            {
                return message.find("rest pose") != std::string::npos;
            }
        ));
        CHECK(std::any_of(
            conversionLogs.begin(),
            conversionLogs.end(),
            [](const std::string& message)
            {
                return message.find("does not preserve animation") != std::string::npos;
            }
        ));
        Assimp::Importer reader;
        const auto scene = reader.ReadFile(output, aiProcess_PreTransformVertices);
        REQUIRE_MESSAGE(scene, std::string(reader.GetErrorString()));
        REQUIRE(scene->mNumMeshes == 1);
        CHECK(scene->mMeshes[0]->mNumVertices == 3);
        CHECK(scene->mMeshes[0]->mNumFaces == 1);
        float minX = scene->mMeshes[0]->mVertices[0].x;
        float maxX = minX;
        for (unsigned int vertex = 1; vertex < scene->mMeshes[0]->mNumVertices; ++vertex)
        {
            minX = std::min(minX, scene->mMeshes[0]->mVertices[vertex].x);
            maxX = std::max(maxX, scene->mMeshes[0]->mVertices[vertex].x);
        }
        CHECK(minX == doctest::Approx(3.0f));
        CHECK(maxX == doctest::Approx(4.0f));
    }
    fs::remove_all(directory);
}
TEST_CASE("Surface conversion preserves face orientation and corner normals")
{
    namespace fs = std::experimental::filesystem;
    const std::string root = std::getenv("REPO_ROOT_PATH");
    const std::string directory = root + "/_build/tests/cpp/surface_conversion";
    fs::create_directories(directory);
    for (const std::string fixture : { "mirrored_surface.usda", "permuted_surface.usda", "permuted_surface.obj" })
    {
        for (const std::string extension : { "obj", "usda", "fbx" })
        {
            if (fixture == "mirrored_surface.usda" && extension != "obj")
            {
                continue;
            }
#ifdef __aarch64__
            if (extension == "fbx")
            {
                continue;
            }
#endif
            CAPTURE(fixture);
            CAPTURE(extension);
            auto callbacks = TestUtils::FileCallbacks();
            callbacks.binaryWriteCallback = TestUtils::WriteBinaryFile;
            const std::string outputPath = directory + "/" + fixture + "." + extension;
            auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
            context->converterContext = OmniConverterContext(
                root + "/data/tests/surfaces/" + fixture,
                outputPath,
                callbacks,
                directory,
                OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS
            );
            auto importer = CreateImporter(context->converterContext);
            auto status = OmniConverterStatus::OK;
            std::string error;
            auto stage = importer->ImportStage(context, status, error);
            REQUIRE(stage);
            REQUIRE_MESSAGE(status == OmniConverterStatus::OK, error);
            REQUIRE(StageUtils::CleanupMeshes(stage, error));
            auto exporter = CreateExporter(context);
            REQUIRE(exporter);
            REQUIRE_MESSAGE(exporter->Export(stage, error) == OmniConverterStatus::OK, error);
            size_t cornerCount = 0;
            if (extension == "usda")
            {
                auto output = PXR_NS::UsdStage::Open(outputPath);
                REQUIRE(static_cast<bool>(output));
                for (const auto& prim : output->Traverse())
                {
                    if (!prim.IsA<PXR_NS::UsdGeomMesh>())
                    {
                        continue;
                    }
                    PXR_NS::UsdGeomMesh mesh(prim);
                    PXR_NS::VtVec3fArray points, normals;
                    PXR_NS::VtIntArray indices, counts;
                    REQUIRE(mesh.GetPointsAttr().Get(&points));
                    REQUIRE(mesh.GetNormalsAttr().Get(&normals));
                    REQUIRE(mesh.GetFaceVertexIndicesAttr().Get(&indices));
                    REQUIRE(mesh.GetFaceVertexCountsAttr().Get(&counts));
                    REQUIRE(mesh.GetNormalsInterpolation() == PXR_NS::UsdGeomTokens->faceVarying);
                    REQUIRE(normals.size() == indices.size());
                    cornerCount += indices.size();
                    size_t first = 0;
                    for (int count : counts)
                    {
                        auto normal = PXR_NS::GfCross(
                                          points[indices[first + 1]] - points[indices[first]],
                                          points[indices[first + 2]] - points[indices[first]]
                        )
                                          .GetNormalized();
                        for (int i = 0; i < count; ++i)
                        {
                            CHECK(PXR_NS::GfDot(normal, normals[first + i]) > 0.99);
                        }
                        first += count;
                    }
                }
            }
            else
            {
                Assimp::Importer reader;
                const auto* output = reader.ReadFile(outputPath, aiProcess_PreTransformVertices);
                REQUIRE_MESSAGE(output, reader.GetErrorString());
                REQUIRE(output->mNumMeshes > 0);
                for (unsigned m = 0; m < output->mNumMeshes; ++m)
                {
                    const auto* mesh = output->mMeshes[m];
                    REQUIRE(mesh->HasNormals());
                    for (unsigned f = 0; f < mesh->mNumFaces; ++f)
                    {
                        const auto& face = mesh->mFaces[f];
                        REQUIRE(face.mNumIndices >= 3);
                        cornerCount += face.mNumIndices;
                        auto normal = (mesh->mVertices[face.mIndices[1]] - mesh->mVertices[face.mIndices[0]]) ^
                                      (mesh->mVertices[face.mIndices[2]] - mesh->mVertices[face.mIndices[0]]);
                        normal.Normalize();
                        for (unsigned i = 0; i < face.mNumIndices; ++i)
                        {
                            CHECK((normal * mesh->mNormals[face.mIndices[i]]) > 0.99f);
                        }
                    }
                }
            }
            CHECK(cornerCount == (fixture == "mirrored_surface.usda" ? 12 : 6));
        }
    }
    fs::remove_all(directory);
}
TEST_CASE("USD conversion preserves mixed mesh sidedness when merging")
{
    namespace fs = std::experimental::filesystem;
    const std::string root = std::getenv("REPO_ROOT_PATH");
    const std::string directory = root + "/_build/tests/cpp/mesh_sidedness";
    fs::create_directories(directory);
    for (bool merge : { false, true })
    {
        CAPTURE(merge);
        const std::string outputPath = directory + (merge ? "/merged.usda" : "/separate.usda");
        auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
        context->converterContext = OmniConverterContext(
            root + "/data/tests/surfaces/surface_sidedness.usda",
            outputPath,
            TestUtils::FileCallbacks(),
            directory,
            OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS
        );
        UsdSdkImporter importer;
        auto status = OmniConverterStatus::OK;
        std::string error;
        auto stage = importer.ImportStage(context, status, error);
        REQUIRE(stage);
        REQUIRE(status == OmniConverterStatus::OK);
        if (merge)
        {
            StageUtils::MergeMeshes(stage, "Merged");
        }
        UsdExporter exporter(context);
        REQUIRE_MESSAGE(exporter.Export(stage, error) == OmniConverterStatus::OK, error);
        auto output = PXR_NS::UsdStage::Open(outputPath);
        REQUIRE(static_cast<bool>(output));
        size_t single = 0, dual = 0;
        for (const auto& prim : output->Traverse())
        {
            if (!prim.IsA<PXR_NS::UsdGeomMesh>())
            {
                continue;
            }
            bool sided = false;
            REQUIRE(PXR_NS::UsdGeomMesh(prim).GetDoubleSidedAttr().Get(&sided));
            sided ? ++dual : ++single;
        }
        CHECK(single == (merge ? 1 : 2));
        CHECK(dual == (merge ? 1 : 2));
    }
    fs::remove_all(directory);
}
TEST_CASE("Merging a mirrored mesh keeps corner attributes attached")
{
    auto source = TestUtils::MakeTriangle();
    source->normals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
    source->hasFaceVaryingNormals = true;
    source->uvs = { PXR_NS::VtVec2fArray{ { 0, 0 }, { 1, 0 }, { 0, 1 } } };
    source->colors = { PXR_NS::VtVec3fArray{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
    auto target = std::make_shared<Mesh>();
    StageUtils::MergeMesh(target, source);
    const auto originalIndices = target->faceVertexIndices;
    const auto originalUvs = target->uvs[0];
    const auto originalColors = target->colors[0];
    PXR_NS::GfMatrix4d transform(1.0);
    transform.SetScale(PXR_NS::GfVec3d(-1, 2, 1));
    StageUtils::MergeMesh(target, source, transform);
    REQUIRE(target->faceVertexIndices.size() == 6);
    const auto& indices = target->faceVertexIndices;
    auto normal = PXR_NS::GfCross(target->points[indices[4]] - target->points[indices[3]], target->points[indices[5]] - target->points[indices[3]])
                      .GetNormalized();
    for (size_t i = 0; i < 3; ++i)
    {
        CHECK(indices[i] == originalIndices[i]);
        CHECK(target->uvs[0][i] == originalUvs[i]);
        CHECK(target->colors[0][i] == originalColors[i]);
        CHECK(PXR_NS::GfDot(normal, target->normals[i + 3]) > .99);
        CHECK(target->uvs[0][i + 3] == source->uvs[0][indices[i + 3] - 3]);
        CHECK(target->colors[0][i + 3] == source->colors[0][indices[i + 3] - 3]);
    }
    CHECK(source->faceVertexIndices == PXR_NS::VtIntArray{ 0, 1, 2 });
}
