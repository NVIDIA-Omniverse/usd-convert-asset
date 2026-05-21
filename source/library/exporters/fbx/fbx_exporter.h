// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../../converter_future.h"
#include "../../stage.h"
#include "../../usd_convert_asset_internal.h"
#include "../exporter.h"

#include <fbxsdk.h>

class FbxSdkExporter : public Exporter
{
public:

    FbxSdkExporter(const OmniFutureThreadContextPtr& context) : Exporter(context)
    {
    }

    virtual ~FbxSdkExporter(){};

    virtual OmniConverterStatus Export(const StagePtr& stage, std::string& detailedError);

private:

    OmniConverterStatus ExportTextures(const StagePtr& stage);
    OmniConverterStatus UploadTextureIfNotEmpty(const TextureImagePtr& texture);
    OmniConverterStatus UploadFileInternal(const std::string& filePath, const std::string& targetDir);

    fbxsdk::FbxSurfaceMaterial* ToFbxMaterial(const StagePtr& stage, fbxsdk::FbxScene* fbxScene, const MaterialPtr& material);
    fbxsdk::FbxCamera* ToFbxCamera(fbxsdk::FbxScene* fbxScene, const CameraPtr& camera);
    fbxsdk::FbxMesh* ToFbxMesh(fbxsdk::FbxScene* fbxScene, const MeshPtr& mesh);
    bool ToCacheAnimationFrames(fbxsdk::FbxScene* fbxScene, fbxsdk::FbxMesh* fbxMesh, const PointCacheTimesamples& pointCacheAnimation);
    std::shared_ptr<fbxsdk::FbxScene> ToFbxScene(fbxsdk::FbxManager* fbxManager, const StagePtr& stage);
    void PopulateStageNodeTree(const StagePtr& stage, const StageNodePtr& currentNode, fbxsdk::FbxScene* fbxScene, fbxsdk::FbxNode* parentFbxNode);
    void PreprocessAllNodes(const StagePtr& stage, const StageNodePtr& node);
    void PreprocessStage(const StagePtr& stage);
    void SetDefaultTransform(fbxsdk::FbxNode* node, const Transform& transform);
    void SetDefaultMatrix(fbxsdk::FbxNode* node, const PXR_NS::GfMatrix4d& transform);
    void SetTimeSampledTranslation(fbxsdk::FbxNode* node, fbxsdk::FbxAnimLayer* animLayer, fbxsdk::FbxTime time, const PXR_NS::GfVec3d& sample);
    void SetTimeSampledScale(fbxsdk::FbxNode* node, fbxsdk::FbxAnimLayer* animLayer, fbxsdk::FbxTime time, const PXR_NS::GfVec3d& sample);
    void SetTimeSampledRotationXYZ(fbxsdk::FbxNode* node, fbxsdk::FbxAnimLayer* animLayer, fbxsdk::FbxTime time, const PXR_NS::GfVec3d& sample);
    void SetNodeTRSSamples(
        const StagePtr& stage,
        fbxsdk::FbxNode* node,
        fbxsdk::FbxAnimLayer* animLayer,
        const TransformTimesamples& transformTimesamples
    );
    fbxsdk::FbxNode* CreateSkeleton(const StagePtr& stage, const StageNodePtr& boneNode, fbxsdk::FbxScene* fbxScene);
    fbxsdk::FbxNode* CreateMesh(const MeshPtr& mesh, const std::string& name, fbxsdk::FbxScene* fbxScene);
    fbxsdk::FbxNode* CreateCamera(const CameraPtr& camera, const std::string& name, fbxsdk::FbxScene* fbxScene);
    fbxsdk::FbxNode* CreateLight(const LightPtr& light, const std::string& name, fbxsdk::FbxScene* fbxScene);
    fbxsdk::FbxNode* CreateSkinnedMeshes(const StagePtr& stage, fbxsdk::FbxScene* fbxScene);
    fbxsdk::FbxTexture* ToFbxTexture(const StagePtr& stage, fbxsdk::FbxScene* fbxScene, const TextureReference& textureReferene);
    fbxsdk::FbxFileTexture*
    ToFbxFileTexture(fbxsdk::FbxScene* fbxScene, const std::string& texturePath, double tx, double ty, double sx, double sy, bool monoAverage);

    std::string mMaterialsExportRoot;
    std::string mTexturesExportRoot;
    std::unordered_map<std::string, std::string> mUploadedFiles;
    std::unordered_map<TextureImagePtr, std::string> mTextureUploadPath;

    std::unordered_map<StageNodePtr, bool> mStageNodeInfos;
    std::unordered_map<StageNodePtr, fbxsdk::FbxNode*> mBoneNodes;

    // Assimp does not support subset, so if a mesh has more than one subset,
    // it will be splitted into several parts. This map is used to record
    // all the meshes that's splitted from original mesh.
    std::unordered_map<MeshPtr, std::vector<size_t>> mMeshIndices;
    std::unordered_map<MeshPtr, fbxsdk::FbxMesh*> mMeshes;
    std::vector<fbxsdk::FbxSurfaceMaterial*> mAllMaterials;

    // Different animation will be exported to different stack.
    // For each stack, it has only one anim layer.
    std::vector<fbxsdk::FbxAnimStack*> mAllAnimStacks;
    std::vector<fbxsdk::FbxAnimLayer*> mAllAnimLayers;
};
