// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../../pxr_includes.h"
#include "../importer.h"

class UsdSdkImporter : public Importer
{
public:

    virtual StagePtr ImportStage(const OmniFutureThreadContextPtr& context, OmniConverterStatus& status, std::string& detailedError) final;

private:

    void TraverseUsdStage(const PXR_NS::UsdPrim& currentPrim, StagePtr& stage, StageNodePtr parentNode);
    MeshPtr PopulateMesh(const StagePtr& stage, const PXR_NS::UsdGeomMesh& usdMesh);
    CurvePtr PopulateCurve(const StagePtr& stage, const PXR_NS::UsdGeomBasisCurves& usdCurve);
    CameraPtr PopulateCamera(const PXR_NS::UsdGeomCamera& usdCamera);
    MaterialPtr PopulateMaterial(const StagePtr& stage, const PXR_NS::UsdShadeMaterial& usdMaterial);
    MaterialPtr PopulateOmniPbrMaterial(const StagePtr& stage, const PXR_NS::UsdShadeShader& usdShader);
    MaterialPtr PopulateOmniGlassMaterial(const StagePtr& stage, const PXR_NS::UsdShadeShader& usdShader);
    MaterialPtr PopulateUsdPreviewSurfaceMaterial(const StagePtr& stage, const PXR_NS::UsdShadeShader& usdShader);
    MaterialPtr PopulateGltfMaterial(const StagePtr& stage, const PXR_NS::UsdShadeShader& usdShader);
#if PXR_MINOR_VERSION > 21 || (PXR_MINOR_VERSION == 21 && PXR_PATCH_VERSION >= 11)
    LightPtr PopulateLight(const PXR_NS::UsdLuxLightAPI& usdLight);
#else
    LightPtr PopulateLight(const PXR_NS::UsdLuxLight& usdLight);
#endif
    size_t PopulateBoundMaterial(const StagePtr& stage, const PXR_NS::UsdPrim& prim);
    std::string SwitchToAnimationTrack(const PXR_NS::UsdPrim& prim, const std::string& animationTrackName);

    TransformAnimationTracks GetTransformAnimation(const PXR_NS::UsdPrim& prim, const StagePtr& stage, bool hasPivot);
    StageNodePtr ImportSkeleton(const PXR_NS::UsdPrim& prim, const StagePtr& stage);
    OmniConverterMaterialProperty ConvertMaterialInput(const PXR_NS::UsdShadeInput& input);
    bool GetGltfTextureInputValue(
        const StagePtr& stage,
        const PXR_NS::UsdShadeShader& shader,
        const std::string& inputName,
        TextureReference& textureReference
    );
    bool GetPreviewSurfaceTextureInputValue(
        const StagePtr& stage,
        const PXR_NS::UsdShadeShader& shader,
        const std::string& inputName,
        TextureReference& textureReference
    );
    size_t GetOrCreateTexture(const StagePtr& stage, const PXR_NS::SdfAssetPath& assetPath);

    struct PrototypeMeshInfo
    {
        size_t index;
        std::string name;
    };

    OmniFutureThreadContextPtr mThreadContext;
    std::unordered_map<PXR_NS::SdfPath, size_t, PXR_NS::SdfPath::Hash> mDefinedMaterials;
    std::unordered_map<PXR_NS::SdfPath, size_t, PXR_NS::SdfPath::Hash> mDefinedMeshes;
    std::unordered_map<std::string, size_t> mTextureIndex;
    std::vector<PXR_NS::SdfPath> mCameraPrimPaths;
    std::vector<AnimationTrack> mAllAnimationTracks;

};
