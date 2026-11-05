// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "../../library/exporters/assimp/assimp_exporter.h"
#include "../../library/exporters/usd/usd_exporter.h"
#include "../../library/importers/assimp/assimp_importer.h"
#include "../../library/importers/importer.h"
#include "../../library/importers/usd/usd_importer.h"
#include "../../library/utils/math_utils.h"
#include "../../library/utils/stage_utils.h"
#include "test_utils.h"

#include <pxr/usd/usd/primRange.h>

#ifndef __aarch64__
#    include <fbxsdk.h>
#endif

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

TEST_CASE("Assimp animation preserves endpoint translation scale and rotation")
{
    namespace fs = std::experimental::filesystem;
    const std::string directory = std::string(std::getenv("REPO_ROOT_PATH")) + "/_build/tests/cpp/animation_endpoints";
    fs::create_directories(directory);
    double ticksPerSecond = 24.0;
    double duration = 2.0;
    double finalKeyTime = 1.5;
    SUBCASE("24 ticks per second")
    {
    }
    SUBCASE("30 ticks per second")
    {
        ticksPerSecond = 30.0;
    }
    SUBCASE("Fractional tick rate")
    {
        ticksPerSecond = 29.97;
    }
    SUBCASE("Unspecified tick rate")
    {
        ticksPerSecond = 0.0;
    }
    SUBCASE("Fractional endpoint")
    {
        duration = finalKeyTime = 2.5;
    }
    SUBCASE("Fractional endpoint at 30 ticks per second")
    {
        duration = finalKeyTime = 2.5;
        ticksPerSecond = 30.0;
    }
    SUBCASE("Duration shorter than one tick")
    {
        duration = finalKeyTime = 0.5;
    }
    SUBCASE("High resolution source clock")
    {
        ticksPerSecond = duration = finalKeyTime = 4800.0;
    }
    CAPTURE(ticksPerSecond);
    CAPTURE(duration);
    const auto sampleCount = static_cast<size_t>(std::ceil(duration)) + 1;
    aiScene scene;
    scene.mRootNode = new aiNode("Animated");
    scene.mNumMeshes = 1;
    scene.mMeshes = new aiMesh* [1]
    {
        new aiMesh()
    };
    auto mesh = scene.mMeshes[0];
    mesh->mNumVertices = 3;
    mesh->mVertices = new aiVector3D[3]{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    mesh->mNumFaces = 1;
    mesh->mFaces = new aiFace[1];
    mesh->mFaces[0].mNumIndices = 3;
    mesh->mFaces[0].mIndices = new unsigned int[3]{ 0, 1, 2 };
    mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
    scene.mNumLights = 1;
    scene.mLights = new aiLight* [1]
    {
        new aiLight()
    };
    scene.mLights[0]->mName = scene.mRootNode->mName;
    scene.mLights[0]->mType = aiLightSource_POINT;
    mesh->mNumBones = 1;
    mesh->mBones = new aiBone* [1]
    {
        new aiBone()
    };
    mesh->mBones[0]->mName = scene.mRootNode->mName;
    mesh->mBones[0]->mNumWeights = 1;
    mesh->mBones[0]->mWeights = new aiVertexWeight[1]{ aiVertexWeight(0, 1.0f) };
    scene.mRootNode->mNumMeshes = 1;
    scene.mRootNode->mMeshes = new unsigned int[1]{ 0 };
    scene.mNumMaterials = 1;
    scene.mMaterials = new aiMaterial* [1]
    {
        new aiMaterial()
    };
    scene.mNumAnimations = 1;
    scene.mAnimations = new aiAnimation* [1]
    {
        new aiAnimation()
    };
    auto animation = scene.mAnimations[0];
    animation->mDuration = duration;
    animation->mTicksPerSecond = ticksPerSecond;
    animation->mNumChannels = 1;
    animation->mChannels = new aiNodeAnim* [1]
    {
        new aiNodeAnim()
    };
    auto channel = animation->mChannels[0];
    channel->mNodeName = scene.mRootNode->mName;
    channel->mNumPositionKeys = channel->mNumScalingKeys = channel->mNumRotationKeys = 3;
    channel->mPositionKeys = new aiVectorKey[3];
    channel->mScalingKeys = new aiVectorKey[3];
    channel->mRotationKeys = new aiQuatKey[3];
    for (int i = 0; i < 3; ++i)
    {
        const double keyTime = i == 2 ? finalKeyTime : (duration < 1.0 ? i * finalKeyTime / 2.0 : static_cast<double>(i));
        channel->mPositionKeys[i] = aiVectorKey(keyTime, aiVector3D(i, 0, 0));
        channel->mScalingKeys[i] = aiVectorKey(keyTime, aiVector3D(1 + i, 1 + i, 1 + i));
        channel->mRotationKeys[i] = aiQuatKey(keyTime, i == 2 ? aiQuaternion(0, 0, 0, 1) : aiQuaternion(1, 0, 0, 0));
    }
    const std::string input = directory + "/animation.assbin";
    Assimp::Exporter writer;
    REQUIRE(writer.Export(&scene, "assbin", input) == aiReturn_SUCCESS);
    OmniConverterCallbacks callbacks;
    conversionLogs.clear();
    callbacks.logCallback = [](const char* message)
    {
        conversionLogs.emplace_back(message);
    };
    callbacks.readCallback = TestUtils::FileCallbacks().readCallback;
    callbacks.binaryWriteCallback = TestUtils::WriteBinaryFile;
    callbacks.pathExistsCallback = TestUtils::FileCallbacks().pathExistsCallback;
    callbacks.makeDirsCallback = TestUtils::FileCallbacks().makeDirsCallback;
    callbacks.layerWriteCallback = TestUtils::FileCallbacks().layerWriteCallback;
    auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
    context->converterContext = OmniConverterContext(input, directory + "/out.usda", callbacks, directory, OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS);
    AssimpImporter importer;
    auto status = OmniConverterStatus::OK;
    std::string error;
    auto stage = importer.ImportStage(context, status, error);
    REQUIRE_MESSAGE(status == OmniConverterStatus::OK, error);
    REQUIRE(stage);
    CHECK(stage->maxKeyFrames == sampleCount - 1);
    REQUIRE(stage->animationTracks.size() == 1);
    CHECK(stage->animationTracks[0].keyFrames <= sampleCount);
    CHECK(stage->animationTracks[0].duration == doctest::Approx(duration));
    CHECK(stage->mutiplier == doctest::Approx(ticksPerSecond > 0 ? ticksPerSecond / 24.0 : 1.0));
    REQUIRE(stage->rootNode->transformAnimationTracks.size() == 1);
    CHECK(std::any_of(
        conversionLogs.begin(),
        conversionLogs.end(),
        [](const std::string& message)
        {
            return message.find("does not preserve lights") != std::string::npos;
        }
    ));
    CHECK(std::any_of(
        conversionLogs.begin(),
        conversionLogs.end(),
        [](const std::string& message)
        {
            return message.find("does not preserve mesh skin bindings") != std::string::npos;
        }
    ));
    const auto& samples = stage->rootNode->transformAnimationTracks[0];
    if (ticksPerSecond == 4800.0)
    {
        REQUIRE(samples.GetTranslationSamples().size() == 26);
    }
    const size_t retainedCount = samples.GetTranslationSamples().size();
    REQUIRE(retainedCount >= 3);
    CHECK(retainedCount <= stage->animationTracks[0].keyFrames + 3);
    REQUIRE(samples.GetScaleSamples().size() == retainedCount);
    CHECK(samples.GetSampleTime(retainedCount - 1) == doctest::Approx(duration));
    if (duration >= 1.0)
    {
        CHECK(samples.GetSampleTime(1) == doctest::Approx(1.0));
        CHECK(samples.GetTranslationSamples()[1] == PXR_NS::GfVec3d(1, 0, 0));
        CHECK(samples.GetScaleSamples()[1] == PXR_NS::GfVec3d(2, 2, 2));
    }
    CHECK(samples.GetTranslationSamples().back() == PXR_NS::GfVec3d(2, 0, 0));
    CHECK(samples.GetScaleSamples().back() == PXR_NS::GfVec3d(3, 3, 3));
    REQUIRE(samples.GetOrientSamples().size() == retainedCount);
    CHECK(samples.GetOrientSamples().back() == PXR_NS::GfQuatd(0, PXR_NS::GfVec3d(0, 0, 1)));
    {
        UsdExporter exporter(context);
        REQUIRE_MESSAGE(exporter.Export(stage, error) == OmniConverterStatus::OK, error);
        auto output = PXR_NS::UsdStage::Open(directory + "/out.usda");
        REQUIRE(static_cast<bool>(output));
        const double expectedEnd = duration / stage->mutiplier;
        CHECK(output->GetEndTimeCode() == doctest::Approx(expectedEnd));
        size_t animatedAttributes = 0;
        for (const auto& prim : output->Traverse())
        {
            for (const auto& attribute : prim.GetAttributes())
            {
                std::vector<double> times;
                attribute.GetTimeSamples(&times);
                if (!times.empty())
                {
                    ++animatedAttributes;
                    CHECK(times.size() == retainedCount);
                    CHECK(times.back() == doctest::Approx(expectedEnd));
                }
            }
        }
        CHECK(animatedAttributes >= 3);
        auto readContext = std::make_shared<OmniConverterFuture::FutureThreadContext>();
        readContext->converterContext = OmniConverterContext(
            directory + "/out.usda",
            directory + "/roundtrip.usda",
            callbacks,
            directory,
            OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS
        );
        UsdSdkImporter usdReader;
        auto imported = usdReader.ImportStage(readContext, status, error);
        REQUIRE_MESSAGE(status == OmniConverterStatus::OK, error);
        REQUIRE(imported);
        size_t animatedNodes = 0;
        StageUtils::TraverseStageTree(
            imported->rootNode,
            [&](const StageNodePtr& node)
            {
                for (const auto& track : node->transformAnimationTracks)
                {
                    if (!track.Empty())
                    {
                        ++animatedNodes;
                        REQUIRE(!track.GetTranslationSamples().empty());
                        CHECK(track.GetSampleTime(track.GetTranslationSamples().size() - 1) == doctest::Approx(expectedEnd));
                        CHECK(track.GetTranslationSamples().back() == PXR_NS::GfVec3d(2, 0, 0));
                        CHECK(track.GetScaleSamples().back() == PXR_NS::GfVec3d(3, 3, 3));
                    }
                }
                return true;
            }
        );
        CHECK(animatedNodes > 0);
        UsdExporter roundtripWriter(readContext);
        REQUIRE_MESSAGE(roundtripWriter.Export(imported, error) == OmniConverterStatus::OK, error);
        auto roundtrip = PXR_NS::UsdStage::Open(directory + "/roundtrip.usda");
        REQUIRE(static_cast<bool>(roundtrip));
        CHECK(roundtrip->GetEndTimeCode() == doctest::Approx(expectedEnd));
    }
    // Other transform exporters must retain the same fractional sample time.
    for (const std::string extension : { "gltf", "fbx" })
    {
#ifdef __aarch64__
        if (extension == "fbx")
        {
            continue; // The Assimp FBX exporter does not export animation.
        }
#endif
        CAPTURE(extension);
        const std::string outputPath = directory + "/out." + extension;
        context->converterContext = OmniConverterContext(input, outputPath, callbacks, directory, OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS);
        auto exporter = CreateExporter(context);
        REQUIRE(exporter);
        REQUIRE_MESSAGE(exporter->Export(stage, error) == OmniConverterStatus::OK, error);
        Assimp::Importer reader;
        const auto result = reader.ReadFile(outputPath, 0);
        REQUIRE_MESSAGE(result, reader.GetErrorString());
        REQUIRE(result->mNumAnimations == 1);
        const auto clip = result->mAnimations[0];
        REQUIRE(clip->mTicksPerSecond > 0.0);
        const double expectedSeconds = duration / (ticksPerSecond > 0 ? ticksPerSecond : 24.0);
        bool foundAnimatedChannel = false;
        for (unsigned int i = 0; i < clip->mNumChannels; ++i)
        {
            const auto node = clip->mChannels[i];
            if (node->mNumPositionKeys > 1)
            {
                foundAnimatedChannel = true;
                REQUIRE(node->mNumScalingKeys > 0);
                REQUIRE(node->mNumRotationKeys > 0);
                CHECK(node->mPositionKeys[node->mNumPositionKeys - 1].mTime / clip->mTicksPerSecond == doctest::Approx(expectedSeconds));
                CHECK(node->mScalingKeys[node->mNumScalingKeys - 1].mTime / clip->mTicksPerSecond == doctest::Approx(expectedSeconds));
                CHECK(node->mRotationKeys[node->mNumRotationKeys - 1].mTime / clip->mTicksPerSecond == doctest::Approx(expectedSeconds));
            }
        }
        CHECK(foundAnimatedChannel);
    }
    fs::remove_all(directory);
}
TEST_CASE("Assimp FBX import keeps the source timeline duration")
{
    const std::string root = std::getenv("REPO_ROOT_PATH");
    OmniConverterCallbacks callbacks;
    callbacks.readCallback = TestUtils::FileCallbacks().readCallback;
    auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
    context->converterContext = OmniConverterContext(
        root + "/_build/target-deps/assimp_sample_models/FBX/animation_with_skeleton.fbx",
        root + "/_build/tests/cpp/animation_duration.usda",
        callbacks,
        root,
        OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS
    );
    // Exercise the ARM64 importer path directly on every test platform.
    AssimpImporter importer;
    auto status = OmniConverterStatus::OK;
    std::string error;
    auto stage = importer.ImportStage(context, status, error);
    REQUIRE_MESSAGE(stage, error);
    REQUIRE(status == OmniConverterStatus::OK);
    REQUIRE(stage->animationTracks.size() == 1);
    CHECK(stage->animationTracks[0].fps == doctest::Approx(24.0));
    CHECK(stage->maxKeyFrames / stage->mutiplier == doctest::Approx(20.0));
}
TEST_CASE("USD transform channels retain fractional endpoints after a round trip")
{
    namespace fs = std::experimental::filesystem;
    const std::string root = std::getenv("REPO_ROOT_PATH");
    const std::string directory = root + "/_build/tests/cpp/fractional_transforms";
    fs::create_directories(directory);
    OmniConverterCallbacks callbacks;
    callbacks.pathExistsCallback = TestUtils::FileCallbacks().pathExistsCallback;
    callbacks.makeDirsCallback = TestUtils::FileCallbacks().makeDirsCallback;
    callbacks.layerWriteCallback = TestUtils::FileCallbacks().layerWriteCallback;
    auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
    const std::string outputPath = directory + "/out.usda";
    context->converterContext = OmniConverterContext(
        root + "/data/tests/usda/fractional_transform_endpoints.usda",
        outputPath,
        callbacks,
        directory,
        OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS | OMNI_CONVERTER_FLAGS_IGNORE_PIVOTS
    );
    UsdSdkImporter importer;
    auto status = OmniConverterStatus::OK;
    std::string error;
    auto stage = importer.ImportStage(context, status, error);
    REQUIRE_MESSAGE(status == OmniConverterStatus::OK, error);
    REQUIRE(stage);
    size_t animatedNodes = 0;
    StageUtils::TraverseStageTree(
        stage->rootNode,
        [&](const StageNodePtr& node)
        {
            for (const auto& samples : node->transformAnimationTracks)
            {
                if (!samples.Empty())
                {
                    ++animatedNodes;
                    REQUIRE(samples.GetTranslationSamples().size() == 4);
                    CHECK(samples.GetTranslationSamples().back() == PXR_NS::GfVec3d(6, 0, 0));
                    CHECK(samples.GetSampleTime(3) == doctest::Approx(2.5));
                    if (node->name == "Euler")
                    {
                        REQUIRE(samples.GetScaleSamples().size() == 3);
                        CHECK(samples.GetSampleTime(2, TransformChannel::Scale) == doctest::Approx(1.5));
                        CHECK(samples.GetScaleSamples().back() == PXR_NS::GfVec3d(2));
                        CHECK(samples.GetRotationXYZSamples().back() == PXR_NS::GfVec3d(0, 0, 90));
                    }
                }
            }
            return true;
        }
    );
    CHECK(animatedNodes == 3);
    {
        UsdExporter exporter(context);
        REQUIRE_MESSAGE(exporter.Export(stage, error) == OmniConverterStatus::OK, error);
        auto output = PXR_NS::UsdStage::Open(outputPath);
        REQUIRE(static_cast<bool>(output));
        CHECK(output->GetEndTimeCode() == doctest::Approx(2.5));
        size_t animatedAttributes = 0;
        for (const auto& prim : output->Traverse())
        {
            for (const auto& attribute : prim.GetAttributes())
            {
                std::vector<double> times;
                attribute.GetTimeSamples(&times);
                if (!times.empty())
                {
                    ++animatedAttributes;
                    const bool earlyScale = prim.GetName() == PXR_NS::TfToken("Euler") && attribute.GetName() == PXR_NS::TfToken("xformOp:scale");
                    CHECK(times.back() == doctest::Approx(earlyScale ? 1.5 : 2.5));
                }
            }
        }
        CHECK(animatedAttributes >= 3);
    }
    fs::remove_all(directory);
}
TEST_CASE("USD skeletal samples retain fractional clip endpoints")
{
    namespace fs = std::experimental::filesystem;
    const std::string directory = std::string(std::getenv("REPO_ROOT_PATH")) + "/_build/tests/cpp/skeletal_endpoints";
    fs::create_directories(directory);
    bool earlyScale = false;
    bool differentBoneEndpoints = false;
    bool longerTimeline = false;
    SUBCASE("All channels reach the clip end")
    {
    }
    SUBCASE("Scale ends before the clip")
    {
        earlyScale = true;
    }
    SUBCASE("Bones and channels finish at different fractional times")
    {
        differentBoneEndpoints = true;
    }
    SUBCASE("The scene continues after the bone stops")
    {
        longerTimeline = true;
    }
    auto stage = std::make_shared<Stage>();
    stage->rootNode = std::make_shared<StageNode>("World");
    auto bone = std::make_shared<StageNode>("RootBone");
    bone->isBoneNode = true;
    bone->parent = stage->rootNode;
    stage->rootNode->children.push_back(bone);
    AnimationTrack track;
    track.name = "Motion";
    track.duration = longerTimeline ? 5.0 : 2.5;
    track.keyFrames = 4;
    stage->animationTracks.push_back(track);
    stage->maxKeyFrames = 3;
    PXR_NS::VtVec3dArray translations = { { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 }, { 3, 0, 0 } };
    PXR_NS::VtVec3dArray scales(earlyScale ? 2 : 4, PXR_NS::GfVec3d(2));
    PXR_NS::VtQuatdArray rotations(4, PXR_NS::GfQuatd(1));
    if (differentBoneEndpoints)
    {
        rotations[1] = PXR_NS::GfQuatd(std::sqrt(0.5), 0, 0, std::sqrt(0.5));
        rotations[2] = rotations[3] = PXR_NS::GfQuatd(0, 0, 0, 1);
    }
    TransformTimesamples samples(translations, rotations, scales);
    samples.SetSampleEndTime(2.5);
    bone->transformAnimationTracks.push_back(samples);
    if (differentBoneEndpoints)
    {
        auto child = std::make_shared<StageNode>("ChildBone");
        child->isBoneNode = true;
        child->parent = bone;
        bone->children.push_back(child);
        TransformTimesamples childSamples(
            PXR_NS::VtVec3dArray{ { 0, 0, 0 }, { 10, 0, 0 }, { 20, 0, 0 } },
            PXR_NS::VtQuatdArray{ PXR_NS::GfQuatd(1), PXR_NS::GfQuatd(std::sqrt(0.5), 0, 0, std::sqrt(0.5)) },
            PXR_NS::VtVec3dArray{ PXR_NS::GfVec3d(1), PXR_NS::GfVec3d(3) }
        );
        childSamples.SetSampleEndTime(1.5, TransformChannel::Translation);
        childSamples.SetSampleEndTime(0.5, TransformChannel::Rotation);
        childSamples.SetSampleEndTime(0.75, TransformChannel::Scale);
        child->transformAnimationTracks.push_back(childSamples);
        // A joint without animation must retain its rest pose at all shared sample times.
        auto staticBone = std::make_shared<StageNode>("StaticBone");
        staticBone->isBoneNode = true;
        staticBone->parent = bone;
        staticBone->restTransform.SetTranslate(PXR_NS::GfVec3d(7, 0, 0));
        bone->children.push_back(staticBone);
    }
    stage->meshes.push_back(TestUtils::MakeTriangle());
    MeshGeomSubset subset;
    subset.faceIndices = { 0 };
    stage->meshes[0]->meshSubsets = { subset };
    auto skin = std::make_shared<SkinMesh>(0);
    skin->skeletonRoot = bone;
    skin->influencedBoneNodes = { bone };
    skin->numBoneInfluencesPerVertex = 1;
    skin->jointWeights = { 1, 1, 1 };
    skin->jointInfluences = { 0, 0, 0 };
    stage->skinMeshes.push_back(skin);
    OmniConverterCallbacks callbacks;
    callbacks.pathExistsCallback = TestUtils::FileCallbacks().pathExistsCallback;
    callbacks.makeDirsCallback = TestUtils::FileCallbacks().makeDirsCallback;
    callbacks.layerWriteCallback = TestUtils::FileCallbacks().layerWriteCallback;
    auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
    const std::string outputPath = directory + "/out.usda";
    context->converterContext = OmniConverterContext("input.fbx", outputPath, callbacks, directory, OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS);
    std::string error;
    {
        UsdExporter exporter(context);
        REQUIRE_MESSAGE(exporter.Export(stage, error) == OmniConverterStatus::OK, error);
        auto output = PXR_NS::UsdStage::Open(outputPath);
        REQUIRE(static_cast<bool>(output));
        CHECK(output->GetEndTimeCode() == doctest::Approx(track.duration));
        size_t animationCount = 0;
        for (const auto& prim : output->Traverse())
        {
            if (auto animation = PXR_NS::UsdSkelAnimation(prim))
            {
                ++animationCount;
                for (const auto& attr : { animation.GetTranslationsAttr(), animation.GetRotationsAttr(), animation.GetScalesAttr() })
                {
                    std::vector<double> times;
                    attr.GetTimeSamples(&times);
                    REQUIRE(!times.empty());
                    const bool isEarlyScale = earlyScale && attr.GetName() == PXR_NS::UsdSkelTokens->scales;
                    CHECK(times.back() == doctest::Approx(isEarlyScale ? 1.0 : 2.5));
                }
                PXR_NS::VtVec3fArray values;
                REQUIRE(animation.GetTranslationsAttr().Get(&values, 2.5));
                REQUIRE(values.size() == (differentBoneEndpoints ? 3 : 1));
                CHECK(values[0] == PXR_NS::GfVec3f(3, 0, 0));
                if (differentBoneEndpoints)
                {
                    CHECK(values[1] == PXR_NS::GfVec3f(20, 0, 0));
                    CHECK(values[2] == PXR_NS::GfVec3f(7, 0, 0));
                    REQUIRE(animation.GetTranslationsAttr().Get(&values, 1.5));
                    CHECK(values[0][0] == doctest::Approx(1.5));
                    CHECK(values[1][0] == doctest::Approx(20.0));
                    CHECK(values[2][0] == doctest::Approx(7.0));
                    PXR_NS::VtQuatfArray orientations;
                    REQUIRE(animation.GetRotationsAttr().Get(&orientations, 0.5));
                    REQUIRE(orientations.size() == 3);
                    CHECK(orientations[0].GetReal() == doctest::Approx(0.9238795));
                    CHECK(orientations[1].GetReal() == doctest::Approx(std::sqrt(0.5)));
                    PXR_NS::VtVec3hArray sizes;
                    REQUIRE(animation.GetScalesAttr().Get(&sizes, 0.75));
                    REQUIRE(sizes.size() == 3);
                    CHECK(sizes[0] == PXR_NS::GfVec3h(2));
                    CHECK(sizes[1] == PXR_NS::GfVec3h(3));
                    CHECK(sizes[2] == PXR_NS::GfVec3h(1));
                }
            }
        }
        CHECK(animationCount == 1);
    }
    fs::remove_all(directory);
}
TEST_CASE("Assimp clips with different rates retain equal playback durations")
{
    namespace fs = std::experimental::filesystem;
    const std::string directory = std::string(std::getenv("REPO_ROOT_PATH")) + "/_build/tests/cpp/mixed_animation_rates";
    for (bool reverseOrder : { false, true })
    {
        for (bool skeletal : { false, true })
        {
            CAPTURE(reverseOrder);
            CAPTURE(skeletal);
            fs::create_directories(directory);
            aiScene scene;
            scene.mRootNode = new aiNode("Animated");
            scene.mNumMeshes = 1;
            scene.mMeshes = new aiMesh* [1]
            {
                new aiMesh()
            };
            auto mesh = scene.mMeshes[0];
            mesh->mNumVertices = 3;
            mesh->mVertices = new aiVector3D[3]{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
            mesh->mNumFaces = 1;
            mesh->mFaces = new aiFace[1];
            mesh->mFaces[0].mNumIndices = 3;
            mesh->mFaces[0].mIndices = new unsigned int[3]{ 0, 1, 2 };
            mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
            scene.mRootNode->mNumMeshes = 1;
            scene.mRootNode->mMeshes = new unsigned int[1]{ 0 };
            scene.mNumMaterials = 1;
            scene.mMaterials = new aiMaterial* [1]
            {
                new aiMaterial()
            };
            scene.mNumAnimations = 2;
            scene.mAnimations = new aiAnimation* [2]
            {
                new aiAnimation(), new aiAnimation()
            };
            for (unsigned int i = 0; i < 2; ++i)
            {
                const double rate = (i == 0) != reverseOrder ? 24.0 : 30.0;
                auto clip = scene.mAnimations[i];
                clip->mName = aiString(rate == 24.0 ? "SlowClock" : "FastClock");
                clip->mDuration = clip->mTicksPerSecond = rate;
                clip->mNumChannels = 1;
                clip->mChannels = new aiNodeAnim* [1]
                {
                    new aiNodeAnim()
                };
                auto channel = clip->mChannels[0];
                channel->mNodeName = scene.mRootNode->mName;
                channel->mNumPositionKeys = channel->mNumScalingKeys = channel->mNumRotationKeys = 2;
                channel->mPositionKeys = new aiVectorKey[2]{ { 0, { 0, 0, 0 } }, { rate, { 1, 0, 0 } } };
                channel->mScalingKeys = new aiVectorKey[2]{ { 0, { 1, 1, 1 } }, { rate, { 2, 2, 2 } } };
                channel->mRotationKeys = new aiQuatKey[2]{ { 0, { 1, 0, 0, 0 } }, { rate, { 0, 0, 0, 1 } } };
            }
            const std::string input = directory + "/clips.assbin";
            const std::string outputPath = directory + "/out.usda";
            Assimp::Exporter writer;
            REQUIRE(writer.Export(&scene, "assbin", input) == aiReturn_SUCCESS);
            auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
            context->converterContext = OmniConverterContext(
                input,
                outputPath,
                TestUtils::FileCallbacks(),
                directory,
                OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS
            );
            AssimpImporter importer;
            auto status = OmniConverterStatus::OK;
            std::string error;
            auto stage = importer.ImportStage(context, status, error);
            REQUIRE_MESSAGE(status == OmniConverterStatus::OK, error);
            REQUIRE(stage);
            REQUIRE(stage->animationTracks.size() == 2);
            if (skeletal)
            {
                auto bone = stage->rootNode;
                bone->isBoneNode = true;
                bone->staticMeshInstances.clear();
                stage->rootNode = std::make_shared<StageNode>("World");
                stage->rootNode->children = { bone };
                bone->parent = stage->rootNode;
                auto skin = std::make_shared<SkinMesh>(0);
                skin->skeletonRoot = bone;
                skin->influencedBoneNodes = { bone };
                skin->numBoneInfluencesPerVertex = 1;
                skin->jointWeights = { 1, 1, 1 };
                skin->jointInfluences = { 0, 0, 0 };
                stage->skinMeshes.push_back(skin);
            }
            {
                UsdExporter exporter(context);
                REQUIRE_MESSAGE(exporter.Export(stage, error) == OmniConverterStatus::OK, error);
                auto output = PXR_NS::UsdStage::Open(outputPath);
                REQUIRE(static_cast<bool>(output));
                CHECK(output->GetEndTimeCode() / output->GetTimeCodesPerSecond() == doctest::Approx(1.0));
                const auto sets = output->GetDefaultPrim().GetVariantSets();
                REQUIRE(sets.GetNames().size() == 1);
                auto variants = sets.GetVariantSet(sets.GetNames()[0]);
                REQUIRE(variants.GetVariantNames().size() == 2);
                for (const auto& name : variants.GetVariantNames())
                {
                    CAPTURE(name);
                    REQUIRE(variants.SetVariantSelection(name));
                    size_t animatedAttributes = 0;
                    for (const auto& prim : output->Traverse())
                    {
                        for (const auto& attribute : prim.GetAttributes())
                        {
                            std::vector<double> times;
                            attribute.GetTimeSamples(&times);
                            if (!times.empty())
                            {
                                ++animatedAttributes;
                                CHECK(times.back() == doctest::Approx(24.0));
                                if (attribute.GetName() == PXR_NS::TfToken("xformOp:translate"))
                                {
                                    const PXR_NS::UsdGeomXformOp translation(attribute);
                                    REQUIRE(translation);
                                    const auto position = translation.GetOpTransform(12).ExtractTranslation();
                                    CHECK(position[0] == doctest::Approx(0.5));
                                }
                                if (attribute.GetName() == PXR_NS::UsdSkelTokens->translations)
                                {
                                    PXR_NS::VtVec3fArray positions;
                                    REQUIRE(attribute.Get(&positions, 12));
                                    REQUIRE(positions.size() == 1);
                                    CHECK(positions[0][0] == doctest::Approx(0.5));
                                }
                            }
                        }
                    }
                    CHECK(animatedAttributes >= 3);
                }
            }
            fs::remove_all(directory);
        }
    }
}
TEST_CASE("USD skeletal import normalizes a nonzero start and preserves intermediate poses")
{
    namespace fs = std::experimental::filesystem;
    const std::string root = std::getenv("REPO_ROOT_PATH");
    const std::string directory = root + "/_build/tests/cpp/skeleton_offset";
    fs::create_directories(directory);
    const std::string outputPath = directory + "/out.usda";
    auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
    context->converterContext = OmniConverterContext(
        root + "/data/tests/usda/skeleton_animation_offset.usda",
        outputPath,
        TestUtils::FileCallbacks(),
        directory,
        OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS
    );
    UsdSdkImporter importer;
    auto status = OmniConverterStatus::OK;
    std::string error;
    auto stage = importer.ImportStage(context, status, error);
    REQUIRE_MESSAGE(status == OmniConverterStatus::OK, error);
    REQUIRE(stage);
    size_t animatedBones = 0;
    StageUtils::TraverseStageTree(
        stage->rootNode,
        [&](const StageNodePtr& node)
        {
            if (node->isBoneNode && !node->transformAnimationTracks.empty())
            {
                ++animatedBones;
                const auto& samples = node->transformAnimationTracks[0];
                REQUIRE(samples.GetTranslationSamples().size() == 4);
                REQUIRE(samples.GetScaleSamples().size() == 4);
                REQUIRE(samples.GetOrientSamples().size() == 4);
                for (size_t i = 0; i < 4; ++i)
                {
                    CHECK(samples.GetTranslationSamples()[i][0] == doctest::Approx(static_cast<double>(i)));
                    CHECK(samples.GetScaleSamples()[i][0] == doctest::Approx(static_cast<double>(i + 1)));
                    CHECK(samples.GetSampleTime(i) == doctest::Approx(i == 3 ? 2.5 : static_cast<double>(i)));
                }
                CHECK(std::abs(samples.GetOrientSamples()[1].GetReal()) == doctest::Approx(0.9238795));
                CHECK(std::abs(samples.GetOrientSamples().back().GetReal()) == doctest::Approx(0.0));
            }
            return true;
        }
    );
    CHECK(animatedBones == 1);
    {
        UsdExporter exporter(context);
        REQUIRE_MESSAGE(exporter.Export(stage, error) == OmniConverterStatus::OK, error);
        auto output = PXR_NS::UsdStage::Open(outputPath);
        REQUIRE(static_cast<bool>(output));
        // The scene lasts five ticks, but the skeleton reaches its final pose at 2.5.
        CHECK(output->GetEndTimeCode() == doctest::Approx(5.0));
        size_t animations = 0;
        for (const auto& prim : output->Traverse())
        {
            if (auto animation = PXR_NS::UsdSkelAnimation(prim))
            {
                ++animations;
                for (size_t i = 0; i < 4; ++i)
                {
                    const double time = i == 3 ? 2.5 : static_cast<double>(i);
                    PXR_NS::VtVec3fArray positions;
                    REQUIRE(animation.GetTranslationsAttr().Get(&positions, time));
                    REQUIRE(positions.size() == 1);
                    CHECK(positions[0][0] == doctest::Approx(static_cast<double>(i)));
                }
                for (const auto& attribute : { animation.GetTranslationsAttr(), animation.GetScalesAttr(), animation.GetRotationsAttr() })
                {
                    std::vector<double> times;
                    attribute.GetTimeSamples(&times);
                    CHECK(times == std::vector<double>{ 0, 1, 2, 2.5 });
                }
            }
        }
        CHECK(animations == 1);
    }
    fs::remove_all(directory);
}
TEST_CASE("USD fallback timeline uses each animation track rate")
{
    namespace fs = std::experimental::filesystem;
    const std::string directory = std::string(std::getenv("REPO_ROOT_PATH")) + "/_build/tests/cpp/fallback_timeline";
    bool includePointCache = false;
    bool cacheOnly = false;
    SUBCASE("Transform animation")
    {
    }
    SUBCASE("Vertex animation outlasts transform animation")
    {
        includePointCache = true;
    }
    SUBCASE("Vertex animation without transform tracks")
    {
        includePointCache = true;
        cacheOnly = true;
    }
    for (double fps : { 12.0, 24.0, 30.0 })
    {
        for (bool additionalTrack : { false, true })
        {
            CAPTURE(fps);
            CAPTURE(additionalTrack);
            fs::create_directories(directory);
            auto stage = std::make_shared<Stage>();
            stage->rootNode = std::make_shared<StageNode>("Animated");
            stage->rootNode->staticMeshInstances = { 0 };
            auto mesh = TestUtils::MakeTriangle();
            MeshGeomSubset subset;
            subset.faceIndices = { 0 };
            mesh->meshSubsets = { subset };
            stage->meshes = { mesh };
            // FBX imports can describe a one-second clip with frame count and rate only.
            AnimationTrack track;
            track.name = "Motion";
            track.fps = fps;
            track.keyFrames = static_cast<size_t>(fps);
            stage->animationTracks = { track };
            stage->maxKeyFrames = track.keyFrames;
            stage->mutiplier = std::max(1.0, fps / 24.0);
            PXR_NS::VtVec3dArray translations;
            for (size_t i = 0; i < track.keyFrames; ++i)
            {
                translations.push_back(PXR_NS::GfVec3d(static_cast<double>(i), 0, 0));
            }
            stage->rootNode->transformAnimationTracks.emplace_back(translations, PXR_NS::VtQuatdArray(), PXR_NS::VtVec3dArray());
            if (additionalTrack)
            {
                AnimationTrack shorterTrack;
                shorterTrack.name = "Short";
                shorterTrack.duration = 12;
                shorterTrack.keyFrames = 13;
                stage->animationTracks.push_back(shorterTrack);
                stage->rootNode->transformAnimationTracks.emplace_back();
            }
            if (includePointCache)
            {
                mesh->pointCacheTimesamples.resize(static_cast<size_t>(2 * fps), mesh->points);
                stage->maxKeyFrames = mesh->pointCacheTimesamples.size();
                stage->mutiplier = fps / 24.0;
            }
            if (cacheOnly)
            {
                stage->animationTracks.clear();
                stage->rootNode->transformAnimationTracks.clear();
            }
            const std::string outputPath = directory + "/out.usda";
            auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
            context->converterContext = OmniConverterContext(
                "input.fbx",
                outputPath,
                TestUtils::FileCallbacks(),
                directory,
                OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS
            );
            std::string error;
            {
                UsdExporter exporter(context);
                REQUIRE_MESSAGE(exporter.Export(stage, error) == OmniConverterStatus::OK, error);
                auto output = PXR_NS::UsdStage::Open(outputPath);
                REQUIRE(static_cast<bool>(output));
                CHECK(output->GetEndTimeCode() == doctest::Approx(includePointCache ? 48.0 : 24.0));
                if (additionalTrack && !cacheOnly)
                {
                    REQUIRE(output->GetDefaultPrim().GetVariantSet(ANIMATION_TRACK_VARIANT_SET_NAME).SetVariantSelection("motion"));
                }
                size_t animatedAttributes = 0;
                for (const auto& prim : output->Traverse())
                {
                    for (const auto& attr : prim.GetAttributes())
                    {
                        std::vector<double> times;
                        attr.GetTimeSamples(&times);
                        if (!times.empty())
                        {
                            ++animatedAttributes;
                            const bool vertexAnimation = attr.GetName() == PXR_NS::UsdGeomTokens->points ||
                                                         attr.GetName() == PXR_NS::UsdGeomTokens->normals;
                            CHECK(times.back() == doctest::Approx(((vertexAnimation ? 2.0 : 1.0) * fps - 1.0) * 24.0 / fps));
                            CHECK(output->GetEndTimeCode() >= times.back());
                        }
                    }
                }
                CHECK(animatedAttributes > 0);
            }
            fs::remove_all(directory);
        }
    }
}
TEST_CASE("USD skeleton preserves interior fractional poses through conversion")
{
    namespace fs = std::experimental::filesystem;
    const std::string root = std::getenv("REPO_ROOT_PATH");
    const std::string directory = root + "/_build/tests/cpp/skeleton_interior";
    fs::create_directories(directory);
    const auto outputPath = directory + "/out.usda";
    auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
    context->converterContext = OmniConverterContext(
        root + "/data/tests/usda/skeleton_interior_keys.usda",
        outputPath,
        TestUtils::FileCallbacks(),
        directory,
        OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS
    );
    UsdSdkImporter importer;
    auto status = OmniConverterStatus::OK;
    std::string error;
    auto stage = importer.ImportStage(context, status, error);
    REQUIRE_MESSAGE(status == OmniConverterStatus::OK, error);
    REQUIRE(stage);
    UsdExporter exporter(context);
    REQUIRE_MESSAGE(exporter.Export(stage, error) == OmniConverterStatus::OK, error);
    auto output = PXR_NS::UsdStage::Open(outputPath);
    REQUIRE(static_cast<bool>(output));
    CHECK(output->GetEndTimeCode() == doctest::Approx(1.0));
    size_t animations = 0;
    for (const auto& prim : output->Traverse())
    {
        if (auto animation = PXR_NS::UsdSkelAnimation(prim))
        {
            ++animations;
            PXR_NS::VtVec3fArray positions;
            PXR_NS::VtQuatfArray rotations;
            PXR_NS::VtVec3hArray scales;
            REQUIRE(animation.GetTranslationsAttr().Get(&positions, 0.5));
            REQUIRE(positions.size() == 1);
            CHECK(positions[0] == PXR_NS::GfVec3f(2, 0, 0));
            REQUIRE(animation.GetTranslationsAttr().Get(&positions, 1.0));
            CHECK(positions[0] == PXR_NS::GfVec3f(0));
            REQUIRE(animation.GetRotationsAttr().Get(&rotations, 0.25));
            REQUIRE(rotations.size() == 1);
            CHECK(rotations[0].GetReal() == doctest::Approx(0.7071068));
            REQUIRE(animation.GetScalesAttr().Get(&scales, 0.75));
            REQUIRE(scales.size() == 1);
            CHECK(scales[0] == PXR_NS::GfVec3h(2));
            std::vector<double> times;
            REQUIRE(animation.GetTranslationsAttr().GetTimeSamples(&times));
            CHECK(times == std::vector<double>{ 0, 0.25, 0.5, 0.75, 1 });
        }
    }
    CHECK(animations == 1);
    fs::remove_all(directory);
}

#ifndef __aarch64__
TEST_CASE("FBX export preserves interpolated and held transform playback")
{
    namespace fs = std::experimental::filesystem;
    const std::string directory = std::string(std::getenv("REPO_ROOT_PATH")) + "/_build/tests/cpp/fbx_playback";
    fs::create_directories(directory);
    const std::string outputPath = directory + "/motion.fbx";
    bool interpolate = true;
    bool explicitTimes = true;
    double explicitEnd = 25.5;
    PXR_NS::GfQuatd first(1.0), last = PXR_NS::GfRotation(PXR_NS::GfVec3d(0, 0, 1), 90).GetQuat();
    SUBCASE("Sparse fractional keys")
    {
    }
    SUBCASE("Rotation across the angle boundary")
    {
        first = PXR_NS::GfRotation(PXR_NS::GfVec3d(0, 0, 1), 179).GetQuat();
        last = PXR_NS::GfRotation(PXR_NS::GfVec3d(0, 0, 1), -179).GetQuat();
    }
    SUBCASE("Rotation around different axes")
    {
        first = PXR_NS::GfRotation(PXR_NS::GfVec3d(1, 0, 0), 90).GetQuat();
        last = PXR_NS::GfRotation(PXR_NS::GfVec3d(0, 1, 0), 90).GetQuat();
    }
    SUBCASE("Short rotation around different axes")
    {
        explicitEnd = 0.5;
        first = PXR_NS::GfRotation(PXR_NS::GfVec3d(1, 0, 0), 90).GetQuat();
        last = PXR_NS::GfRotation(PXR_NS::GfVec3d(0, 1, 0), 90).GetQuat();
    }
    SUBCASE("Rotation through a gimbal boundary")
    {
        first = MathUtils::EulerAnglesToQuatenion(PXR_NS::GfVec3d(20, 85, 30));
        last = MathUtils::EulerAnglesToQuatenion(PXR_NS::GfVec3d(20, 95, 30));
    }
    SUBCASE("Short rotation through a gimbal boundary")
    {
        explicitEnd = 0.5;
        first = MathUtils::EulerAnglesToQuatenion(PXR_NS::GfVec3d(20, 85, 30));
        last = MathUtils::EulerAnglesToQuatenion(PXR_NS::GfVec3d(20, 95, 30));
    }
    SUBCASE("Explicit held poses")
    {
        interpolate = false;
    }
    SUBCASE("Legacy regular samples")
    {
        interpolate = explicitTimes = false;
    }
    const double end = explicitTimes ? explicitEnd : 1.0;
    TransformTimesamples samples(
        PXR_NS::VtVec3dArray{ { 0, 0, 0 }, { 24, 0, 0 } },
        PXR_NS::VtQuatdArray{ first, last },
        PXR_NS::VtVec3dArray{ { 1, 1, 1 }, { 3, 3, 3 } }
    );
    if (explicitTimes)
    {
        for (auto channel : { TransformChannel::Translation, TransformChannel::Rotation, TransformChannel::Scale })
        {
            samples.SetSampleTimes({ 0.0, end }, channel, interpolate);
        }
    }
    auto stage = std::make_shared<Stage>();
    stage->rootNode = std::make_shared<StageNode>("Scene");
    auto moving = std::make_shared<StageNode>("Moving");
    moving->isBoneNode = true;
    moving->parent = stage->rootNode;
    moving->transformAnimationTracks.push_back(samples);
    stage->rootNode->children.push_back(moving);
    stage->animationTracks.resize(1);
    stage->animationTracks[0].name = "Motion";
    stage->animationTracks[0].fps = 24;
    stage->animationTracks[0].duration = end;
    auto callbacks = TestUtils::FileCallbacks();
    callbacks.binaryWriteCallback = TestUtils::WriteBinaryFile;
    auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
    context->converterContext = OmniConverterContext("input.usda", outputPath, callbacks, directory, OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS);
    auto exporter = CreateExporter(context);
    std::string error;
    REQUIRE_MESSAGE(exporter->Export(stage, error) == OmniConverterStatus::OK, error);
    const auto destroy = [](fbxsdk::FbxManager* manager)
    {
        manager->Destroy();
    };
    std::unique_ptr<fbxsdk::FbxManager, decltype(destroy)> manager(fbxsdk::FbxManager::Create(), destroy);
    manager->SetIOSettings(fbxsdk::FbxIOSettings::Create(manager.get(), IOSROOT));
    auto scene = fbxsdk::FbxScene::Create(manager.get(), "Playback");
    auto reader = fbxsdk::FbxImporter::Create(manager.get(), "Reader");
    REQUIRE(reader->Initialize(outputPath.c_str(), -1, manager->GetIOSettings()));
    REQUIRE(reader->Import(scene));
    auto node = scene->FindNodeByName("Moving");
    REQUIRE(node);
    auto stack = scene->GetSrcObject<fbxsdk::FbxAnimStack>(0);
    REQUIRE(stack);
    scene->SetCurrentAnimationStack(stack);
    auto layer = stack->GetMember<fbxsdk::FbxAnimLayer>(0);
    auto curve = node->LclTranslation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X);
    REQUIRE(curve);
    auto rotationCurve = node->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Z);
    REQUIRE(rotationCurve);
    CHECK(rotationCurve->KeyGetCount() <= (interpolate ? 128 : 2));
    CHECK(curve->KeyGetCount() == 2);
    CHECK(curve->KeyGetTime(1).GetSecondDouble() == doctest::Approx(end / 24.0));
    CHECK(
        curve->KeyGetInterpolation(0) ==
        (interpolate ? fbxsdk::FbxAnimCurveDef::eInterpolationLinear : fbxsdk::FbxAnimCurveDef::eInterpolationConstant)
    );
    for (int sample = 0; sample <= 100; ++sample)
    {
        const double fraction = sample / 100.0;
        CAPTURE(fraction);
        fbxsdk::FbxTime time;
        time.SetSecondDouble(end * fraction / 24.0);
        const auto transform = node->EvaluateLocalTransform(time);
        if (interpolate)
        {
            CHECK(transform.GetT()[0] == doctest::Approx(24 * fraction));
            CHECK(transform.GetS()[0] == doctest::Approx(1 + 2 * fraction));
            const auto actual = transform.GetQ();
            const auto expected = PXR_NS::GfSlerp(fraction, first, last);
            const auto im = expected.GetImaginary();
            const double dot = actual[3] * expected.GetReal() + actual[0] * im[0] + actual[1] * im[1] + actual[2] * im[2];
            CHECK(std::abs(dot) >= 0.9999996); // Less than 0.11 degrees of playback error.
        }
        else
        {
            // Retain discrete playback instead of turning held samples into a glide.
            CHECK((transform.GetT()[0] == 0.0 || transform.GetT()[0] == 24.0));
            CHECK((transform.GetS()[0] == 1.0 || transform.GetS()[0] == 3.0));
            CHECK(node->GetQuaternionInterpolation(fbxsdk::FbxNode::eSourcePivot) == fbxsdk::eQuatInterpOff);
        }
    }
    manager.reset();
    fs::remove_all(directory);
}
#endif

TEST_CASE("USD skeleton holds its final pose when keys precede the scene start")
{
    namespace fs = std::experimental::filesystem;
    const std::string root = std::getenv("REPO_ROOT_PATH");
    const std::string directory = root + "/_build/tests/cpp/skeleton_held_before_start";
    fs::create_directories(directory);
    const std::string outputPath = directory + "/out.usda";
    auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
    context->converterContext = OmniConverterContext(
        root + "/data/tests/usda/skeleton_held_before_start.usda",
        outputPath,
        TestUtils::FileCallbacks(),
        directory,
        OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS
    );
    UsdSdkImporter importer;
    auto status = OmniConverterStatus::OK;
    std::string error;
    auto stage = importer.ImportStage(context, status, error);
    REQUIRE_MESSAGE(status == OmniConverterStatus::OK, error);
    REQUIRE(stage);
    {
        UsdExporter exporter(context);
        REQUIRE_MESSAGE(exporter.Export(stage, error) == OmniConverterStatus::OK, error);
        auto output = PXR_NS::UsdStage::Open(outputPath);
        REQUIRE(static_cast<bool>(output));
        CHECK(output->GetEndTimeCode() == doctest::Approx(2.0));
        size_t animations = 0;
        for (const auto& prim : output->Traverse())
        {
            if (auto animation = PXR_NS::UsdSkelAnimation(prim))
            {
                ++animations;
                for (double time : { 0.0, 1.0, 2.0 })
                {
                    PXR_NS::VtVec3fArray positions;
                    PXR_NS::VtVec3hArray scales;
                    PXR_NS::VtQuatfArray rotations;
                    REQUIRE(animation.GetTranslationsAttr().Get(&positions, time));
                    REQUIRE(animation.GetScalesAttr().Get(&scales, time));
                    REQUIRE(animation.GetRotationsAttr().Get(&rotations, time));
                    REQUIRE(positions.size() == 1);
                    REQUIRE(scales.size() == 1);
                    REQUIRE(rotations.size() == 1);
                    CHECK(positions[0] == PXR_NS::GfVec3f(3, 0, 0));
                    CHECK(scales[0] == PXR_NS::GfVec3h(4));
                    CHECK(std::abs(rotations[0].GetImaginary()[2]) == doctest::Approx(1.0));
                }
            }
        }
        CHECK(animations == 1);
    }
    fs::remove_all(directory);
}
