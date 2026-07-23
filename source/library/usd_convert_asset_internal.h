// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "pxr_includes.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <usd_convert_asset.h>
#include <vector>

using OmniConverterBlobPtr = std::shared_ptr<OmniConverterBlob>;
inline void gBlobDefaultDataDeleter(void* data)
{
    delete[] (uint8_t*)data;
};
OmniConverterBlobPtr createOmniConverterBlob(uint8_t* data, size_t size, OmniConverterBlobDataDeleter deleter = gBlobDefaultDataDeleter);

enum class AssetType
{
    OBJ,
    FBX,
    GLTF,
    GLB,
    USDC,
    USDA,
    USDZ,
    BVH,
    STL,
    PLY,
    LXO,
    MD5,
    OTHER
};

struct OmniConverterMaterialProperty
{
    std::string name;
    OmniConverterMDLPropertyMetadata::_Value value;
    std::string stringValue;
    OmniConverterMaterialPropertyValueType valueType = OMNI_CONVERTER_VALUE_TYPE_UNDEFINED;
    bool isTextureProperty = false;
    double textureTranslation[2];
    double textureScale[2];

    // Output for MDL display
    std::string displayName;
    std::string groupName;
    OmniConverterMDLPropertyType detailedType = OMNI_CONVERTER_MDL_PROPERTY_TYPE_UNDEFINED;
    bool hasDefaultValue = false;
    OmniConverterMDLPropertyMetadata::_Value defaultValue;

    bool hasMinValue = false;
    OmniConverterMDLPropertyMetadata::_Value minValue;

    bool hasMaxValue = false;
    OmniConverterMDLPropertyMetadata::_Value maxValue;

    std::string colorSpace;
    bool singlePrecision = false;
};

struct TextureReference;
struct OmniConverterMaterialDescription
{
    // Input
    std::string name;
    std::string classId;
    std::vector<OmniConverterMaterialProperty> inputProperties;

    // Output
    std::vector<OmniConverterMaterialProperty> outputProperties;
    bool builtin = false; // If builtin is true, it will not resolve material path.
                          // For example, if it uses materials from Core MDL Library, this needs
                          // to be set to true.
                          // If it's false, it will copy material to export directory, and
                          // remap the path that references this material.
    std::string materialPath;
    std::string entryIdentifier;
};

struct OmniConverterAssetHandle
{
    std::vector<OmniConverterMaterialDescription> materials;
};

struct OmniConverterCallbacks
{
    OmniConverterReader readCallback = nullptr;
    OmniConverterBinaryWriter binaryWriteCallback = nullptr;
    OmniConverterUsdWriter layerWriteCallback = nullptr;
    OmniConverterFileCopy fileCopyCallback = nullptr;
    OmniConverterMakeDirs makeDirsCallback = nullptr;
    OmniConverterPathExists pathExistsCallback = nullptr;
    OmniConverterLogCallback logCallback = nullptr;
    OmniConverterProgressReporter progressCallback = nullptr;
    OmniConverterMaterialLoader materialLoader = nullptr;

};

static const std::string CACHED_USD_MAIN_FILE_NAME = "main.usd";
static const std::string ANIMATION_TRACK_VARIANT_SET_NAME = "animation_track";

class OmniConverterContext
{
public:

    OmniConverterContext()
    {
    }

    OmniConverterContext(
        const std::string& importAssetPath,
        const std::string& outputAssetpath,
        OmniConverterCallbacks callbacks,
        const std::string& cacheFolder,
        int32_t flags = 0,
        bool populateMaterialsOnly = false
    );
    bool IsInMemoryImport() const;
    bool IsInMemoryOutput() const;
    std::string GetImportAssetPath() const;
    std::string GetImportAssetDir() const;
    std::string GetImportAssetFileName() const;
    std::string GetOutputAssetPath() const;
    std::string GetOutputAssetDir() const;
    std::string GetOutputAssetFileName() const;
    bool HasMaterialLoader() const;
    bool PopulateMaterialsOnly() const;

    // By default, it will search path specified by texturePath.
    // If it's not existed, it will search other files that has the same name, but with different extensions.
    bool FilterTexturePath(const std::string& texturePath, std::string& filteredPath);
    bool LoadMaterial(OmniConverterFuture* future, OmniConverterMaterialDescription* material) const;
    bool IsPathExisted(const std::string& path) const;
    bool WriteUsdLayer(const std::string& outputPath, const std::string& layerIdentifier) const;
    bool CopyFile(const std::string& targetPath, const std::string& sourcePath) const;
    bool MakeDirectories(const std::string& directory) const;
    OmniConverterBlobPtr ReadFile(const std::string& path) const;
    bool WriteBinary(const std::string& path, OmniConverterBlob* blob) const;
    void ReportProgress(OmniConverterFuture* future, uint32_t progress, uint32_t total) const;
    void Log(const std::string& message) const;


    bool IsCachingEnabled() const
    {
        return !mCacheFolder.empty() && IsInMemoryOutput();
    }

    std::string GetCacheFolder() const
    {
        return mCacheFolder;
    }

    bool IgnoreAnimations() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_IGNORE_ANIMATION;
    }

    bool IgnoreMaterials() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS;
    }

    bool SingleMesh() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_SINGLE_MESH_FILE;
    }

    bool SmoothNormals() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_GEN_SMOOTH_NORMALS;
    }

    bool IgnoreCameras() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_IGNORE_CAMERAS;
    }

    bool IgnoreLights() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_IGNORE_LIGHTS;
    }

    bool ExportPreviewSurface() const
    {
        return true;
    }

    bool ExportPointerInstancer() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_SUPPORT_POINTER_INSTANCER;
    }

    // True: imported file is expected to be a shapenet object
    bool ExportAsShapenet() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_EXPORT_AS_SHAPENET;
    }


    bool UseMeterPerUnit() const;

    bool KeepAssetUnits() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_KEEP_WORLD_UNITS;
    }

    bool CreateWorldAsDefaultRootPrim() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_CREATE_WORLD_AS_DEFAULT_PRIM;
    }

    bool EmbeddingTextures() const
    {
        // Always embed textures into glb
        if (GetOutputAssetType() == AssetType::GLB)
        {
            return true;
        }

        return mFlags & OMNI_CONVERTER_FLAGS_EMBED_TEXTURES;
    }


    bool ConvertFbxToYUp() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_FBX_CONVERT_TO_Y_UP;
    }

    bool ConvertFbxToZUp() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_FBX_CONVERT_TO_Z_UP;
    }

    bool KeepAllMaterials() const;

    bool MergeAllMeshes() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_MERGE_ALL_MESHES;
    }

    bool UseDoublePrecisionForUSDTransformOp() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_USE_DOUBLE_PRECISION_FOR_USD_TRANSFORM_OP;
    }

    bool DisableInstancing() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_DISABLE_INSTANCING;
    }

    bool ExportHiddenProps() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_EXPORT_HIDDEN_PROPS;
    }

    bool BakingScales() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_FBX_BAKING_SCALES_INTO_MESH;
    }

    bool PivotSupportedForOutputFormat() const
    {
        if (mFlags & OMNI_CONVERTER_FLAGS_IGNORE_PIVOTS)
        {
            return false;
        }

        return IsOutputAssetUsdcOrUsdaOrUsdz() || GetOutputAssetType() == AssetType::FBX;
    }

    bool IgnoreFlipRotation() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_IGNORE_FLIP_ROTATION;
    }

    bool IgnoreUnboundBones() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_FBX_IGNORE_UNBOUND_BONES;
    }

    bool ExportEmbeddedGltf() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_EXPORT_EMBEDDED_GLTF;
    }

    bool ConvertUpY() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_STAGE_UP_Y;
    }

    bool ConvertUpZ() const
    {
        return mFlags & OMNI_CONVERTER_FLAGS_STAGE_UP_Z;
    }

    bool IsImportAssetGltfOrGlb() const;
    bool IsImportAssetUsdcOrUsdaOrUsdz() const;
    bool IsSupportedImportAsset() const;
    static std::string GetSupportedImportFormatsForError();

    bool IsOutputAssetGltfOrGlb() const;
    bool IsOutputAssetUsdcOrUsdaOrUsdz() const;

    AssetType GetImportAssetType() const;
    AssetType GetOutputAssetType() const;

    PXR_NS::UsdStageRefPtr GetCachedStage() const
    {
        return mCachedStage;
    }

    void clear()
    {
        mCachedStage = nullptr;
    }

    int32_t GetFlags() const
    {
        return mFlags;
    }

    void SetImportAssetDigest(const std::string& digest)
    {
        mImportAssetDigest = digest;
    }

    std::string GetImportAssetDigest() const
    {
        return mImportAssetDigest;
    }

    void SetCurveSubdivisionNumber(const uint32_t curveSubdivisionNumber)
    {
        mCurveSubdivisionNumber = curveSubdivisionNumber;
    }

    uint32_t GetCurveSubdivisionNumber() const
    {
        return mCurveSubdivisionNumber;
    }

private:

    AssetType GetAssetTypeInternal(const std::string& assetPath) const;

    // If it's not null, the import asset is from cached stage of USD.
    PXR_NS::UsdStageRefPtr mCachedStage = nullptr;
    std::string mImportAssetPath;
    AssetType mImportAssetType = AssetType::OTHER;
    std::string mOutputAssetPath;
    AssetType mOutputAssetType = AssetType::OTHER;
    OmniConverterCallbacks mCallbacks;
    std::string mCacheFolder;
    int32_t mFlags = 0;
    bool mPopulateMaterialsOnly = false;
    std::string mImportAssetDigest;
    uint32_t mCurveSubdivisionNumber = 1;
};
