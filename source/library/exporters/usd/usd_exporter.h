// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../../converter_future.h"
#include "../../stage.h"
#include "../../usd_convert_asset_internal.h"
#include "../../utils/name_utils.h"
#include "../exporter.h"

class UsdExporter : public Exporter
{
public:

    UsdExporter(const OmniFutureThreadContextPtr& context) : Exporter(context)
    {
    }

    virtual ~UsdExporter(){};

    virtual OmniConverterStatus Export(const StagePtr& stage, std::string& detailedError);

private:

    OmniConverterStatus WriteLayerTo(PXR_NS::SdfLayerRefPtr layer, const std::string& path, std::string& detailedError);
    PXR_NS::SdfLayerRefPtr GetOrCreateLayer(const std::string& realStagePath, bool yAxis = true, double unit = 0.01);
    OmniConverterStatus UploadTextureIfNotEmpty(const TextureImagePtr& texture, std::string& detailedError);
    OmniConverterStatus UploadFileInternal(const std::string& filePath, const std::string& targetDir, std::string& detailedError);

    void PreprocessStage(const StagePtr& stage);
    PXR_NS::SdfPrimSpecHandle BindOrDefineMesh(
        const StagePtr& stage,
        const MeshPtr& mesh,
        PXR_NS::SdfLayerRefPtr usdLayer,
        const PXR_NS::SdfPath& parentPath,
        bool instanceable = false,
        const std::string& instanceDisplayName = std::string()
    );
    OmniConverterStatus ExportTextures(const StagePtr& stage, std::string& detailedError);
    OmniConverterStatus ExportInstancedMeshes(const StagePtr& stage, std::string& detailedError);
    OmniConverterStatus ExportStageTree(const StagePtr& stage, std::string& detailedError);
    OmniConverterStatus ExportAnimationClip(const StagePtr& stage, std::string& detailedError);
    OmniConverterStatus TraverseAndExport(
        const StagePtr& stage,
        const StageNodePtr& stageNode,
        PXR_NS::SdfLayerRefPtr usdLayer,
        const PXR_NS::SdfPath& parentPrimPath,
        NameUtils::NameCache& uniqueNameCount
    );
    PXR_NS::SdfPrimSpecHandle ExportSkeletonAndSkinning(
        const StagePtr& stage,
        const StageNodePtr& rootBone,
        PXR_NS::SdfLayerRefPtr usdLayer,
        const PXR_NS::SdfPath& skeletonRootPath
    );

    PXR_NS::SdfPrimSpecHandle ExportMeshInternal(
        PXR_NS::SdfLayerRefPtr usdLayer,
        const StagePtr& stage,
        const MeshPtr& mesh,
        const PXR_NS::SdfPath& meshPath,
        const std::string& meshDisplayName = std::string()
    );

    PXR_NS::SdfPrimSpecHandle ExportPointCloud(
        PXR_NS::SdfLayerRefPtr usdLayer,
        const StagePtr& stage,
        const MeshPtr& pointCloud,
        const PXR_NS::SdfPath& pointCloudPath,
        const std::string& pointCloudDisplayName
    );
    OmniConverterStatus ExportAnimations(const StagePtr& stage, std::string& detailedError);
    void ExportPreviewSurfaceNode(
        const StagePtr& stage,
        PXR_NS::SdfLayerHandle usdLayer,
        PXR_NS::SdfPrimSpecHandle& usdShader,
        const PXR_NS::SdfPrimSpecHandle& materialPrim,
        const MaterialPtr& material
    );
    bool ExportPreviewSurfaceTextureNode(
        const StagePtr& stage,
        PXR_NS::SdfLayerHandle usdLayer,
        const PXR_NS::SdfPrimSpecHandle& materialPrim,
        PXR_NS::SdfPrimSpecHandle& usdShader,
        const MaterialPtr& material,
        MaterialTextureType type,
        const std::string& name
    );

    PXR_NS::SdfPrimSpecHandle CreateMaterialPrim(
        const StagePtr& stage,
        PXR_NS::SdfLayerHandle usdLayer,
        PXR_NS::SdfPath parentPath,
        const MaterialPtr& material
    );
    void AddExternalReference(
        const PXR_NS::SdfPrimSpecHandle primSpec,
        const std::string& referencePath,
        const PXR_NS::SdfPath& primPath = PXR_NS::SdfPath::EmptyPath()
    );
    void PreprocessAllNodes(const StagePtr& stage);

    using NameInfo = NameUtils::NameInfo;
    NameInfo GetNameInfo(const std::string& baseName, const std::string& prefix, NameUtils::NameCache& nameMap);

    void SetXformTransformSamples(
        PXR_NS::SdfPrimSpecHandle xformPrimSpec,
        const TransformTimesamples& transformTimesamples,
        bool useDoublePrecisionOps,
        bool useTESOps,
        const StagePtr& stage
    );

    void BindMaterialToPrimFromSubset(
        const StagePtr& stage,
        const MeshGeomSubset& subset,
        PXR_NS::SdfPrimSpecHandle primSpec,
        PXR_NS::SdfPath materialGroupPath
    );


    std::string mMainUSDExportPath;
    std::string mPropsExportRoot;
    std::string mMaterialsExportRoot;
    std::string mTexturesExportRoot;
    std::string mAnimationsExportRoot;
    std::string mPropsFilePath;

    struct MeshPrimInfo
    {
        std::string layerPath; // The layer path of this mesh defined in.
        PXR_NS::SdfPath meshPrimPath; // UsdGeomMesh path inside layerPath.
        PXR_NS::SdfPath prototypePrimPath; // If the mesh is instanced and it's single USD export, prototypePrimPath
                                           // will point to the prototype mesh.
        std::vector<PXR_NS::SdfPath> subsets; // It's not empty if it has more than one subsets.
        size_t meshInstanceCount = 0; // Instances count in scene graph.
    };

    struct StageNodeInfo
    {
        bool hasProps = false; // If this node or its children includes props.
        bool hasSkeleton = false; // If this node or its children includes skeleton.
        PXR_NS::SdfPath nodeSkelRootPath; // If this node is root bone, this will record its prim path.
        PXR_NS::SdfPath nodePrimPath; // Node path in stage tree.
        std::vector<PXR_NS::SdfPath> meshInstancePaths; // The prim path of mesh instances under this node.
        std::vector<PXR_NS::SdfPath> cameraPaths; // The prim path of cameras under this node.
        std::string jointName; // If this is a bone node, it will store the hierarchical joint name.
    };

    bool mHasMaterialsToExport = false;
    std::vector<bool> mShouldExportMaterial;
    std::unordered_map<MeshPtr, MeshPrimInfo> mMeshPrimInfos;
    std::unordered_map<StageNodePtr, StageNodeInfo> mStageNodeInfos;

    std::unordered_map<TextureImagePtr, std::string> mTextureUploadPath;
    // Hold in-memory layers until exit to avoid it's been released before reference
    // Key will be identifier of in-memory layer.
    std::unordered_map<std::string, PXR_NS::SdfLayerRefPtr> mLayerHolders;
    std::unordered_map<std::string, std::string> mUploadedFiles;

    NameUtils::NameCache mUniqueMaterialName;
    std::unordered_map<MaterialPtr, NameInfo> mMaterialNameInfos;


    PXR_NS::SdfPath mCommonRootPath;
    PXR_NS::SdfPath mCommonMaterialGroupPath;

    bool mEnableFractionalOpacity = false;
};
