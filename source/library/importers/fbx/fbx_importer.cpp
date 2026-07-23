// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "fbx_importer.h"

#include "../../common/common.h"
#include "../../common/custom_fbx_io.h"
#include "../../common/fbx_common.h"
#include "../../utils/utils.h"

#include <experimental/filesystem>
#include <fbxsdk.h>
#include <numeric>

namespace fs = std::experimental::filesystem;


namespace
{
template <typename T>
bool AllGfElementsAreEqual(const T& samples)
{
    if (samples.empty())
    {
        return true;
    }

    using ElementType = typename T::ElementType;
    return std::all_of(
        samples.begin(),
        samples.end(),
        [samples](const ElementType& sample)
        {
            return PXR_NS::GfIsClose(sample, samples[0], 1e-6);
        }
    );
}

template <>
bool AllGfElementsAreEqual(const PXR_NS::VtQuatdArray& samples)
{
    if (samples.empty())
    {
        return true;
    }

    return std::all_of(
        samples.begin(),
        samples.end(),
        [samples](const PXR_NS::GfQuatd& sample)
        {
            return PXR_NS::GfIsClose(sample.GetReal(), samples[0].GetReal(), 1e-6) &&
                   PXR_NS::GfIsClose(sample.GetImaginary(), samples[0].GetImaginary(), 1e-6);
        }
    );
}
} // namespace

static PXR_NS::GfMatrix4d FbxAMatrixToGfMatrix(const fbxsdk::FbxAMatrix& matrix)
{
    // FBX Matrix is saved in row order as the same as USD.
    return PXR_NS::GfMatrix4d(
        matrix.Get(0, 0),
        matrix.Get(0, 1),
        matrix.Get(0, 2),
        matrix.Get(0, 3),
        matrix.Get(1, 0),
        matrix.Get(1, 1),
        matrix.Get(1, 2),
        matrix.Get(1, 3),
        matrix.Get(2, 0),
        matrix.Get(2, 1),
        matrix.Get(2, 2),
        matrix.Get(2, 3),
        matrix.Get(3, 0),
        matrix.Get(3, 1),
        matrix.Get(3, 2),
        matrix.Get(3, 3)
    );
}

static PXR_NS::GfVec3d FbxVector4dToGfVector3d(const fbxsdk::FbxVector4& vector)
{
    return PXR_NS::GfVec3d(vector[0], vector[1], vector[2]);
}

static PXR_NS::GfVec3f FbxVector4dToGfVector3f(const fbxsdk::FbxVector4& vector)
{
    return PXR_NS::GfVec3f(vector[0], vector[1], vector[2]);
}

static PXR_NS::GfVec2f FbxVector2dToGfVector2f(const fbxsdk::FbxVector2& vector)
{
    return PXR_NS::GfVec2f(vector[0], vector[1]);
}

static PXR_NS::GfVec3f FbxColorToGfVector3f(const fbxsdk::FbxColor& color)
{
    return PXR_NS::GfVec3f(color[0], color[1], color[2]);
}

static bool IsReverseRotation(PXR_NS::GfVec3d r1, PXR_NS::GfVec3d r2)
{
    return (abs(r1[0] - r2[0]) > 359) || (abs(r1[1] - r2[1]) > 359) || (abs(r1[2] - r2[2]) > 359);
}

static bool IsHorizontalRotation(PXR_NS::GfVec3d r1, PXR_NS::GfVec3d r2)
{
    return (abs(r1[0] - r2[0]) > 179) || (abs(r1[1] - r2[1]) > 179) || (abs(r1[2] - r2[2]) > 179);
}

// Detect if the rotation in the middle of three consecutive ratationss is an outlier
static bool IsHorizontalRotation(PXR_NS::GfVec3d r1, PXR_NS::GfVec3d r2, PXR_NS::GfVec3d r3)
{
    return IsHorizontalRotation(r1, r2) && IsHorizontalRotation(r2, r3);
}

std::string FbxSdkImporter::ComputeHash(const OmniFutureThreadContextPtr& context)
{
    return std::string();
}

#if defined(_WIN32) || defined(_WIN64)

// FBX seems to require all allocations to be 16 byte aligned
// mimalloc MI_MAX_ALIGN_SIZE   defaults to 16; but only when allocation is over 16 bytes
// https://github.com/microsoft/mimalloc/issues/199#issuecomment-582494965
//
constexpr size_t kMiMallocMinumSizeFor16ByteAlignment = 16;

static void* miMallocAlignmentOverride(size_t size)
{
    return malloc(size >= kMiMallocMinumSizeFor16ByteAlignment ? size : 16);
}

static void* miMallocAlignmentOverrideCalloc(size_t number, size_t size)
{
    if (size >= kMiMallocMinumSizeFor16ByteAlignment)
    {
        return ::calloc(number, size);
    }

    size = number * size;
    void* ptr = miMallocAlignmentOverride(size);
    if (ptr)
    {
        ::memset(ptr, 0, size);
    }
    return ptr;
}

static void* miMallocAlignmentOverrideRealloc(void* srcPtr, size_t size)
{
    if (size >= kMiMallocMinumSizeFor16ByteAlignment)
    {
        return ::realloc(srcPtr, size);
    }

    void* dstPptr = miMallocAlignmentOverride(size);
    if (srcPtr)
    {
        ::memcpy(dstPptr, srcPtr, size);
    }
    return dstPptr;
}

#endif

FbxSdkImporter::FbxSdkImporter()
{
#if defined(_WIN32) || defined(_WIN64)
    FbxSetMallocHandler(miMallocAlignmentOverride);
    FbxSetCallocHandler(miMallocAlignmentOverrideCalloc);
    FbxSetReallocHandler(miMallocAlignmentOverrideRealloc);
    FbxSetFreeHandler(::free); // Can just use normal free since these routines just force a mimimum allocation of 16 bytes
#endif
}

StagePtr FbxSdkImporter::ImportStage(const OmniFutureThreadContextPtr& context, OmniConverterStatus& status, std::string& detailedError)
{
    mThreadContext = context;
    status = OmniConverterStatus::OK;
    Log("Starting to import asset with FBX importer.");

    std::lock_guard<std::mutex> lock(gFbxManagerLock);
    auto manager = FbxManagerCreator();
    auto ioSettings = FbxSettingsCreator(manager.get(), IOSROOT);
    manager->SetIOSettings(ioSettings.get());

    auto importScene = FbxSceneCreator(manager.get(), "");
    auto importer = FbxImporterCreator(manager.get(), "rb");
    auto meshConverter = FbxGeometryConverter(manager.get());

    std::string importAssetPath = mThreadContext->converterContext.GetImportAssetPath();
    std::string localPathWithoutScheme = importAssetPath;
    if (PathUtils::GetUrlScheme(localPathWithoutScheme) == "file")
    {
        // Omniverse uses URLs, and local paths are mapped to scheme file:.
        // It needs to be removed as FBX SDK cannot accept path like that. And
        // This is applied to non-customized IO.
        localPathWithoutScheme = localPathWithoutScheme.substr(5);

#if defined(_WIN32)
        // Strip off slashes ahead of disk drive symbol.
        while (localPathWithoutScheme.size() > 0 && localPathWithoutScheme[0] == '/')
        {
            localPathWithoutScheme = localPathWithoutScheme.substr(1);
        }
#endif
    }


    std::unique_ptr<CustomFBXReadStream> fbxReadStream = nullptr;
    bool importSuccess = false;
    // Try to load file without customized IO as it has 2G size limit. Only local file is supported.
    std::error_code ec;
    const auto localPath = fs::u8path(localPathWithoutScheme);
    if (fs::exists(localPath, ec))
    {
        const auto fileSize = fs::file_size(localPath, ec);
        if (!ec && fileSize == 0)
        {
            detailedError = "Asset " + importAssetPath + " is empty.";
            status = OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT;
            Log(detailedError);
            return nullptr;
        }
        importSuccess = importer->Initialize(localPathWithoutScheme.c_str());
    }
    else
    {
        auto fbxBlobPtr = mThreadContext->converterContext.ReadFile(importAssetPath);
        if (!fbxBlobPtr)
        {
            detailedError = "Failed to read asset " + importAssetPath;
            status = OmniConverterStatus::FILE_READ_ERROR;
            Log(detailedError);
            return nullptr;
        }

        if (!fbxBlobPtr->buffer || fbxBlobPtr->size == 0)
        {
            detailedError = "Asset " + importAssetPath + " is empty.";
            status = OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT;
            Log(detailedError);
            return nullptr;
        }

        fbxReadStream.reset(new CustomFBXReadStream(manager.get(), fbxBlobPtr));
        importSuccess = importer->Initialize(fbxReadStream.get());
    }

    if (importSuccess)
    {
        importSuccess = importer->Import(importScene.get());
        if (!importSuccess)
        {
            status = OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT;
        }
    }
    else
    {
        status = OmniConverterStatus::UNSUPPORTED_IMPORT_FORMAT;
    }

    std::shared_ptr<Stage> stage = nullptr;
    if (!importSuccess)
    {
        detailedError = "Failed to import asset " + importAssetPath;
        Log(detailedError);
    }
    else
    {
        auto rootNode = importScene->GetRootNode();

        stage = std::make_shared<Stage>();
        if (mThreadContext->converterContext.PopulateMaterialsOnly())
        {
            PopulateAllMaterials(importScene.get(), rootNode, stage);
        }
        else
        {
            double unitsScale = 1.0;
            PopulateStageInfo(importer.get(), importScene.get(), stage, unitsScale);
            PopulateStageTree(importScene.get(), rootNode, stage, nullptr, nullptr, false, unitsScale);
            PopulateAllMeshes(importScene.get(), stage, unitsScale, meshConverter);
            BakingScales(stage);
        }
    }

    importScene.reset();
    importer.reset();
    ioSettings.reset();
    manager.reset();
    fbxReadStream.reset();

    return stage;
}

void FbxSdkImporter::Log(const std::string& message)
{
    mThreadContext->converterContext.Log(message.c_str());
}

void FbxSdkImporter::PopulateStageInfo(fbxsdk::FbxImporter* importer, fbxsdk::FbxScene* scene, const StagePtr& stage, double& unitsScale)
{
    if (mThreadContext->converterContext.ConvertFbxToYUp())
    {
        fbxsdk::FbxAxisSystem axisSystem(fbxsdk::FbxAxisSystem::eMayaYUp);
        axisSystem.ConvertScene(scene);
    }
    else if (mThreadContext->converterContext.ConvertFbxToZUp())
    {
        fbxsdk::FbxAxisSystem axisSystem(fbxsdk::FbxAxisSystem::eMayaZUp);
        axisSystem.ConvertScene(scene);
    }
    int dir;
    const auto& upVector = scene->GetGlobalSettings().GetAxisSystem().GetUpVector(dir);
    stage->yAxis = upVector != fbxsdk::FbxAxisSystem::eZAxis;

    // override up-axis with user-defined value
    if (mThreadContext->converterContext.ConvertUpY())
    {
        stage->yAxis = true;
    }
    else if (mThreadContext->converterContext.ConvertUpZ())
    {
        stage->yAxis = false;
    }

    auto systemUnit = scene->GetGlobalSettings().GetSystemUnit();

    double scaledUnitsAsCentimeters = systemUnit.GetScaleFactor();
    if (mThreadContext->converterContext.KeepAssetUnits())
    {
        stage->worldUnits = scaledUnitsAsCentimeters / 100.0;
        unitsScale = 1.0;
    }
    else if (mThreadContext->converterContext.UseMeterPerUnit())
    {
        stage->worldUnits = 1.0;
        unitsScale = scaledUnitsAsCentimeters / 100.0; // Meters per unit
    }
    else
    {
        stage->worldUnits = 0.01;
        unitsScale = scaledUnitsAsCentimeters;
    }

    auto info = scene->GetSceneInfo();
    auto applicationName = info->Original_ApplicationName.Get();
    if (applicationName.Find("Blender") == 0)
    {
        stage->isExportFromBlender = true;
    }

    if (!mThreadContext->converterContext.IgnoreAnimations())
    {
        size_t maxKeyFrames = 0;
        double fps = GetFramerate(scene);
        double multiplier = fps / 24.0;
        if (multiplier < 1.0)
        {
            multiplier = 1.0;
        }
        for (size_t i = 0; i < scene->GetSrcObjectCount<fbxsdk::FbxAnimStack>(); i++)
        {
            fbxsdk::FbxAnimStack* animStack = scene->GetSrcObject<fbxsdk::FbxAnimStack>(i);
            AnimationTrack animationTrack;
            animationTrack.name = animStack->GetName();
            if (animationTrack.name.substr(0, 16) == "AnimationStack::")
            {
                animationTrack.name = animationTrack.name.substr(16);
            }
            else if (animationTrack.name.substr(0, 11) == "AnimStack::")
            {
                animationTrack.name = animationTrack.name.substr(11);
            }

            fbxsdk::FbxTime localStart = animStack->LocalStart.Get();
            fbxsdk::FbxTime localStop = animStack->LocalStop.Get();
            fbxsdk::FbxTime duration = localStop - localStart;
            animationTrack.keyFrames = (size_t)(duration.GetSecondDouble() * fps);
            if (animationTrack.keyFrames != 0)
            {
                if (animationTrack.keyFrames > maxKeyFrames)
                {
                    maxKeyFrames = animationTrack.keyFrames;
                    stage->mutiplier = multiplier;
                }
                animationTrack.fps = fps;
                stage->animationTracks.push_back(animationTrack);
            }
        }

        stage->maxKeyFrames = maxKeyFrames;

        for (size_t i = 0; i < scene->GetSrcObjectCount<fbxsdk::FbxConstraintParent>(); i++)
        {
            // Only one source is supported at this moment
            fbxsdk::FbxConstraintParent* constraintRelationship = scene->GetSrcObject<fbxsdk::FbxConstraintParent>(i);
            Constraint constraint;
            constraint.targetNode = constraintRelationship->GetConstraintSource(0);
            constraint.translationOffset = constraintRelationship->GetTranslationOffset(0) * unitsScale;
            constraint.rotationOffset = constraintRelationship->GetRotationOffset(0);
            mNodeConstraints.insert({ constraintRelationship->GetConstrainedObject(), constraint });
        }
    }
}

void FbxSdkImporter::PopulateAllMaterials(fbxsdk::FbxScene* scene, fbxsdk::FbxNode* currentNode, const StagePtr& stage)
{
    if (!currentNode)
    {
        return;
    }

    size_t materialCount = currentNode->GetMaterialCount();
    if (materialCount > 0)
    {
        for (size_t materialIndex = 0; materialIndex < materialCount; materialIndex++)
        {
            fbxsdk::FbxSurfaceMaterial* surfaceMaterial = currentNode->GetMaterial(materialIndex);
            if (mFbxMaterialToMaterialIndex.count(surfaceMaterial) == 0)
            {
                auto material = LoadMaterial(stage, surfaceMaterial);
                if (material)
                {
                    size_t index = AddMaterial(stage, material);
                    mFbxMaterialToMaterialIndex.insert({ surfaceMaterial, index });
                }
            }
        }
    }

    for (size_t i = 0; i < currentNode->GetChildCount(); i++)
    {
        PopulateAllMaterials(scene, currentNode->GetChild(i), stage);
    }
}

void FbxSdkImporter::PopulateStageTree(
    fbxsdk::FbxScene* scene,
    fbxsdk::FbxNode* currentNode,
    const StagePtr& stage,
    const StageNodePtr& parentStageNode,
    fbxsdk::FbxNode** currentSkeletonRoot,
    bool hasGeometricOpsInParent,
    double scale
)
{
    if (!currentNode)
    {
        return;
    }

    StageNodePtr stageNode = std::make_shared<StageNode>(currentNode->GetName(), true);
    stageNode->parent = parentStageNode;
    if (!parentStageNode)
    {
        stage->rootNode = stageNode;
    }
    else
    {
        parentStageNode->children.push_back(stageNode);
    }

    stageNode->worldTransformMatrix = GetNodeGlobalMatrix(currentNode, scale);
    if (hasGeometricOpsInParent || HasGeometricTransforms(currentNode))
    {
        hasGeometricOpsInParent = true;
        if (parentStageNode)
        {
            auto localMatrix = stageNode->worldTransformMatrix * parentStageNode->worldTransformMatrix.GetInverse();
            stageNode->localTransform = Transform(localMatrix);
        }
        else
        {
            stageNode->localTransform = Transform(stageNode->worldTransformMatrix);
        }
    }
    else
    {
        stageNode->localTransform = GetNodeLocalTransform(currentNode, scale);
    }

    if (!mThreadContext->converterContext.IgnoreAnimations())
    {
        stageNode->transformAnimationTracks = GetNodeAnimation(stage->animationTracks, scene, currentNode, stageNode->localTransform, scale);
        // if (stageNode->transformAnimationTracks.empty())
        {
            FbxEuler::EOrder rotationOrder;
            currentNode->GetRotationOrder(FbxNode::eSourcePivot, rotationOrder);
            if (rotationOrder != FbxEuler::eOrderXYZ)
            {
                stageNode->useOrderForAnimation = true;
                auto localRotation = stageNode->localTransform.GetRotationXYZ();
                auto rotation = ChangeRotationOrderToXYZ(localRotation, rotationOrder);
                Transform tranform(
                    stageNode->localTransform.GetTES().t,
                    FbxVector4dToGfVector3d(rotation.GetR()),
                    stageNode->localTransform.GetTES().s
                );
                stageNode->orderTransform = tranform.GetMatrix();
            }
        }
    }

    bool skipChildren = false;
    if (auto attribute = currentNode->GetNodeAttribute())
    {
        fbxsdk::FbxNodeAttribute::EType attributeType = attribute->GetAttributeType();
        switch (attributeType)
        {
            case fbxsdk::FbxNodeAttribute::eNull:
            {
                if (stage->isExportFromBlender)
                {
                    auto parentNode = stageNode->parent.lock();
                    if (!parentNode || !parentNode->isBoneNode && currentNode->GetChildCount())
                    {
                        // For Blender exported fbx file,
                        // Should treat null node  as a special bone node,
                        // it usually is the SkelRoot in USD.
                        stageNode->isBoneNode = true;
                    }
                }
                break;
            }
            case fbxsdk::FbxNodeAttribute::eMesh:
            {
                // Delay mesh population since it needs scene graph information.
                fbxsdk::FbxMesh* fbxMesh = currentNode->GetMesh();
                if (fbxMesh && fbxMesh->GetControlPointsCount() != 0)
                {
                    size_t skinDeformerCount = fbxMesh->GetDeformerCount(fbxsdk::FbxDeformer::eSkin);
                    auto iter = mFbxMeshToMeshIndex.find(fbxMesh);
                    if (iter != mFbxMeshToMeshIndex.end())
                    {
                        auto& meshInfo = mMeshInfos[iter->second];
                        skipChildren = meshInfo.mergeChildren;
                        meshInfo.attachedNodes.push_back(stageNode);
                    }
                    else
                    {
                        MeshInfo meshInfo;
                        meshInfo.attachedNodes.push_back(stageNode);
                        size_t meshIndex = mFbxMeshToMeshIndex.size();
                        iter = mFbxMeshToMeshIndex.insert({ fbxMesh, meshIndex }).first;
                        meshInfo.geomBindTransform = stageNode->worldTransformMatrix;

                        // Moving the mesh parent's scale into mesh if the mesh's parent has time sample animation
                        if (stageNode->parent.lock()->transformAnimationTracks.size() > 0)
                        {
                            auto scale = stageNode->parent.lock()->localTransform.GetTES().s;
                            auto scaleMatrix = PXR_NS::GfMatrix4d(1.0);
                            scaleMatrix.SetScale(scale);
                            meshInfo.geomBindTransform = scaleMatrix * stageNode->worldTransformMatrix;
                        }
                        // FIXME: Substance Painter will export mesh with splitted parts even with the same
                        // material. It needs to merge them together. The merge rules are:
                        // 1. Children must have the same node name.
                        // 2. Children should not have any descendant nodes.
                        // 3. Children must be mesh only.
                        // 4. No skinning
                        if (skinDeformerCount == 0)
                        {
                            skipChildren = true;
                            for (size_t i = 0; i < currentNode->GetChildCount(); i++)
                            {
                                auto child = currentNode->GetChild(i);
                                auto childName = child->GetName();
                                size_t childrenCount = child->GetChildCount();
                                if (std::string(childName) != stageNode->name || childrenCount > 0 || !attribute ||
                                    attribute->GetAttributeType() != fbxsdk::FbxNodeAttribute::eMesh)
                                {
                                    skipChildren = false;
                                    break;
                                }
                            }
                            meshInfo.mergeChildren = skipChildren;
                        }
                        mMeshInfos.push_back(meshInfo);
                    }

                    if (skinDeformerCount == 0)
                    {
                        stageNode->staticMeshInstances.push_back(iter->second);
                        mMeshInfos[iter->second].instanceCount += 1;
                    }
                }

                break;
            }
            case fbxsdk::FbxNodeAttribute::eCamera:
                if (!mThreadContext->converterContext.IgnoreCameras())
                {
                    PopulateCamera(scene, currentNode, stage, stageNode, scale);
                }
                break;

            case fbxsdk::FbxNodeAttribute::eLight:
                if (!mThreadContext->converterContext.IgnoreLights())
                {
                    PopulateLight(scene, currentNode, stage, stageNode);
                }
                break;

            case fbxsdk::FbxNodeAttribute::eSkeleton:
            {
                stageNode->isBoneNode = true;
                fbxsdk::FbxSkeleton* skeleton = currentNode->GetSkeleton();
                const char* name = nullptr;
                if (skeleton)
                {
                    name = skeleton->GetName();
                }

                if (name && name[0] != '\0')
                {
                    stageNode->name = name;
                }

                break;
            }
            default:
                break;
        }
    }

    if (stageNode->IsRootBone())
    {
        currentSkeletonRoot = &currentNode;
    }

    if (stageNode->isBoneNode)
    {
        stageNode->restTransform = stageNode->localTransform.GetMatrix();
        const auto& parentNode = stageNode->parent.lock();
        if (stageNode->IsRootBone())
        {
            stageNode->bindTransform = stageNode->restTransform;
        }
        else
        {
            stageNode->bindTransform = stageNode->localTransform.GetMatrix() * parentNode->bindTransform;
        }

        if (!mThreadContext->converterContext.IgnoreAnimations())
        {
            auto iter = mNodeConstraints.find(currentNode);
            if (iter != mNodeConstraints.end())
            {
                // It only supports to constrain a bone node to another bone node.
                const Constraint& constraint = iter->second;
                auto boneIndexIter = mNodeBoneIndex.find(constraint.targetNode);
                if (boneIndexIter != mNodeBoneIndex.end())
                {
                    auto parentNode = mBoneList[boneIndexIter->second];
                    stageNode->transformAnimationTracks = parentNode->transformAnimationTracks;
                }
            }
        }

        mBoneList.push_back(stageNode);
        mNodeBoneIndex[currentNode] = mBoneList.size() - 1;
        mBoneNodeRoot.insert({ currentNode, *currentSkeletonRoot });
    }

    if (!skipChildren)
    {
        for (size_t i = 0; i < currentNode->GetChildCount(); i++)
        {
            PopulateStageTree(scene, currentNode->GetChild(i), stage, stageNode, currentSkeletonRoot, hasGeometricOpsInParent, scale);
        }
    }

    if (stageNode->IsRootBone())
    {
        *currentSkeletonRoot = nullptr;
    }
}

void FbxSdkImporter::PopulateAllMeshes(fbxsdk::FbxScene* scene, const StagePtr& stage, double scale, fbxsdk::FbxGeometryConverter& converter)
{
    stage->meshes.resize(mFbxMeshToMeshIndex.size());
    for (const auto& fbxMeshInfo : mFbxMeshToMeshIndex)
    {
        auto fbxMesh = fbxMeshInfo.first;
        auto& meshInfo = mMeshInfos[fbxMeshInfo.second];
        // usd not support one polygon mesh, need triangulate here
        if (fbxMesh->GetPolygonCount() == 1)
        {
            auto fbxMeshNode = converter.Triangulate(fbxMesh, true, false);
            // if Triangulate fails, it returns NULL and keeps original node attribute.
            fbxMesh = (fbxMeshNode == NULL) ? fbxMesh : static_cast<fbxsdk::FbxMesh*>(fbxMeshNode);
        }
        auto mesh = PopulateMesh(scene, fbxMesh, stage, scale, meshInfo.mergeChildren);
        meshInfo.mesh = mesh;

        stage->meshes[fbxMeshInfo.second] = mesh;

        // Populate skinning information
        std::vector<VertexBoneData> vertexBoneDataArray;
        vertexBoneDataArray.resize(mesh->points.size());
        size_t skinDeformerCount = fbxMesh->GetDeformerCount(fbxsdk::FbxDeformer::eSkin);
        SkinMeshPtr skinMeshPtr;
        for (size_t i = 0; i < skinDeformerCount; i++)
        {
            auto skinDeformer = (fbxsdk::FbxSkin*)fbxMesh->GetDeformer(i, fbxsdk::FbxDeformer::eSkin);

            size_t clusterSize = skinDeformer->GetClusterCount();
            for (size_t j = 0; j < clusterSize; j++)
            {
                auto cluster = skinDeformer->GetCluster(j);
                auto linkNode = cluster->GetLink();
                if (!linkNode)
                {
                    continue;
                }

                size_t index = mNodeBoneIndex[linkNode];
                auto bone = mBoneList[index];
                fbxsdk::FbxAMatrix offsetFbxMatrix;
                cluster->GetTransformLinkMatrix(offsetFbxMatrix);
                PXR_NS::GfMatrix4d offsetMatrix = FbxAMatrixToGfMatrix(offsetFbxMatrix);
                const auto& translation = offsetMatrix.ExtractTranslation();
                offsetMatrix.SetTranslateOnly(translation * scale);
                bone->bindTransform = offsetMatrix;

                size_t controlPointSize = cluster->GetControlPointIndicesCount();
                int* indices = cluster->GetControlPointIndices();
                double* weights = cluster->GetControlPointWeights();
                // Ability to preserve original skeleton when converting from FBX to USD
                if (!mThreadContext->converterContext.IgnoreUnboundBones() || (controlPointSize != 0 && indices && weights))
                {
                    if (!skinMeshPtr)
                    {
                        skinMeshPtr = std::make_shared<SkinMesh>(fbxMeshInfo.second);
                        auto rootBone = mBoneNodeRoot[linkNode];
                        size_t index = mNodeBoneIndex[rootBone];
                        skinMeshPtr->skeletonRoot = mBoneList[index];
                    }

                    skinMeshPtr->influencedBoneNodes.push_back(bone);
                }
                if (controlPointSize != 0 && indices && weights)
                {
                    for (size_t k = 0; k < controlPointSize; k++)
                    {
                        vertexBoneDataArray[indices[k]].addBoneData(skinMeshPtr->influencedBoneNodes.size() - 1, weights[k]);
                    }
                }
            }
        }

        if (skinMeshPtr)
        {
            PXR_NS::GfMatrix4d rootBoneParentTransform(1.0);
            auto rootBoneParent = skinMeshPtr->skeletonRoot->parent.lock();
            // It needs to cancel out parent transform since mesh is binding in the local space of skeleton.
            if (rootBoneParent)
            {
                rootBoneParentTransform = rootBoneParent->worldTransformMatrix;
            }
            skinMeshPtr->geomBindTransform = mMeshInfos[fbxMeshInfo.second].geomBindTransform * rootBoneParentTransform.GetInverse();

            size_t maxInfluencedBones = 0;
            for (const auto& vertexBoneData : vertexBoneDataArray)
            {
                if (vertexBoneData.ids.size() > maxInfluencedBones)
                {
                    maxInfluencedBones = vertexBoneData.ids.size();
                }
            }

            for (const auto& boneData : vertexBoneDataArray)
            {
                for (size_t j = 0; j < boneData.weights.size(); j++)
                {
                    skinMeshPtr->jointWeights.push_back(boneData.weights[j]);
                    skinMeshPtr->jointInfluences.push_back((int)boneData.ids[j]);
                }

                for (size_t j = boneData.weights.size(); j < maxInfluencedBones; j++)
                {
                    skinMeshPtr->jointWeights.push_back(0);
                    skinMeshPtr->jointInfluences.push_back(0);
                }
            }
            skinMeshPtr->numBoneInfluencesPerVertex = maxInfluencedBones;

            stage->skinMeshes.push_back(skinMeshPtr);
        }
    }

    for (auto& bone : mBoneList)
    {
        if (bone->IsRootBone())
        {
            bone->restTransform = bone->bindTransform;
        }
        else
        {
            auto parentBone = bone->parent.lock();
            bone->restTransform = bone->bindTransform * parentBone->bindTransform.GetInverse();
        }
    }
}

MeshPtr FbxSdkImporter::PopulateMesh(fbxsdk::FbxScene* scene, fbxsdk::FbxMesh* fbxMesh, const StagePtr& stage, double scale, bool mergeMeshChildren)
{
    MeshPtr baseMesh = PopulateMeshInternal(scene, fbxMesh, stage, scale);
    // Merge children meshes. This is for avoiding importing separate meshes exported from Substance Painter.
    if (mergeMeshChildren)
    {
        auto meshNode = fbxMesh->GetNode();
        for (size_t i = 0; i < meshNode->GetChildCount(); i++)
        {
            fbxsdk::FbxMesh* fbxChildMesh = meshNode->GetChild(i)->GetMesh();
            MeshPtr childMesh = PopulateMeshInternal(scene, fbxChildMesh, stage, scale);
            StageUtils::MergeMesh(baseMesh, childMesh);
        }
    }

    return baseMesh;
}

MeshPtr FbxSdkImporter::PopulateMeshInternal(fbxsdk::FbxScene* scene, fbxsdk::FbxMesh* fbxMesh, const StagePtr& stage, double scale)
{
    MeshPtr mesh = std::make_shared<Mesh>();
    const char* meshName = fbxMesh->GetName();
    const fbxsdk::FbxNode* currentNode = fbxMesh->GetNode();
    if (meshName && meshName[0] != '\0')
    {
        mesh->name = meshName;
    }
    else
    {
        mesh->name = currentNode->GetName();
    }

    for (size_t i = 0; i < fbxMesh->GetControlPointsCount(); i++)
    {
        mesh->points.push_back(FbxVector4dToGfVector3f(fbxMesh->GetControlPointAt(i)) * scale);
    }

    auto faceIndices = fbxMesh->GetPolygonVertices();
    mesh->faceVertexIndices.assign(faceIndices, faceIndices + fbxMesh->GetPolygonVertexCount());
    for (size_t i = 0; i < fbxMesh->GetPolygonCount(); i++)
    {
        size_t polygonSize = fbxMesh->GetPolygonSize(i);
        mesh->faceVertexCounts.push_back(polygonSize);
    }

    fbxsdk::FbxArray<fbxsdk::FbxVector4> faceNormals;
    if (!fbxMesh->GetPolygonVertexNormals(faceNormals))
    {
        Log("Failed to read normals for mesh " + mesh->name);
    }

    for (size_t i = 0; i < faceNormals.GetCount(); i++)
    {
        mesh->normals.push_back(FbxVector4dToGfVector3f(faceNormals[i]));
    }

    fbxsdk::FbxStringList fbxUVSetNames;
    fbxMesh->GetUVSetNames(fbxUVSetNames);
    mesh->uvs.resize(fbxUVSetNames.GetCount());
    mesh->uvIndices.resize(fbxUVSetNames.GetCount());
    for (size_t i = 0; i < fbxUVSetNames.GetCount(); i++)
    {
        auto uvElement = fbxMesh->GetElementUV(i);
        if (uvElement && uvElement->GetIndexArray().GetCount() > 0 && mThreadContext->converterContext.IsOutputAssetUsdcOrUsdaOrUsdz())
        {
            auto& uvs = uvElement->GetDirectArray();

            for (size_t j = 0; j < uvs.GetCount(); j++)
            {
                mesh->uvs[i].push_back(FbxVector2dToGfVector2f(uvs.GetAt(j)));
            }

            auto& uvindices = uvElement->GetIndexArray();
            for (size_t index = 0; index < uvindices.GetCount(); index++)
            {
                mesh->uvIndices[i].push_back(uvindices.GetAt(index));
            }
        }
        else
        {
            const char* uvName = fbxUVSetNames.GetStringAt(i);
            for (size_t j = 0; j < fbxMesh->GetPolygonCount(); j++)
            {
                for (size_t k = 0; k < fbxMesh->GetPolygonSize(j); k++)
                {
                    fbxsdk::FbxVector2 uv(0.0f, 0.0f);
                    bool unmapped = false;
                    fbxMesh->GetPolygonVertexUV(j, k, uvName, uv, unmapped);
                    mesh->uvs[i].push_back(FbxVector2dToGfVector2f(uv));
                }
            }
        }
    }

    PopulateVertexColor(fbxMesh, mesh);
    PopulateSubsets(fbxMesh, stage, mesh);
    GetCacheAnimationFrames(stage, scene, fbxMesh, mesh->pointCacheTimesamples);

    return mesh;
}

void FbxSdkImporter::PopulateCamera(
    fbxsdk::FbxScene* scene,
    fbxsdk::FbxNode* currentNode,
    const StagePtr& stage,
    const StageNodePtr& stageNode,
    double scale
)
{
    fbxsdk::FbxCamera* fbxCamera = currentNode->GetCamera();
    if (!fbxCamera)
    {
        return;
    }

    auto camera = std::make_shared<Camera>();
    auto position = fbxCamera->EvaluatePosition(fbxsdk::FBXSDK_TIME_ZERO);
    auto targetPosition = fbxCamera->EvaluateLookAtPosition(fbxsdk::FBXSDK_TIME_ZERO);
    auto upVector = fbxCamera->EvaluateUpDirection(position, targetPosition, fbxsdk::FBXSDK_TIME_ZERO);
    camera->lookAt = FbxVector4dToGfVector3d(targetPosition) * scale;
    camera->position = FbxVector4dToGfVector3d(position) * scale;
    camera->up = FbxVector4dToGfVector3d(upVector);
    double stageUnitScale = 1 / (100.0 * stage->worldUnits);
    camera->focalLength = fbxCamera->FocalLength.Get() * stageUnitScale; // In tenth of stage unit
    camera->horizonalAperture = fbxCamera->FilmWidth.Get() * 25.4f * stageUnitScale; // In tenth of stage unit
    camera->verticallAperture = fbxCamera->FilmHeight.Get() * 25.4f * stageUnitScale; // In tenth of stage unit
    camera->clippingNear = fbxCamera->NearPlane.Get() * scale;
    camera->clippingFar = fbxCamera->FarPlane.Get() * scale;
    if (currentNode->LclTranslation.IsAnimated() || currentNode->LclRotation.IsAnimated() || currentNode->LclScaling.IsAnimated())
    {
        for (size_t i = 0; i < stage->animationTracks.size(); i++)
        {
            PXR_NS::VtVec3dArray positionSamples;
            PXR_NS::VtVec3dArray targetSamples;
            PXR_NS::VtVec3dArray upSamples;

            fbxsdk::FbxAnimStack* animStack = scene->GetSrcObject<fbxsdk::FbxAnimStack>(i);
            scene->SetCurrentAnimationStack(animStack);
            fbxsdk::FbxTimeSpan timeSpan = animStack->GetLocalTimeSpan();
            fbxsdk::FbxTime step = timeSpan.GetDuration() / stage->animationTracks[i].keyFrames;
            for (size_t j = 0; j < stage->animationTracks[i].keyFrames; j++)
            {
                const auto& time = timeSpan.GetStart() + step * j;
                position = fbxCamera->EvaluatePosition(time);
                targetPosition = fbxCamera->EvaluateLookAtPosition(time);
                upVector = fbxCamera->EvaluateUpDirection(position, targetPosition, time);
                positionSamples.push_back(FbxVector4dToGfVector3d(position) * scale);
                targetSamples.push_back(FbxVector4dToGfVector3d(targetPosition) * scale);
                upSamples.push_back(FbxVector4dToGfVector3d(upVector));

                PXR_NS::GfMatrix4d viewTransform;
                viewTransform.SetLookAt(FbxVector4dToGfVector3d(position), FbxVector4dToGfVector3d(targetPosition), FbxVector4dToGfVector3d(upVector));
            }

            camera->positionAnimations.push_back(positionSamples);
            camera->lookAtAnimations.push_back(targetSamples);
            camera->upAnimations.push_back(upSamples);
        }
    }

    const char* name = fbxCamera->GetName();
    if (name && name[0] != '\0')
    {
        camera->name = name;
    }
    else
    {
        camera->name = currentNode->GetName();
    }

    stage->cameras.push_back(camera);
    stageNode->cameras.push_back(stage->cameras.size() - 1);
}

void FbxSdkImporter::PopulateLight(fbxsdk::FbxScene* scene, fbxsdk::FbxNode* currentNode, const StagePtr& stage, const StageNodePtr& stageNode)
{
    auto fbxLight = currentNode->GetLight();
    if (!fbxLight)
    {
        return;
    }

    LightPtr light = std::make_shared<Light>();
    const auto& name = fbxLight->GetName();
    if (name && name[0] != '\0')
    {
        light->name = name;
    }
    else
    {
        light->name = currentNode->GetName();
    }
    light->color = FbxColorToGfVector3f(fbxLight->Color.Get());
    light->outAngle = fbxLight->OuterAngle.Get();
    light->innerAngle = fbxLight->InnerAngle.Get();
    light->intensity = fbxLight->Intensity.Get();

    auto lightType = fbxLight->LightType.Get();
    switch (lightType)
    {
        case fbxsdk::FbxLight::ePoint:
            light->type = LightType::POINT;
            break;
        case fbxsdk::FbxLight::eDirectional:
        case fbxsdk::FbxLight::eSpot:
            light->type = LightType::DISTANT;
            break;
        case fbxsdk::FbxLight::eArea:
            if (fbxLight->AreaLightShape.Get() == fbxsdk::FbxLight::eSphere)
            {
                light->type = LightType::SPHERE;
            }
            else
            {
                light->type = LightType::RECT;
            }
            break;
        case fbxsdk::FbxLight::eVolume:
        default:
            light->type = LightType::POINT;
            break;
    }

    stage->lights.push_back(light);
    stageNode->lights.push_back(stage->lights.size() - 1);
}

void FbxSdkImporter::PopulateSubsets(const fbxsdk::FbxMesh* fbxMesh, const StagePtr& stage, const MeshPtr& mesh)
{
    size_t materialCount = 0;
    fbxsdk::FbxNode* node = nullptr;

    if (fbxMesh && fbxMesh->GetNode())
    {
        node = fbxMesh->GetNode();
        materialCount = node->GetMaterialCount();
    }

    if (materialCount > 0)
    {
        std::vector<size_t> globalMaterialIndex;
        globalMaterialIndex.resize(materialCount, INVALID_MATERIAL_INDEX);
        for (size_t materialIndex = 0; materialIndex < materialCount; materialIndex++)
        {
            fbxsdk::FbxSurfaceMaterial* surfaceMaterial = node->GetMaterial(materialIndex);
            auto iter = mFbxMaterialToMaterialIndex.find(surfaceMaterial);
            if (iter != mFbxMaterialToMaterialIndex.end())
            {
                globalMaterialIndex[materialIndex] = iter->second;
            }
            else
            {
                auto material = LoadMaterial(stage, surfaceMaterial);
                if (material)
                {
                    size_t index = AddMaterial(stage, material);
                    mFbxMaterialToMaterialIndex.insert({ surfaceMaterial, index });
                    globalMaterialIndex[materialIndex] = index;
                }
            }
        }

        // Group faces with material index
        std::unordered_map<size_t, PXR_NS::VtArray<int>> materialFaceIndices;
        const auto& elementMaterial = fbxMesh->GetElementMaterial();
        if (elementMaterial)
        {
            const auto& mappingMode = elementMaterial->GetMappingMode();
            if (mappingMode == fbxsdk::FbxGeometryElementMaterial::eAllSame || mappingMode == fbxsdk::FbxGeometryElementMaterial::eByPolygon)
            {
                const auto materialIndices = &fbxMesh->GetElementMaterial()->GetIndexArray();
                if (materialIndices)
                {
                    size_t count = materialIndices->GetCount();
                    if (count == mesh->faceVertexCounts.size())
                    {
                        for (size_t i = 0; i < mesh->faceVertexCounts.size(); i++)
                        {
                            int materialIndex = materialIndices->GetAt(i);
                            if (materialIndex >= 0 && materialIndex < globalMaterialIndex.size())
                            {
                                size_t globalIndex = globalMaterialIndex[materialIndex];
                                materialFaceIndices[globalIndex].push_back(i);
                            }
                        }
                    }
                    else if (count == 1) // All the same
                    {
                        size_t materialIndex = materialIndices->GetAt(0);
                        if (materialIndex < globalMaterialIndex.size())
                        {
                            size_t globalIndex = globalMaterialIndex[materialIndex];
                            for (size_t i = 0; i < mesh->faceVertexCounts.size(); i++)
                            {
                                materialFaceIndices[globalIndex].push_back(i);
                            }
                        }
                    }
                    else
                    {
                        Log("ERROR: material indices count are not the same as face count: " + mesh->name);
                    }
                }
            }
        }

        if (materialFaceIndices.size() > 0)
        {
            for (const auto& materialFace : materialFaceIndices)
            {
                MeshGeomSubset subset;
                subset.faceIndices = materialFace.second;
                subset.materialIndex = materialFace.first;
                if (subset.materialIndex != INVALID_MATERIAL_INDEX)
                {
                    subset.name = stage->materials[subset.materialIndex]->name;
                }
                mesh->meshSubsets.push_back(subset);
            }
        }
        else
        {
            MeshGeomSubset subset;
            subset.faceIndices.resize(mesh->faceVertexCounts.size());
            subset.materialIndex = globalMaterialIndex[0];
            if (subset.materialIndex != INVALID_MATERIAL_INDEX)
            {
                subset.name = stage->materials[subset.materialIndex]->name;
            }
            std::iota(subset.faceIndices.begin(), subset.faceIndices.end(), 0);
            mesh->meshSubsets.push_back(subset);
        }
    }
    else
    {
        MeshGeomSubset subset;
        subset.faceIndices.resize(mesh->faceVertexCounts.size());
        std::iota(subset.faceIndices.begin(), subset.faceIndices.end(), 0);
        mesh->meshSubsets.push_back(subset);
    }
}

void FbxSdkImporter::PopulateVertexColor(const fbxsdk::FbxMesh* fbxMesh, const MeshPtr& mesh)
{
    size_t vertexColorCount = fbxMesh->GetElementVertexColorCount();
    if (vertexColorCount == 0)
    {
        return;
    }

    mesh->colors.resize(vertexColorCount);
    for (size_t i = 0; i < vertexColorCount; i++)
    {
        auto vertexColor = fbxMesh->GetElementVertexColor(i);
        auto mappingMode = vertexColor->GetMappingMode();
        auto referenceMode = vertexColor->GetReferenceMode();
        for (size_t j = 0; j < mesh->faceVertexIndices.size(); j++)
        {
            fbxsdk::FbxColor color;
            switch (mappingMode)
            {
                case fbxsdk::FbxLayerElement::eByControlPoint:
                    if (referenceMode == fbxsdk::FbxGeometryElement::eDirect)
                    {
                        color = vertexColor->GetDirectArray().GetAt(mesh->faceVertexIndices[j]);
                    }
                    else
                    {
                        int index = vertexColor->GetIndexArray().GetAt(mesh->faceVertexIndices[j]);
                        color = vertexColor->GetDirectArray().GetAt(index);
                    }
                    break;
                case fbxsdk::FbxLayerElement::eByPolygonVertex:
                    if (referenceMode == fbxsdk::FbxGeometryElement::eDirect)
                    {
                        color = vertexColor->GetDirectArray().GetAt(j);
                    }
                    else
                    {
                        int index = vertexColor->GetIndexArray().GetAt(j);
                        color = vertexColor->GetDirectArray().GetAt(index);
                    }
                    break;
                default:
                    color = fbxsdk::FbxColor(1.0, 1.0, 1.0, 1.0);
                    break;
            }
            mesh->colors[i].push_back(FbxColorToGfVector3f(color));
        }
    }
}

size_t FbxSdkImporter::AddMaterial(const StagePtr& stage, const MaterialPtr& material)
{
    // Checks if the material has the same params set to reuse it.
    for (size_t i = 0; i < stage->materials.size(); i++)
    {
        const auto& existingMaterial = stage->materials[i];
        if (existingMaterial == material)
        {
            return i;
        }
    }

    // If It has name conflicts.
    auto iter = mMaterialNameInstances.find(material->name);
    if (iter != mMaterialNameInstances.end())
    {
        material->name = material->name + std::to_string(iter->second);
        iter->second += 1;
    }
    else
    {
        mMaterialNameInstances.insert({ material->name, 0 });
    }

    stage->materials.push_back(material);

    return stage->materials.size() - 1;
}

MaterialPtr FbxSdkImporter::LoadMaterial(const StagePtr& stage, fbxsdk::FbxSurfaceMaterial* fbxMaterial)
{
    MaterialPtr material = std::make_shared<Material>();
    material->name = fbxMaterial->GetName();

    // Intercepts material loading process
    if (mThreadContext->converterContext.HasMaterialLoader() || mThreadContext->converterContext.PopulateMaterialsOnly())
    {
        OmniConverterMaterialDescription materialDescription;
        const auto& classId = fbxMaterial->GetClassId();
        materialDescription.name = material->name;
        materialDescription.classId = classId.GetName();

        for (auto p = fbxMaterial->GetFirstProperty(); p.IsValid(); p = fbxMaterial->GetNextProperty(p))
        {
            OmniConverterMaterialProperty materialProperty = ConvertFbxProperty(stage, p);
            materialDescription.inputProperties.push_back(materialProperty);
        }

        material->rawAssetProperties = materialDescription.inputProperties;
        if (!mThreadContext->converterContext.PopulateMaterialsOnly() && mThreadContext->converterContext.HasMaterialLoader())
        {
            material->fallback = !mThreadContext->MaterialLoader(&materialDescription);
        }

        material->materialType = materialDescription.classId;
        material->inputProperties = materialDescription.outputProperties;
        material->materialPath = materialDescription.materialPath;
        material->entryIdentifier = materialDescription.entryIdentifier;
        material->builtIn = materialDescription.builtin;
    }

    // if(!mThreadContext->converterContext.materialLoader || mThreadContext->converterContext.ExportPreviewSurface())
    {
        const auto& classId = fbxMaterial->GetClassId();
        if (classId.Is(fbxsdk::FbxSurfacePhong::ClassId))
        {
            auto fbxPhongMaterial = (fbxsdk::FbxSurfacePhong*)fbxMaterial;
            auto diffuseColor = fbxPhongMaterial->Diffuse;
            if (diffuseColor.IsValid())
            {
                material->diffuseColor = FbxVector4dToGfVector3f(fbxPhongMaterial->Diffuse.Get());
                material->hasDiffuseColor = true;
            }

            auto emissiveColor = fbxPhongMaterial->Emissive;
            if (emissiveColor.IsValid() && emissiveColor.Get() != fbxsdk::FbxVector4(1.0f, 1.0f, 1.0f))
            {
                auto emissiveFactor = fbxPhongMaterial->EmissiveFactor.Get();
                material->emissiveColor = emissiveFactor * FbxVector4dToGfVector3f(fbxPhongMaterial->Emissive.Get());
                material->hasEmissiveColor = true;
            }

            auto specularColor = fbxPhongMaterial->Specular;
            if (specularColor.IsValid())
            {
                material->specularColor = FbxVector4dToGfVector3f(fbxPhongMaterial->Specular.Get());
                material->hasSpecularColor = true;
            }

            auto transparentColor = fbxPhongMaterial->TransparentColor;
            if (transparentColor.IsValid())
            {
                auto color = transparentColor.Get();
                auto factor = fbxPhongMaterial->TransparencyFactor;
                if (factor.IsValid())
                {
                    color = fbxsdk::FbxDouble3(color[0] * factor.Get(), color[1] * factor.Get(), color[2] * factor.Get());
                }
                // as calculated by FBX SDK 2017:
                float opacity = 1.0f - ((color[0] + color[1] + color[2]) / 3.0f);
                material->opacity = opacity;
                material->hasOpacity = true;
            }
        }
        else if (classId.Is(fbxsdk::FbxSurfaceLambert::ClassId))
        {
            auto fbxPhongMaterial = (fbxsdk::FbxSurfaceLambert*)fbxMaterial;
            auto diffuseColor = fbxPhongMaterial->Diffuse;
            if (diffuseColor.IsValid())
            {
                material->diffuseColor = FbxVector4dToGfVector3f(fbxPhongMaterial->Diffuse.Get());
                material->hasDiffuseColor = true;
            }

            auto emissiveColor = fbxPhongMaterial->Emissive;
            if (emissiveColor.IsValid())
            {
                auto emissiveFactor = fbxPhongMaterial->EmissiveFactor.Get();
                material->emissiveColor = emissiveFactor * FbxVector4dToGfVector3f(fbxPhongMaterial->Emissive.Get());
                material->hasEmissiveColor = true;
            }

            auto transparentColor = fbxPhongMaterial->TransparentColor;
            if (transparentColor.IsValid())
            {
                auto color = transparentColor.Get();
                auto factor = fbxPhongMaterial->TransparencyFactor;
                if (factor.IsValid())
                {
                    color = fbxsdk::FbxDouble3(color[0] * factor.Get(), color[1] * factor.Get(), color[2] * factor.Get());
                }
                // as calculated by FBX SDK 2017:
                float opacity = 1.0f - ((color[0] + color[1] + color[2]) / 3.0f);
                material->opacity = opacity;
                material->hasOpacity = true;
            }
        }
        else
        {
            auto diffuseColorProperty = fbxMaterial->FindProperty(fbxsdk::FbxSurfaceMaterial::sDiffuse);
            if (diffuseColorProperty.IsValid())
            {
                auto diffuseColor = diffuseColorProperty.Get<fbxsdk::FbxColor>();
                material->diffuseColor = FbxColorToGfVector3f(diffuseColor);
                material->hasDiffuseColor = true;
            }

            auto emissiveColorProperty = fbxMaterial->FindProperty(fbxsdk::FbxSurfaceMaterial::sEmissive);
            if (emissiveColorProperty.IsValid())
            {
                auto emissiveColor = emissiveColorProperty.Get<fbxsdk::FbxColor>();
                material->emissiveColor = FbxColorToGfVector3f(emissiveColor);
                material->hasEmissiveColor = true;

                auto emissiveColorFactorProperty = fbxMaterial->FindProperty(fbxsdk::FbxSurfaceMaterial::sEmissiveFactor);
                if (emissiveColorFactorProperty.IsValid())
                {
                    auto emissiveColorFactor = emissiveColorProperty.Get<fbxsdk::FbxDouble>();
                    material->emissiveColor *= emissiveColorFactor;
                }
            }

            auto transparentColorProperty = fbxMaterial->FindProperty(fbxsdk::FbxSurfaceMaterial::sTransparentColor);
            if (transparentColorProperty.IsValid())
            {
                auto transparentColor = transparentColorProperty.Get<fbxsdk::FbxColor>();
                auto transparentColorFactorProperty = fbxMaterial->FindProperty(fbxsdk::FbxSurfaceMaterial::sTransparencyFactor);
                if (transparentColorFactorProperty.IsValid())
                {
                    auto factor = transparentColorFactorProperty.Get<FbxDouble>();
                    transparentColor = fbxsdk::FbxDouble3(transparentColor[0] * factor, transparentColor[1] * factor, transparentColor[2] * factor);
                }
                float opacity = 1.0f - ((transparentColor[0] + transparentColor[1] + transparentColor[2]) / 3.0f);
                material->opacity = opacity;
                material->hasOpacity = true;
            }

            auto specularColorProperty = fbxMaterial->FindProperty(fbxsdk::FbxSurfaceMaterial::sSpecular);
            if (specularColorProperty.IsValid())
            {
                auto specularColor = specularColorProperty.Get<fbxsdk::FbxColor>();
                material->specularColor = FbxColorToGfVector3f(specularColor);
                material->hasSpecularColor = true;
            }
        }


#define LOAD_TEXTURE_START()                                                                                                                         \
    if (0)                                                                                                                                           \
    {                                                                                                                                                \
    }
#define LOAD_TEXTURE(textureType, textureName, mode, space, uvScale, uvBias)                                                                         \
    else if (p.GetName() == textureName)                                                                                                             \
    {                                                                                                                                                \
        auto textureReference = LoadTexture(stage, p);                                                                                               \
        if (textureReference.IsValid())                                                                                                              \
        {                                                                                                                                            \
            textureReference.outputMode = mode;                                                                                                      \
            textureReference.colorSpace = space;                                                                                                     \
            textureReference.scale = uvScale;                                                                                                        \
            textureReference.bias = uvBias;                                                                                                          \
            material->SetTextureReference(textureType, textureReference);                                                                            \
        }                                                                                                                                            \
    }
#define LOAD_COLOR(field, flag, colorName)                                                                                                           \
    if (p.IsValid() && p.GetName() == colorName)                                                                                                     \
    {                                                                                                                                                \
        auto color = p.Get<fbxsdk::FbxColor>();                                                                                                      \
        field = FbxColorToGfVector3f(color);                                                                                                         \
        flag = true;                                                                                                                                 \
    }
#define LOAD_FLOAT(field, floatName)                                                                                                                 \
    if (p.IsValid() && p.GetName() == floatName)                                                                                                     \
    {                                                                                                                                                \
        auto value = p.Get<fbxsdk::FbxFloat>();                                                                                                      \
        field = value;                                                                                                                               \
    }

        for (auto p = fbxMaterial->GetFirstProperty(); p.IsValid(); p = fbxMaterial->GetNextProperty(p))
        {
            LOAD_TEXTURE_START()
            LOAD_TEXTURE(
                MaterialTextureType::DIFFUSE,
                fbxsdk::FbxSurfaceMaterial::sDiffuse,
                TextureOutputMode::RGB,
                TextureColorSpace::SRGB,
                PXR_NS::GfVec4f(1.0f),
                PXR_NS::GfVec4f(0.0f)
            )
            LOAD_TEXTURE(
                MaterialTextureType::EMISSIVE,
                fbxsdk::FbxSurfaceMaterial::sEmissive,
                TextureOutputMode::RGB,
                TextureColorSpace::SRGB,
                PXR_NS::GfVec4f(1.0f),
                PXR_NS::GfVec4f(0.0f)
            )
            LOAD_TEXTURE(
                MaterialTextureType::NORMAL,
                fbxsdk::FbxSurfaceMaterial::sNormalMap,
                TextureOutputMode::RGB,
                TextureColorSpace::RAW,
                PXR_NS::GfVec4f(1.0f),
                PXR_NS::GfVec4f(0.0f)
            )
            LOAD_TEXTURE(
                MaterialTextureType::OPACITY,
                fbxsdk::FbxSurfaceMaterial::sTransparentColor,
                TextureOutputMode::AVERAGE,
                TextureColorSpace::SRGB,
                PXR_NS::GfVec4f(1.0f),
                PXR_NS::GfVec4f(0.0f)
            )
            else if (p.GetParent().GetName() == "Maya")
            {
                // Load color
                LOAD_COLOR(material->emissiveColor, material->hasEmissiveColor, "emissionColor");
                float emissiveFactor = 1.0;
                LOAD_FLOAT(emissiveFactor, "emission");
                material->emissiveColor *= emissiveFactor;

                LOAD_COLOR(material->diffuseColor, material->hasDiffuseColor, "baseColor");

                // Maya PBR
                LOAD_TEXTURE_START()
                LOAD_TEXTURE(
                    MaterialTextureType::DIFFUSE,
                    "baseColor",
                    TextureOutputMode::RGB,
                    TextureColorSpace::AUTO,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::NORMAL,
                    "normalCamera",
                    TextureOutputMode::RGB,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::EMISSIVE,
                    "emissionColor",
                    TextureOutputMode::RGB,
                    TextureColorSpace::AUTO,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::METALLIC,
                    "metalness",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::ROUGHNESS,
                    "specularRoughness",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::OCCLUSION,
                    "diffuseRoughness",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::OPACITY,
                    "opacity",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )

                // Maya stingray
                LOAD_TEXTURE(
                    MaterialTextureType::DIFFUSE,
                    "TEX_color_map",
                    TextureOutputMode::RGB,
                    TextureColorSpace::SRGB,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::NORMAL,
                    "TEX_normal_map",
                    TextureOutputMode::RGB,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::EMISSIVE,
                    "TEX_emissive_map",
                    TextureOutputMode::RGB,
                    TextureColorSpace::SRGB,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::METALLIC,
                    "TEX_metallic_map",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::ROUGHNESS,
                    "TEX_roughness_map",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::OCCLUSION,
                    "TEX_ao_map",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::OPACITY,
                    "TEX_opacity_map",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
            }
            else
            {
                // Max
                // max_2021_LegacyStandard_mat
                float emissiveFactor = 1.0;
                LOAD_FLOAT(emissiveFactor, "emission");
                LOAD_COLOR(material->emissiveColor, material->hasEmissiveColor, "EmissiveColor");
                material->emissiveColor *= emissiveFactor;
                LOAD_COLOR(material->diffuseColor, material->hasDiffuseColor, "DiffuseColor");

                LOAD_TEXTURE_START()
                LOAD_TEXTURE(
                    MaterialTextureType::DIFFUSE,
                    "DiffuseColor",
                    TextureOutputMode::RGB,
                    TextureColorSpace::AUTO,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::NORMAL,
                    "Bump",
                    TextureOutputMode::RGB,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::OPACITY,
                    "TransparentColor",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::EMISSIVE,
                    "EmissiveColor",
                    TextureOutputMode::RGB,
                    TextureColorSpace::AUTO,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::METALLIC,
                    "SpecularFactor",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::ROUGHNESS,
                    "ShininessExponent",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::OCCLUSION,
                    "AmbientColor",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )

                // max_2021_Physical_mat
                LOAD_COLOR(material->emissiveColor, material->hasEmissiveColor, "EmissiveColor");
                LOAD_COLOR(material->diffuseColor, material->hasDiffuseColor, "DiffuseColor");

                LOAD_TEXTURE_START()
                LOAD_TEXTURE(
                    MaterialTextureType::DIFFUSE,
                    "base_color_map",
                    TextureOutputMode::RGB,
                    TextureColorSpace::AUTO,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::NORMAL,
                    "bump_map",
                    TextureOutputMode::RGB,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::OPACITY,
                    "transparency_map",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::EMISSIVE,
                    "emit_color_map",
                    TextureOutputMode::RGB,
                    TextureColorSpace::AUTO,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::METALLIC,
                    "metalness_map",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::OCCLUSION,
                    "diff_rough_map",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::ROUGHNESS,
                    "roughness_map",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )

                // max_2021_PBRWrapper_metalrough_mat
                LOAD_TEXTURE_START()
                LOAD_TEXTURE(
                    MaterialTextureType::DIFFUSE,
                    "base_color_map",
                    TextureOutputMode::RGB,
                    TextureColorSpace::AUTO,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::NORMAL,
                    "norm_map",
                    TextureOutputMode::RGB,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::OPACITY,
                    "opacity_map",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::EMISSIVE,
                    "emit_color_map",
                    TextureOutputMode::RGB,
                    TextureColorSpace::AUTO,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::METALLIC,
                    "metalness_map",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::OCCLUSION,
                    "ao_map",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::ROUGHNESS,
                    "roughness_map",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )

                // max_2021_ArnoldStandard_001_mat
                LOAD_TEXTURE_START()
                LOAD_TEXTURE(
                    MaterialTextureType::DIFFUSE,
                    "base_color.shader",
                    TextureOutputMode::RGB,
                    TextureColorSpace::AUTO,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::NORMAL,
                    "normal.shader",
                    TextureOutputMode::RGB,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::OPACITY,
                    "opacity.shader",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::EMISSIVE,
                    "emission_color.shader",
                    TextureOutputMode::RGB,
                    TextureColorSpace::AUTO,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::METALLIC,
                    "metalness.shader",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::OCCLUSION,
                    "diffuse_roughness.shader",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
                LOAD_TEXTURE(
                    MaterialTextureType::ROUGHNESS,
                    "specular_roughness.shader",
                    TextureOutputMode::AVERAGE,
                    TextureColorSpace::RAW,
                    PXR_NS::GfVec4f(1.0f),
                    PXR_NS::GfVec4f(0.0f)
                )
            }
        }

        auto& opacityTexture = material->GetTextureReference(MaterialTextureType::OPACITY);
        auto& diffuseTexture = material->GetTextureReference(MaterialTextureType::DIFFUSE);
        if (HasAlphaChannel(mThreadContext, stage, opacityTexture.imageIndex))
        {
            opacityTexture.outputMode = TextureOutputMode::ALPHA;
        }
    }

    return material;
}

TextureReference FbxSdkImporter::LoadTexture(const StagePtr& stage, fbxsdk::FbxProperty& textureProperty)
{
    size_t textureCount = textureProperty.GetSrcObjectCount<fbxsdk::FbxTexture>();
    for (int i = 0; i < textureCount; i++)
    {
        fbxsdk::FbxFileTexture* fbxFileTexture = textureProperty.GetSrcObject<fbxsdk::FbxFileTexture>(i);
        if (fbxFileTexture)
        {
            std::string filePath = fbxFileTexture->GetFileName();
            if (filePath.empty())
            {
                // It's possible that this texture links to another one.
                for (auto p = fbxFileTexture->GetFirstProperty(); p.IsValid(); p = fbxFileTexture->GetNextProperty(p))
                {
                    size_t propertyTextureCount = p.GetSrcObjectCount<fbxsdk::FbxTexture>();
                    for (int j = 0; j < propertyTextureCount; j++)
                    {
                        fbxsdk::FbxFileTexture* propertyFileTexture = p.GetSrcObject<fbxsdk::FbxFileTexture>(j);
                        if (propertyFileTexture)
                        {
                            filePath = propertyFileTexture->GetFileName();
                            if (!filePath.empty())
                            {
                                break;
                            }
                        }
                    }
                }

                if (filePath.empty())
                {
                    continue;
                }
            }

            std::string path = PathUtils::AbsPath(filePath);
            std::string filteredPath;
            if (!mThreadContext->converterContext.IsPathExisted(path))
            {
                const std::string& importBasePath = mThreadContext->converterContext.GetImportAssetDir();
                if (!PathUtils::IsAbsolutePath(filePath))
                {
                    path = PathUtils::JoinPaths(importBasePath, filePath);
                }
                else
                {
                    path = filePath;
                }

                if (!mThreadContext->converterContext.IsPathExisted(path))
                {
                    auto fileName = PathUtils::GetFileName(path, true);
                    // Try to find texture from the dir of import asset.
                    path = PathUtils::JoinPaths(importBasePath, fileName);
                    if (!mThreadContext->converterContext.FilterTexturePath(path, filteredPath))
                    {
                        // Try to find texture from the textures dir of import asset.
                        path = PathUtils::JoinPaths({ importBasePath, "textures", fileName });
                        mThreadContext->converterContext.FilterTexturePath(path, filteredPath);
                    }
                }
                else
                {
                    filteredPath = path;
                }
            }
            else
            {
                filteredPath = path;
            }

            if (!filteredPath.empty())
            {
                auto iter = mTextureIndex.find(filteredPath);
                size_t imageIndex = -1;
                if (iter == mTextureIndex.end())
                {
                    TextureImagePtr image = std::make_shared<TextureImage>();
                    image->originalPath = filePath;
                    image->realPath = filteredPath;
                    stage->images.push_back(image);
                    imageIndex = stage->images.size() - 1;
                    mTextureIndex.insert({ filteredPath, imageIndex });
                }
                else
                {
                    imageIndex = iter->second;
                }
                TextureReference textureReference;
                textureReference.imageIndex = imageIndex;
                textureReference.transform.translation[0] = fbxFileTexture->GetTranslationU();
                textureReference.transform.translation[1] = fbxFileTexture->GetTranslationV();
                textureReference.transform.scale[0] = fbxFileTexture->GetScaleU();
                textureReference.transform.scale[1] = fbxFileTexture->GetScaleV();
                textureReference.transform.rotation[0] = fbxFileTexture->GetRotationU();
                textureReference.transform.rotation[1] = fbxFileTexture->GetRotationV();
                textureReference.transform.rotation[2] = fbxFileTexture->GetRotationW();

                return textureReference;
            }
            else
            {
                std::string warning = "[WARNING] TextureImage (" + path + ") cannot be found but is referenced in original asset.";
                Log(warning);
            }
        }
    }

    return {};
}

TransformAnimationTracks FbxSdkImporter::GetNodeAnimation(
    const std::vector<AnimationTrack>& animTracks,
    fbxsdk::FbxScene* scene,
    fbxsdk::FbxNode* node,
    const Transform& nodeLocalTransform,
    double scale
)
{
    TransformAnimationTracks nodeAnimations;
    if (node->LclTranslation.IsAnimated() || node->LclRotation.IsAnimated() || node->LclScaling.IsAnimated())
    {
        for (size_t i = 0; i < animTracks.size(); i++)
        {
            fbxsdk::FbxAnimStack* animStack = scene->GetSrcObject<fbxsdk::FbxAnimStack>(i);
            scene->SetCurrentAnimationStack(animStack);
            const auto& nodeAnimation = GetNodeAnimationFrames(animTracks[i], node, animStack, nodeLocalTransform, scale);
            nodeAnimations.push_back(nodeAnimation);
        }
    }

    return nodeAnimations;
}

TransformTimesamples FbxSdkImporter::GetNodeAnimationFrames(
    const AnimationTrack& animationTrack,
    fbxsdk::FbxNode* node,
    fbxsdk::FbxAnimStack* animation,
    const Transform& nodeLocalTransform,
    double scale
)
{
    fbxsdk::FbxTimeSpan timeSpan = animation->GetLocalTimeSpan();
    fbxsdk::FbxTime step = timeSpan.GetDuration() / animationTrack.keyFrames;
    PXR_NS::VtVec3dArray translations;
    PXR_NS::VtVec3dArray scales;
    PXR_NS::VtVec3dArray rotationXYZs;
    if (node->LclTranslation.IsAnimated() || node->LclScaling.IsAnimated() || node->LclRotation.IsAnimated())
    {
        for (size_t i = 0; i < animationTrack.keyFrames; i++)
        {

            const auto& nodeTransform = GetNodeLocalTransform(node, scale, timeSpan.GetStart() + step * i);
            const auto& tes = nodeTransform.GetTES();
            translations.push_back(tes.t);
            scales.push_back(tes.s);

            if (!mThreadContext->converterContext.IgnoreFlipRotation() && rotationXYZs.size() > 1)
            {
                // there are two unusual situations that we need to filer the rotation value
                // 1. Horizontal:
                // pprevRotation :       (180, -88.96231, 180),
                // prevRotation :        (0, -89.86246, 0), <-----wrong value
                // currRotation(tes.r) : (180, -88.22269, 180),
                //
                // 2. Reverse :
                // prevRotation :        (180, -88.96231, 180),
                // currRotation(tes.r) : (-180, -89.86246, 0),< -----wrong value

                PXR_NS::GfVec3d prevRotation = rotationXYZs.back();
                PXR_NS::GfVec3d pprevRotation = rotationXYZs[rotationXYZs.size() - 2];
                // Check the Whirl at first
                if (IsHorizontalRotation(pprevRotation, prevRotation, tes.r))
                {
                    // we need to filter pre rotation value here
                    rotationXYZs[rotationXYZs.size() - 1] = pprevRotation;
                }
                else
                {
                    if (IsReverseRotation(prevRotation, tes.r))
                    {
                        rotationXYZs.push_back(prevRotation);
                    }
                    else
                    {
                        rotationXYZs.push_back(tes.r);
                    }
                }
            }
            else
            {
                rotationXYZs.push_back(tes.r);
            }
        }

        if (translations.size() > 0 && AllGfElementsAreEqual(translations))
        {
            PXR_NS::GfVec3d first = translations.front();
            translations.clear();
            if (first != nodeLocalTransform.GetTranslate())
            {
                translations.push_back(first);
            }
        }

        if (scales.size() > 0 && AllGfElementsAreEqual(scales))
        {
            PXR_NS::GfVec3d first = scales.front();
            scales.clear();
            if (first != nodeLocalTransform.GetScale())
            {
                scales.push_back(first);
            }
        }

        if (rotationXYZs.size() > 0 && AllGfElementsAreEqual(rotationXYZs))
        {
            PXR_NS::GfVec3d first = rotationXYZs.front();
            rotationXYZs.clear();
            if (first != nodeLocalTransform.GetRotationXYZ())
            {
                rotationXYZs.push_back(first);
            }
        }
    }

    return TransformTimesamples(translations, rotationXYZs, scales);
}

fbxsdk::FbxAMatrix FbxSdkImporter::GetNodeGeometryTransform(fbxsdk::FbxNode* node, double scale)
{
    // See
    // http://docs.autodesk.com/FBX/2014/ENU/FBX-SDK-Documentation/index.html?url=files/GUID-C35D98CB-5148-4B46-82D1-51077D8970EE.htm,topicNumber=d30e8813
    // and
    // http://docs.autodesk.com/FBX/2014/ENU/FBX-SDK-Documentation/index.html?url=files/GUID-C35D98CB-5148-4B46-82D1-51077D8970EE.htm,topicNumber=d30e8813
    // for geometry transform's details. It needs to be included in the global matrix calculation of
    // current node, but not passing down to its children.
    fbxsdk::FbxAMatrix matrixGeo;
    matrixGeo.SetIdentity();

    if (node->GetNodeAttribute())
    {
        const FbxVector4 translation = node->GetGeometricTranslation(fbxsdk::FbxNode::eSourcePivot);
        const FbxVector4 rotation = node->GetGeometricRotation(fbxsdk::FbxNode::eSourcePivot);
        const FbxVector4 scaleV = node->GetGeometricScaling(fbxsdk::FbxNode::eSourcePivot);

        matrixGeo.SetS(scaleV);
        matrixGeo.SetR(rotation);
        matrixGeo.SetTOnly(translation * scale);
    }

    return matrixGeo;
}

template <typename T>
fbxsdk::FbxAMatrix FbxSdkImporter::ChangeRotationOrderToXYZ(T& rotation, FbxEuler::EOrder order)
{
    fbxsdk::FbxAMatrix rotationXYZ;
    FbxVector4 rotationX, rotationY, rotationZ;
    rotationX.Set(rotation[0], 0.0, 0.0);
    rotationY.Set(0.0, rotation[1], 0.0);
    rotationZ.Set(0.0, 0.0, rotation[2]);

    fbxsdk::FbxAMatrix rotationX_Matrix, rotationY_Matrix, rotationZ_Matrix;
    rotationX_Matrix.SetR(rotationX);
    rotationY_Matrix.SetR(rotationY);
    rotationZ_Matrix.SetR(rotationZ);
    switch (order)
    {
        case FbxEuler::eOrderXYZ:
            rotationXYZ = rotationZ_Matrix * rotationY_Matrix * rotationX_Matrix;
            break;

        case FbxEuler::eOrderXZY:
            rotationXYZ = rotationY_Matrix * rotationZ_Matrix * rotationX_Matrix;
            break;

        case FbxEuler::eOrderYXZ:
            rotationXYZ = rotationZ_Matrix * rotationX_Matrix * rotationY_Matrix;
            break;

        case FbxEuler::eOrderYZX:
            rotationXYZ = rotationX_Matrix * rotationZ_Matrix * rotationY_Matrix;
            break;

        case FbxEuler::eOrderZXY:
            rotationXYZ = rotationY_Matrix * rotationX_Matrix * rotationZ_Matrix;
            break;

        case FbxEuler::eOrderZYX:
            rotationXYZ = rotationX_Matrix * rotationY_Matrix * rotationZ_Matrix;
            break;
        default:
            rotationXYZ = rotationZ_Matrix * rotationY_Matrix * rotationX_Matrix;
            break;
    }
    return rotationXYZ;
}

Transform FbxSdkImporter::GetNodeLocalTransform(fbxsdk::FbxNode* node, double scale, fbxsdk::FbxTime time)
{
    fbxsdk::FbxDouble3 zero(0.0, 0.0, 0.0);
    fbxsdk::FbxDouble3 one(1.0, 1.0, 1.0);

    PXR_NS::GfVec3d pivot(0.0);
    if (node->RotationPivot.IsValid())
    {
        pivot = FbxVector4dToGfVector3d(node->RotationPivot.Get()) * scale;
    }
    else if (node->ScalingPivot.IsValid())
    {
        pivot = FbxVector4dToGfVector3d(node->ScalingPivot.Get()) * scale;
    }

    // Using transform instead.
    if (!AreTransformOpsSupported(node) ||
        (!mThreadContext->converterContext.PivotSupportedForOutputFormat() && !PXR_NS::GfIsClose(pivot, ZERO_VEC_3D, 1e-6)))
    {
        return Transform(GetNodeLocalMatrix(node, scale, time));
    }
    else
    {
        PXR_NS::GfVec3d translation = FbxVector4dToGfVector3d(node->EvaluateLocalTranslation(time)) * scale;
        if (node->RotationOffset.IsValid())
        {
            translation += FbxVector4dToGfVector3d(node->RotationOffset.Get()) * scale;
        }

        FbxEuler::EOrder rotationOrder;
        node->GetRotationOrder(FbxNode::eSourcePivot, rotationOrder);

        fbxsdk::FbxAMatrix preRotation;
        if (node->PreRotation.IsValid())
        {
            preRotation.SetR(node->PreRotation.Get());
        }

        fbxsdk::FbxAMatrix postRotation;
        if (node->PostRotation.IsValid())
        {
            postRotation.SetR(node->PostRotation.Get());
        }


        fbxsdk::FbxAMatrix rotation;
        if (rotationOrder != FbxEuler::eOrderXYZ)
        {
            rotation = ChangeRotationOrderToXYZ(node->EvaluateLocalRotation(time), rotationOrder);
        }
        else
        {
            rotation = node->EvaluateLocalTransform(time);
        }
        rotation = preRotation * rotation * postRotation.Inverse();
        PXR_NS::GfVec3d scaling = FbxVector4dToGfVector3d(node->EvaluateLocalScaling(time));

        Transform transform(translation, FbxVector4dToGfVector3d(rotation.GetR()), scaling);
        transform.SetPivot(pivot);

        return transform;
    }
}

PXR_NS::GfMatrix4d FbxSdkImporter::GetNodeLocalMatrix(fbxsdk::FbxNode* node, double scale, fbxsdk::FbxTime time)
{
    fbxsdk::FbxAMatrix localTransform;
    auto parentNode = node->GetParent();
    auto nodeLocalTransform = node->EvaluateLocalTransform(time);
    nodeLocalTransform.SetTOnly(nodeLocalTransform.GetT() * scale);

    return FbxAMatrixToGfMatrix(nodeLocalTransform);
}

PXR_NS::GfMatrix4d FbxSdkImporter::GetNodeGlobalMatrix(fbxsdk::FbxNode* node, double scale, fbxsdk::FbxTime time)
{
    const auto& geometricTransform = GetNodeGeometryTransform(node, scale);
    auto globalTransform = node->EvaluateGlobalTransform(time);
    globalTransform.SetTOnly(globalTransform.GetT() * scale);
    auto matrix = FbxAMatrixToGfMatrix(globalTransform * geometricTransform);

    return matrix;
}


OmniConverterMaterialProperty FbxSdkImporter::ConvertFbxProperty(const StagePtr& stage, fbxsdk::FbxProperty& fbxProperty)
{
    OmniConverterMaterialProperty property;
    property.name = fbxProperty.GetHierarchicalName();
    auto textureReference = LoadTexture(stage, fbxProperty);
    if (textureReference.imageIndex != -1)
    {
        TextureImagePtr image = stage->images[textureReference.imageIndex];
        property.isTextureProperty = true;
        property.valueType = OMNI_CONVERTER_VALUE_TYPE_STRING;
        property.stringValue = image->realPath;
        property.textureTranslation[0] = textureReference.transform.translation[0];
        property.textureTranslation[1] = textureReference.transform.translation[1];
        property.textureScale[0] = textureReference.transform.scale[0];
        property.textureScale[1] = textureReference.transform.scale[1];
    }
    else
    {
        auto valueType = fbxProperty.GetPropertyDataType().GetType();
        switch (valueType)
        {
            case fbxsdk::eFbxShort:
            case fbxsdk::eFbxUShort:
            case fbxsdk::eFbxUInt:
            case fbxsdk::eFbxLongLong:
            case fbxsdk::eFbxULongLong:
            case fbxsdk::eFbxInt:
                property.value.intValue = fbxProperty.Get<int>();
                property.valueType = OMNI_CONVERTER_VALUE_TYPE_INT32;
                break;
            case fbxsdk::eFbxBool:
                property.value.boolValue = fbxProperty.Get<bool>();
                property.valueType = OMNI_CONVERTER_VALUE_TYPE_BOOL;
                break;
            case fbxsdk::eFbxHalfFloat:
            case fbxsdk::eFbxFloat:
                property.singlePrecision = true;
            case fbxsdk::eFbxDouble:
                property.value.doubleValue = fbxProperty.Get<float>();
                property.valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE;
                break;
            case fbxsdk::eFbxDouble2:
            {
                const auto& double2Value = fbxProperty.Get<fbxsdk::FbxDouble2>();
                property.value.double2Value[0] = double2Value[0];
                property.value.double2Value[1] = double2Value[1];
                property.valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE2;
                break;
            }
            case fbxsdk::eFbxDouble3:
            {
                const auto& double3Value = fbxProperty.Get<fbxsdk::FbxDouble3>();
                property.value.double3Value[0] = double3Value[0];
                property.value.double3Value[1] = double3Value[1];
                property.value.double3Value[2] = double3Value[2];
                property.valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE3;
                break;
            }
            case fbxsdk::eFbxDouble4:
            {
                const auto& double4Value = fbxProperty.Get<fbxsdk::FbxDouble4>();
                property.value.double4Value[0] = double4Value[0];
                property.value.double4Value[1] = double4Value[1];
                property.value.double4Value[2] = double4Value[2];
                property.value.double4Value[3] = double4Value[3];
                property.valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE4;
                break;
            }
            case fbxsdk::eFbxDouble4x4:
            {
                const auto& double16Value = fbxProperty.Get<fbxsdk::FbxDouble4x4>();
                for (size_t i = 0; i < 4; i++)
                {
                    for (size_t j = 0; j < 4; j++)
                    {
                        property.value.double16Value[i * 4 + j] = double16Value[i][j];
                    }
                }
                property.valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE16;
                break;
            }
            case fbxsdk::eFbxString:
                property.stringValue = fbxProperty.Get<fbxsdk::FbxString>();
                property.valueType = OMNI_CONVERTER_VALUE_TYPE_STRING;
                break;
            default:
                property.valueType = OMNI_CONVERTER_VALUE_TYPE_UNDEFINED;
                break;
        }
    }

    return property;
}

bool FbxSdkImporter::GetCacheAnimationFrames(
    const StagePtr& stage,
    fbxsdk::FbxScene* scene,
    fbxsdk::FbxMesh* mesh,
    PointCacheTimesamples& pointCacheAnimation
)
{
    size_t vertexCacheCount = mesh->GetDeformerCount(fbxsdk::FbxDeformer::eVertexCache);
    if (vertexCacheCount == 0)
    {
        return false;
    }

    fbxsdk::FbxVertexCacheDeformer* deformer = static_cast<fbxsdk::FbxVertexCacheDeformer*>(mesh->GetDeformer(0, fbxsdk::FbxDeformer::eVertexCache));
    fbxsdk::FbxCache* cache = deformer->GetCache();
    unsigned int vertexCount = (unsigned int)mesh->GetControlPointsCount();
    fbxsdk::FbxStatus fbxStatus;
    fbxsdk::FbxString rpath, apath;
    cache->GetCacheFileName(rpath, apath);

    std::string absolutePath(rpath.Buffer());
    if (absolutePath.empty())
    {
        return false;
    }
    else if (!PathUtils::IsAbsolutePath(absolutePath))
    {
        const auto& importBasePath = mThreadContext->converterContext.GetImportAssetDir();
        absolutePath = PathUtils::JoinPaths(importBasePath, std::string(rpath.Buffer()));
        cache->SetCacheFileName(rpath.Buffer(), absolutePath.c_str());
    }

    auto channel = deformer->Channel.Get();
    if (!cache->OpenFileForRead(&fbxStatus))
    {
        return false;
    }

    int channelIndex = cache->GetChannelIndex(deformer->Channel.Get(), &fbxStatus);
    if (channelIndex == -1 || deformer->Type.Get() != fbxsdk::FbxVertexCacheDeformer::ePositions)
    {
        cache->CloseFile();
        return false;
    }

    fbxsdk::FbxTime start;
    fbxsdk::FbxTime end;
    if (!cache->GetAnimationRange(channelIndex, start, end, &fbxStatus))
    {
        cache->CloseFile();
        return false;
    }

    fbxsdk::FbxTimeSpan timeSpan(start, end);
    unsigned int length = 0;
    cache->Read(nullptr, length, timeSpan.GetStart(), channelIndex);
    if (length != vertexCount * 3)
    {
        cache->CloseFile();
        // the content of the cache is by vertex not by control points (we don't support it here)
        return false;
    }

    size_t numFrames = cache->GetSampleCount();
    const auto& timePerFrame = cache->GetCacheTimePerFrame();
    if (numFrames == 0)
    {
        numFrames = (timeSpan.GetDuration() / timePerFrame).Get();
    }

    if (numFrames > stage->maxKeyFrames)
    {
        stage->maxKeyFrames = numFrames;
        stage->mutiplier = GetFramerate(scene) / 24.0;
    }

    if (numFrames > 0)
    {
        fbxsdk::FbxTime step = timeSpan.GetDuration() / numFrames;
        for (size_t i = 0; i < numFrames; i++)
        {
            float* readBuf = NULL;
            unsigned int bufferSize = 0;
            const auto& time = timeSpan.GetStart() + step * i;
            bool readSucceed = cache->Read(&readBuf, bufferSize, time);
            if (readSucceed)
            {
                unsigned int readBufIndex = 0;
                PXR_NS::VtArray<PXR_NS::GfVec3f> pointTransforms;
                while (readBufIndex < 3 * vertexCount)
                {
                    PXR_NS::GfVec3f vertex;
                    vertex[0] = readBuf[readBufIndex];
                    readBufIndex++;
                    vertex[1] = readBuf[readBufIndex];
                    readBufIndex++;
                    vertex[2] = readBuf[readBufIndex];
                    readBufIndex++;
                    pointTransforms.push_back(vertex);
                }
                pointCacheAnimation.push_back(pointTransforms);
            }
            else
            {
                pointCacheAnimation.clear();
                cache->CloseFile();
                return false;
            }
        }
    }

    cache->CloseFile();
    return true;
}

double FbxSdkImporter::GetFramerate(fbxsdk::FbxScene* scene)
{
    auto timeMode = scene->GetGlobalSettings().GetTimeMode();
    if (timeMode != fbxsdk::FbxTime::eCustom)
    {
        return fbxsdk::FbxTime::GetFrameRate(timeMode);
    }
    else
    {
        return scene->GetGlobalSettings().GetCustomFrameRate();
    }
}

bool FbxSdkImporter::AreTransformOpsSupported(fbxsdk::FbxNode* node)
{
    fbxsdk::FbxDouble3 zero(0.0f, 0.0f, 0.0f);
    fbxsdk::FbxDouble3 one(1.0f, 1.0f, 1.0f);
    if (HasGeometricTransforms(node) || (node->ScalingOffset.IsValid() && node->ScalingOffset.Get() != zero))
    {
        return true;
    }

    return false;
}

bool FbxSdkImporter::HasGeometricTransforms(fbxsdk::FbxNode* node)
{
    fbxsdk::FbxDouble3 zero(0.0f, 0.0f, 0.0f);
    fbxsdk::FbxDouble3 one(1.0f, 1.0f, 1.0f);
    if ((node->GeometricTranslation.IsValid() && node->GeometricTranslation.Get() != zero) ||
        (node->GeometricRotation.IsValid() && node->GeometricRotation.Get() != zero) ||
        (node->GeometricScaling.IsValid() && node->GeometricScaling.Get() != one))
    {
        return true;
    }

    return false;
}

void FbxSdkImporter::BakingScales(const StagePtr& stage)
{
    if (mThreadContext->converterContext.BakingScales())
    {
        size_t index = 0;
        for (const auto& meshInfo : mMeshInfos)
        {
            auto mesh = meshInfo.mesh.lock();
            if (meshInfo.attachedNodes.size() > 0 && mesh && mesh->pointCacheTimesamples.empty())
            {
                bool sameScales = true;
                auto referenceScale = meshInfo.attachedNodes[0].lock()->localTransform.GetScale();
                // It only bakes scales if all scales of its references are the same, no animations, and not bone node.
                for (size_t i = 0; i < meshInfo.attachedNodes.size(); i++)
                {
                    auto node = meshInfo.attachedNodes[i].lock();
                    auto scale = node->localTransform.GetScale();
                    if (scale != referenceScale || !node->transformAnimationTracks.empty() || node->isBoneNode || !node->children.empty())
                    {
                        sameScales = false;
                    }
                }

                if (!sameScales)
                {
                    continue;
                }

                for (auto& point : mesh->points)
                {
                    point[0] = point[0] * referenceScale[0];
                    point[1] = point[1] * referenceScale[1];
                    point[2] = point[2] * referenceScale[2];
                }

                auto scaleInverse = referenceScale;
                scaleInverse[0] = scaleInverse[0] == 0.0 ? scaleInverse[0] : 1.0 / scaleInverse[0];
                scaleInverse[1] = scaleInverse[1] == 0.0 ? scaleInverse[1] : 1.0 / scaleInverse[1];
                scaleInverse[2] = scaleInverse[2] == 0.0 ? scaleInverse[2] : 1.0 / scaleInverse[2];
                auto scaleMatrix = PXR_NS::GfMatrix4d(1.0);
                scaleMatrix.SetScale(scaleInverse);
                if (stage->isExportFromBlender)
                {
                    for (auto skinmesh : stage->skinMeshes)
                    {
                        if (skinmesh->meshIndex == index)
                        {
                            // should also bake scale into skin mesh's geomBindTransform
                            // TODO: should we also do this for fbx which not export from blender?
                            skinmesh->geomBindTransform = scaleMatrix * skinmesh->geomBindTransform;
                            break;
                        }
                    }
                }

                // Baking all scales into meshes.
                for (size_t i = 0; i < meshInfo.attachedNodes.size(); i++)
                {
                    auto node = meshInfo.attachedNodes[i].lock();
                    // Clear node scale of node transform
                    node->localTransform.SetScale(PXR_NS::GfVec3d(1.0));
                    node->worldTransformMatrix = scaleMatrix * node->worldTransformMatrix;
                }
            }
            ++index;
        }
    }
}
