// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../importer.h"
#include "assimp/scene.h"


class AssimpImporter : public Importer
{
public:

    virtual std::string ComputeHash(const OmniFutureThreadContextPtr& context);
    virtual StagePtr ImportStage(const OmniFutureThreadContextPtr& context, OmniConverterStatus& status, std::string& detailedError) final;

private:

    void Log(const std::string& message);
    std::vector<std::string> PopulateAllTextures(const StagePtr& stage, const aiScene* assimpScene);
    void PopulateAllCameras(const aiScene* scene, const StagePtr& stage);
    void PopulateAllMaterials(const aiScene* assimpScene, const StagePtr& stage);
    void PopulateAllMeshes(const aiScene* assimpScene, const StagePtr& stage, double scale);
    void PopulateStageTree(
        const aiScene* scene,
        const aiNode* currentNode,
        const aiMatrix4x4& parentWorldTransform,
        const StagePtr& stage,
        const StageNodePtr& parentStageNode,
        double scale
    );
    void PopulateAllSkeletons(
        const aiScene* scene,
        const aiNode* currentNode,
        const std::shared_ptr<Stage>& stage,
        const std::shared_ptr<StageNode>& parentStageNode,
        double scale
    );
    void PopulateSkeleton(
        const aiScene* scene,
        const aiNode* currentNode,
        const std::shared_ptr<Stage>& stage,
        const std::shared_ptr<StageNode>& stageNode,
        const std::shared_ptr<StageNode>& parentBoneNode,
        double scale
    );

    void MergeNamedMeshes(const StagePtr& stage);
    void ReadAnimationInformation(const aiScene* assimpScene, const StagePtr& stage);

    const aiNodeAnim* FindNodeAnim(const aiAnimation* animation, const std::string& nodeName);
    int FindPosition(double animationTime, const aiNodeAnim* nodeAnim);
    int FindRotation(double animationTime, const aiNodeAnim* nodeAnim);
    int FindScaling(double animationTime, const aiNodeAnim* nodeAnim);

    TransformAnimationTracks GetNodeAnimation(
        const std::vector<AnimationTrack>& animTracks,
        const aiScene* assimpScene,
        const aiNode* node,
        double scale
    );

    TransformTimesamples GetNodeAnimationFrames(const aiNode* node, size_t fps, size_t keyFrames, aiAnimation* animation, double scale);

    bool InterpolatedPosition(const aiNode* node, const aiNodeAnim* nodeAnim, double animationTime, double frameTime, aiVector3D& translation);
    bool InterpolatedRotation(const aiNode* node, const aiNodeAnim* nodeAnim, double animationTime, double frameTime, aiQuaternion& orient);
    bool InterpolatedScaling(const aiNode* node, const aiNodeAnim* nodeAnim, double animationTime, double frameTime, aiVector3D& scale);
    aiQuaternion Nlerp(aiQuaternion qa, aiQuaternion qb, float blend);

    size_t FindTexture(const StagePtr& stage, const std::string& path);
    std::vector<std::string> GetMaterialTexturePaths(aiMaterial* material, const aiTextureType& type);

    bool isBoneNode(const aiNode* node);


private:

    OmniFutureThreadContextPtr mThreadContext;
    std::unordered_map<std::string, aiCamera*> mNameAiCameraMapping;

    // It's used to store the valid index of assimp mesh. Normally, all assimp meshes
    // are valid, then the index is the same as the one it's in the AssimpScene::mMeshes.
    // However, it's possible that assimp mesh is invalid. So al mesh indices need to be remapped
    // since Stage::meshes only store valid meshes. If mesh is invalid, -1 will be saved.
    std::vector<size_t> mAssimpMeshIndices;

    // Map used to record count of duplicated names.
    std::unordered_map<std::string, size_t> mMaterialNameIndex;
    std::unordered_map<std::string, size_t> mMaterialIndex;
    std::vector<size_t> mMaterialMappedIndex;

    aiNode* mRootBone = nullptr;
};
