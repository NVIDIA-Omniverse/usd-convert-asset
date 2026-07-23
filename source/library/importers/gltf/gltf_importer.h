// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../../common/tiny_gltf_include.h"
#include "../importer.h"

class GltfImporter : public Importer
{
public:

    virtual std::string ComputeHash(const OmniFutureThreadContextPtr& context);
    virtual StagePtr ImportStage(const OmniFutureThreadContextPtr& context, OmniConverterStatus& status, std::string& detailedError) final;

private:

    void Log(const std::string& message);
    void PopulateAllMeshes(StagePtr& stage, tinygltf::Model& model, double scale = 1.0);
    void PopulateAllMaterials(StagePtr& stage, tinygltf::Model& model);
    void PopulateAllCameras(StagePtr& stage, tinygltf::Model& model, double scale = 1.0);
    void PopulateAllLights(StagePtr& stage, tinygltf::Model& model, double scale = 1.0);
    void PopulateSceneGraph(StagePtr& stage, tinygltf::Model& model, double scale = 1.0);
    void PopulateSceneNode(StagePtr& stage, StageNodePtr& stageNode, tinygltf::Model& model, size_t nodeIndex, bool underSkeleton, double scale = 1.0);
    void PopulateAllSkeletonRoots(StagePtr& stage, tinygltf::Model& model, double scale = 1.0);
    void PopulateStageAnimationInfomation(StagePtr& stage, tinygltf::Model& model, double scale = 1.0);
    TransformAnimationTracks PopulateNodeAnimation(const StagePtr& stage, tinygltf::Model& model, size_t nodeIndex, double scale = 1.0);
    void PopulateNodeParents(tinygltf::Model& model, size_t currentNodeIndex, std::unordered_map<size_t, size_t>& nodeParents);
    size_t FindCommonRootBone(tinygltf::Model& model, const std::unordered_map<size_t, size_t>& nodeParents, const std::set<size_t>& allJointNodes);
    int GetDefaultScene(tinygltf::Model& model);
    bool IsSkeletonRoot(size_t nodeIndex);
    bool HasSkinnedMeshInSkeleton(const StagePtr& stage, size_t skinIndex, size_t meshIndex);
    void FillInfluencedBones(const StagePtr& stage, tinygltf::Model& model);

private:

    OmniFutureThreadContextPtr mThreadContext;
    size_t mGlobalNodeIndex = 0;
    size_t mGlobalBoneNodeIndex = 0;
    size_t mGlobalMeshIndex = 0;
    size_t mGlobalCameraIndex = 0;
    size_t mGlobalLightIndex = 0;
    std::unordered_map<size_t, PXR_NS::GfMatrix4d> mJointBindMatrices;
    std::vector<size_t> mAllSkeletonRoots;
    std::vector<StageNodePtr> mAllStageNodes; // Indexed by node indices from gltf model.

    struct MeshVertexInfluences
    {
        PXR_NS::VtIntArray jointInfluences;
        PXR_NS::VtFloatArray jointWeights;
    };
    std::unordered_map<size_t, MeshVertexInfluences> mMeshIndexToVetexInfluences;

    // Skin index of the skinned mesh, which is the same size of Stage::skinMeshes.
    std::vector<size_t> mMeshSkinIndex;

    struct NodeAnimationSampler
    {
        int position = -1; // Sampler index
        int rotation = -1;
        int scale = -1;
    };
    // This is to record all node's animation samplers of each animation track
    // for quick information query of node animation.
    std::vector<std::unordered_map<size_t, NodeAnimationSampler>> mNodeAnimationSamplerTracks;

    std::unordered_map<size_t, OmniConverterBlobPtr> mEmbeddedImageDatas;
    std::unordered_map<std::string, OmniConverterBlobPtr> mExternalImageDatas;

    tinygltf::Model mGltfModel;
    bool mModelLoaded = false;
    bool mModelLoadedSuccessfully = false;
    OmniConverterStatus mModelLoadStatus = OmniConverterStatus::FILE_READ_ERROR;
    std::string mModelLoadError;
    std::set<int> mVisitedNodes;
};
