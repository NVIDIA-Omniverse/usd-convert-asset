// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../../converter_future.h"
#include "../../stage.h"
#include "../../thirdparty/tiny_gltf.h"
#include "../../usd_convert_asset_internal.h"
#include "../exporter.h"

struct LinearSweptSphere;

class GltfExporter : public Exporter
{
public:

    GltfExporter(const OmniFutureThreadContextPtr& context) : Exporter(context)
    {
    }

    virtual ~GltfExporter(){};

    virtual OmniConverterStatus Export(const StagePtr& stage, std::string& detailedError);

private:

    OmniConverterStatus ExportTextures(tinygltf::Model& model, const StagePtr& stage, std::string& detailedError);
    std::string UploadTexture(const TextureImagePtr& texture, std::string& detailedError);
    OmniConverterStatus UploadFileInternal(const std::string& filePath, const std::string& targetDir, std::string& detailedError);
    OmniConverterBlobPtr ReadTextureData(const StagePtr& stage, size_t imageIndex);
    void PreprocessAllNodes(const StagePtr& stage);
    tinygltf::Mesh ToTinygltfMesh(tinygltf::Model& model, const StagePtr& stage, const MeshPtr& mesh, double scale);
    tinygltf::Mesh ToTinygltfMesh(
        tinygltf::Model& model,
        const StagePtr& stage,
        const CurvePtr& curve,
        double scale,
        const std::vector<std::vector<LinearSweptSphere>>& lssStrands
    );
    tinygltf::Camera ToTinygltfCamera(tinygltf::Model& model, const CameraPtr& camera, double scale);
    tinygltf::Light ToTinygltfLight(const LightPtr& light, double scale);
    tinygltf::Material ToTinygltfMaterial(tinygltf::Model& model, const MaterialPtr& material);

    void CreateTransformAnimationChannels(
        tinygltf::Model& model,
        tinygltf::Animation& animationTrack,
        size_t nodeIndex,
        const TransformTimesamples& transformTimesamples,
        double frameStep,
        double scale = 1.0
    );
    void CreateNodeAnimation(
        tinygltf::Model& model,
        size_t nodeIndex,
        const StagePtr& stage,
        const TransformAnimationTracks& transformAnimationTracks,
        double scale = 1.0
    );
    void CreatePointCacheAnimationChannel(tinygltf::Model& model, size_t nodeIndex, size_t meshIndex, const StagePtr& stage);

    bool FillClearCoatExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue);
    bool FillSpecularRoughnessExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue);
    bool FillSheenExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue);
    bool FillTransmissionExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue);
    bool FillSpecularExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue);
    bool FillIorExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue);
    bool FillVolumeExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue);
    bool FillEmissiveStrengthExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue);
    bool FillIridescenceExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue);
    bool FillAnisotropyExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue);
    bool FillTextureTransformExtensionValue(tinygltf::Model& model, const TextureReference& textureReference, tinygltf::Value& extensionValue);

    void PopulatePropsAndMaterials(
        tinygltf::Model& model,
        const StagePtr& stage,
        double scale,
        const std::vector<std::vector<LinearSweptSphere>>& lssStrands
    );
    void PopulateStageNodeTree(
        tinygltf::Model& model,
        const StagePtr& stage,
        const StageNodePtr& currentNode,
        size_t parentNodeIndex,
        size_t currentSkin,
        PXR_NS::VtMatrix4fArray& skinInverseBindMatrices,
        double scale,
        std::vector<std::vector<LinearSweptSphere>>& lssStrands
    );
    tinygltf::TextureInfo ToTinyGltfTextureInfo(tinygltf::Model& model, const TextureReference& textureReference);

    std::string mMdlModulesExportRoot;
    std::string mTexturesExportRoot;

    // mUploadedFiles will be used to check if corresponding texture
    // path has already been handled.
    std::unordered_map<std::string, std::string> mUploadedFiles;

    struct StageNodeInfo
    {
        bool hasProps = false; // If this node or its children includes props.
        bool hasSkeleton = false; // If this node or its children includes bones.
    };
    std::unordered_map<StageNodePtr, StageNodeInfo> mStageNodeInfos;
    std::unordered_set<std::string> mExtensionsUsed;

    std::unordered_map<StageNodePtr, size_t> mAllJointIndices;
};
