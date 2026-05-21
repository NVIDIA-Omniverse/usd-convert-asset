// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../importer.h"

#include <fbxsdk.h>

class FbxSdkImporter : public Importer
{
public:

    FbxSdkImporter();
    virtual std::string ComputeHash(const OmniFutureThreadContextPtr& context);
    virtual StagePtr ImportStage(const OmniFutureThreadContextPtr& context, OmniConverterStatus& status, std::string& detailedError) final;

private:

    void Log(const std::string& message);
    void PopulateStageInfo(fbxsdk::FbxImporter* importer, fbxsdk::FbxScene* scene, const StagePtr& stage, double& unitsScale);
    void PopulateAllMaterials(fbxsdk::FbxScene* scene, fbxsdk::FbxNode* currentNode, const StagePtr& stage);
    void PopulateStageTree(
        fbxsdk::FbxScene* scene,
        fbxsdk::FbxNode* currentNode,
        const StagePtr& stage,
        const StageNodePtr& parentStageNode,
        fbxsdk::FbxNode** currentSkeletonRoot,
        bool hasGeometricOpsInParent,
        double scale
    );
    void PopulateAllMeshes(fbxsdk::FbxScene* scene, const StagePtr& stage, double scale, fbxsdk::FbxGeometryConverter& converter);
    MeshPtr PopulateMesh(fbxsdk::FbxScene* scene, fbxsdk::FbxMesh* fbxMesh, const StagePtr& stage, double scale, bool mergeMeshChildren);
    MeshPtr PopulateMeshInternal(fbxsdk::FbxScene* scene, fbxsdk::FbxMesh* fbxMesh, const StagePtr& stage, double scale);
    void PopulateCamera(fbxsdk::FbxScene* scene, fbxsdk::FbxNode* currentNode, const StagePtr& stage, const StageNodePtr& stageNode, double scale);
    void PopulateLight(fbxsdk::FbxScene* scene, fbxsdk::FbxNode* currentNode, const StagePtr& stage, const StageNodePtr& stageNode);
    void PopulateSubsets(const fbxsdk::FbxMesh* fbxMesh, const StagePtr& stage, const MeshPtr& mesh);
    void PopulateVertexColor(const fbxsdk::FbxMesh* fbxMesh, const MeshPtr& mesh);
    size_t AddMaterial(const StagePtr& stage, const MaterialPtr& material);
    MaterialPtr LoadMaterial(const StagePtr& stage, fbxsdk::FbxSurfaceMaterial* fbxMaterial);
    TextureReference LoadTexture(const StagePtr& stage, fbxsdk::FbxProperty& textureProperty);
    TransformAnimationTracks GetNodeAnimation(
        const std::vector<AnimationTrack>& animTracks,
        fbxsdk::FbxScene* scene,
        fbxsdk::FbxNode* node,
        const Transform& nodeLocalTransform,
        double scale
    );
    TransformTimesamples GetNodeAnimationFrames(
        const AnimationTrack& animationTrack,
        fbxsdk::FbxNode* node,
        fbxsdk::FbxAnimStack* animation,
        const Transform& nodeLocalTransform,
        double scale
    );
    fbxsdk::FbxAMatrix GetNodeGeometryTransform(fbxsdk::FbxNode* node, double scale);
    OmniConverterMaterialProperty ConvertFbxProperty(const StagePtr& stage, fbxsdk::FbxProperty& fbxProperty);
    Transform GetNodeLocalTransform(fbxsdk::FbxNode* node, double scale, fbxsdk::FbxTime time = fbxsdk::FBXSDK_TIME_INFINITE);
    template <typename T>
    fbxsdk::FbxAMatrix ChangeRotationOrderToXYZ(T& rotation, FbxEuler::EOrder order);
    PXR_NS::GfMatrix4d GetNodeLocalMatrix(fbxsdk::FbxNode* node, double scale, fbxsdk::FbxTime time = fbxsdk::FBXSDK_TIME_INFINITE);
    PXR_NS::GfMatrix4d GetNodeGlobalMatrix(fbxsdk::FbxNode* node, double scale, fbxsdk::FbxTime time = fbxsdk::FBXSDK_TIME_INFINITE);
    bool GetCacheAnimationFrames(const StagePtr& stage, fbxsdk::FbxScene* scene, fbxsdk::FbxMesh* mesh, PointCacheTimesamples& pointCacheAnimation);
    double GetFramerate(fbxsdk::FbxScene* scene);
    bool AreTransformOpsSupported(fbxsdk::FbxNode* node);
    bool HasGeometricTransforms(fbxsdk::FbxNode* node);
    void BakingScales(const StagePtr& stage);

private:

    struct Constraint
    {
        fbxsdk::FbxObject* targetNode;
        fbxsdk::FbxVector4 translationOffset;
        fbxsdk::FbxVector4 rotationOffset;
    };

    struct MeshInfo
    {
        PXR_NS::GfMatrix4d geomBindTransform = PXR_NS::GfMatrix4d(1.0);
        size_t instanceCount = 0; // It records the instances count of corresponding mesh.
        bool mergeChildren = false; // If it needs to merge children meshes, this is specific for fbxes exported from
                                    // Substance Painter.

        // The following properties are used only for baking scales into meshes if instanceCount == 1 and mergeChildren
        // == false and also no animations for the attached node.
        std::vector<std::weak_ptr<StageNode>> attachedNodes; // The last attached node.
        std::weak_ptr<Mesh> mesh; // The mesh prototype.
    };

    OmniFutureThreadContextPtr mThreadContext;
    std::unordered_map<fbxsdk::FbxMesh*, size_t> mFbxMeshToMeshIndex;
    std::unordered_map<fbxsdk::FbxSurfaceMaterial*, size_t> mFbxMaterialToMaterialIndex;
    std::vector<StageNodePtr> mBoneList;
    std::unordered_map<fbxsdk::FbxObject*, size_t> mNodeBoneIndex;
    std::unordered_map<fbxsdk::FbxObject*, fbxsdk::FbxObject*> mBoneNodeRoot;
    std::unordered_map<const fbxsdk::FbxObject*, Constraint> mNodeConstraints;
    std::unordered_map<std::string, size_t> mMaterialNameInstances;
    std::unordered_map<std::string, size_t> mTextureIndex;


    std::vector<MeshInfo> mMeshInfos;
};
