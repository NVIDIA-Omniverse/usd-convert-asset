// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include <stddef.h>

#ifdef OMNI_ASSET_CONVERTER_EXPORTS
#    ifdef __cplusplus
#        define OMNI_ASSET_CONVERTER_EXPORT_C extern "C"
#    else
#        define OMNI_ASSET_CONVERTER_EXPORT_C
#    endif

#    if defined(_WIN32)
#        undef OMNI_ASSET_CONVERTER_EXPORT
#        define OMNI_ASSET_CONVERTER_EXPORT OMNI_ASSET_CONVERTER_EXPORT_C __declspec(dllexport)
#    elif defined(__linux__)
#        undef OMNI_ASSET_CONVERTER_EXPORT
#        define OMNI_ASSET_CONVERTER_EXPORT OMNI_ASSET_CONVERTER_EXPORT_C __attribute__((visibility("default")))
#    endif
#else
#    undef OMNI_ASSET_CONVERTER_EXPORT
#    define OMNI_ASSET_CONVERTER_EXPORT extern "C"
#endif

#include <stdint.h>
#include <string>
#include <vector>

// Don't export animation from asset
#define OMNI_CONVERTER_FLAGS_IGNORE_ANIMATION (0x1)

// Don't export materials from asset
#define OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS (0x1 << 1)

// Export Single props USD even there are native instancing in the imported assets. By default, it will export separate
// USD files for instancing assets.
#define OMNI_CONVERTER_FLAGS_SINGLE_MESH_FILE (0x1 << 2)

// Generate Smooth Normals for mesh.
#define OMNI_CONVERTER_FLAGS_GEN_SMOOTH_NORMALS (0x1 << 3)

// Don't export camera from asset
#define OMNI_CONVERTER_FLAGS_IGNORE_CAMERAS (0x1 << 4)


// Export pointer instancer if possible
#define OMNI_CONVERTER_FLAGS_SUPPORT_POINTER_INSTANCER (0x1 << 6)

// DEPRECATED: Export usd as if shapenet obj file was input
#define OMNI_CONVERTER_FLAGS_EXPORT_AS_SHAPENET (0x1 << 7)


// Use meter as world unit. It's centimeter by default.
#define OMNI_CONVERTER_FLAGS_USE_METER_PER_UNIT (0x1 << 9)

// Creates a /World prim as the default root or not.
#define OMNI_CONVERTER_FLAGS_CREATE_WORLD_AS_DEFAULT_PRIM (0x1 << 10)

// Don't export lights from asset
#define OMNI_CONVERTER_FLAGS_IGNORE_LIGHTS (0x1 << 11)

// Embedding texture during export
#define OMNI_CONVERTER_FLAGS_EMBED_FBX_TEXTURES (0x1 << 12)
#define OMNI_CONVERTER_FLAGS_EMBED_TEXTURES OMNI_CONVERTER_FLAGS_EMBED_FBX_TEXTURES

// Converting imported scene to be Z-UP. This is only applied to Fbx.
#define OMNI_CONVERTER_FLAGS_FBX_CONVERT_TO_Z_UP (0x1 << 13)

// Converting imported scene to be Y-UP. If both OMNI_CONVERTER_FLAGS_CONVERT_TO_Z_UP and
// OMNI_CONVERTER_FLAGS_CONVERT_TO_Y_UP are set, it will be converted Y-UP. This is only applied to Fbx.
#define OMNI_CONVERTER_FLAGS_FBX_CONVERT_TO_Y_UP (0x1 << 14)

// Keeping all materials including those ones that are not referenced by any meshes.
#define OMNI_CONVERTER_FLAGS_KEEP_ALL_MATERIALS (0x1 << 15)

// Merging all meshes to single one
#define OMNI_CONVERTER_FLAGS_MERGE_ALL_MESHES (0x1 << 16)

// Use double precision for scale and rotation op of USD.
#define OMNI_CONVERTER_FLAGS_USE_DOUBLE_PRECISION_FOR_USD_TRANSFORM_OP (0x1 << 17)

// Ignore pivots for FBX import.
#define OMNI_CONVERTER_FLAGS_IGNORE_PIVOTS (0x1 << 18)

// Disable scene instancing.
#define OMNI_CONVERTER_FLAGS_DISABLE_INSTANCING (0x1 << 19)

// Export hidden props. If props are not visible, it will be skipped.
#define OMNI_CONVERTER_FLAGS_EXPORT_HIDDEN_PROPS (0x1 << 20)

// Baking scales for FBX. FBX mesh is atrribute attached to a scene node, and the same of the scene node may not be
// identity. This flag can be set to bake scale into mesh points.
#define OMNI_CONVERTER_FLAGS_FBX_BAKING_SCALES_INTO_MESH (0x1 << 21)

// Keep units of original asset. By default, it will use 1cm per world unit.
// If OMNI_CONVERTER_FLAGS_USE_METER_PER_UNIT is specified, it will use 1m per world unit.
// If this flag is specified, OMNI_CONVERTER_FLAGS_USE_METER_PER_UNIT will not be useful,
// and it will respect the original units inside asset.
#define OMNI_CONVERTER_FLAGS_KEEP_WORLD_UNITS (0x1 << 22)

// Default to filter the flip value for rotation in animation.
#define OMNI_CONVERTER_FLAGS_IGNORE_FLIP_ROTATION (0x1 << 23)

// Default to import unbound bones for Fbx import.
#define OMNI_CONVERTER_FLAGS_FBX_IGNORE_UNBOUND_BONES (0x1 << 24)

// Default to export separate gltf.
#define OMNI_CONVERTER_FLAGS_EXPORT_EMBEDDED_GLTF (0x1 << 25)


// Set exported USD stage up axis to override to y-axis (1)
#define OMNI_CONVERTER_FLAGS_STAGE_UP_Y (0x1 << 27)

// Set exported USD stage up axis to override to z-axis (2)
#define OMNI_CONVERTER_FLAGS_STAGE_UP_Z (0x1 << 28)

enum class OmniConverterStatus : uint8_t
{
    OK, // Convert finished and successfully
    IN_PROGRESS, // Convert is in progress
    CANCELLED, // Convert is cancelled. This will be returned if you call omniConverterStopFuture
    UNSUPPORTED_IMPORT_FORMAT, // Source format is not supported
    INCOMPLETE_IMPORT_FORMAT, // Source file format is supported, but it's incomplete or broken.
    UNSUPPORTED_EXPORT_FORMAT, // Export format is not supported
    FILE_READ_ERROR, // File read error during convert.
    FILE_NOT_EXISTED, // File is not existed during convert.
    FILE_WRITE_ERROR, // File write error during convert.
    DIRECTORY_CREATE_FAILED, // Failed to create directory.
    UNKNOWN // Unknown issue.
};

typedef void (*OmniConverterBlobDataDeleter)(void* buffer);

// Data blob. Data blob is a handle to host
// data block from python to c++. This is
// used for OmniConverterReader to pass
// memory between python and c++.
struct OmniConverterBlob
{
    void* buffer = nullptr;
    size_t size = 0;
    // Function to call to free the buffer.
    OmniConverterBlobDataDeleter deleter = nullptr;
};

// Material description. It's handle to describe material as set of properties.
struct OmniConverterMaterialDescription;

// Future is an asynchronous object, that could be stop/wait/release
// You can refer the following API about how to operate future.
struct OmniConverterFuture;

/**
 * The callback you could provide to receive internal logs.
 */
typedef void (*OmniConverterLogCallback)(const char* message);

/**
 * The callback you could provide to converter for checking if file is existed already.
 * This callback should be provided along with makedirs and write as following.
 */
typedef bool (*OmniConverterPathExists)(const char* path);

/**
 * The callback you could provide to converter for making directories.
 */
typedef bool (*OmniConverterMakeDirs)(const char* path);

/**
 * The callback you could provide to converter for customizing the file write.
 * This callback will not handle USD content, for which, you can see
 * @param path Target path that data writes to.
 * @param data Asset data.
 * @param size Asset size.
 * @return True if success, false otherwise.
 */
typedef bool (*OmniConverterBinaryWriter)(const char* path, OmniConverterBlob* blob);

/**
 * The callback you could provide to converter for customizing the USD layer write.
 */
typedef bool (*OmniConverterUsdWriter)(const char* targetPath, const char* layerIdentifier);

/**
 * The callback you could provide to converter for customizing the file copy.
 * @param targetPath The target path that the source path will be copied to.
 * @param sourcePath The source file it will be copied.
 */
typedef bool (*OmniConverterFileCopy)(const char* targetPath, const char* sourcePath);

/**
 * The callback you could provide to converter for customizing the file read.
 * @param path Target path that data writes to.
 * @param OmniConverterBlob Blob to return. Blob will be filled with
 * @return True if success, false otherwise.
 */
typedef bool (*OmniConverterReader)(const char* path, OmniConverterBlob* blob);

/**
 * The callback you could provide to receive progress of future.
 * @param future The converting future.
 * @param progress Current progress, which is a integer that less or equal to total.
 * @param total Total steps to be finished.
 */
typedef void (*OmniConverterProgressReporter)(OmniConverterFuture* future, uint32_t progress, uint32_t total);

/**
 * The callback you could provide to intercept the load of materials.
 */
typedef bool (*OmniConverterMaterialLoader)(OmniConverterFuture* future, OmniConverterMaterialDescription* material);


/**
 * Sets the cache store for caching. For glTF/OBJ/FBX to in-memory USD conversion, the USD results will be cached. By
 * default, if the cache location is not provided, caching will be disabled. Pass null or an empty string to clear a
 * previously configured cache folder. Btw, cacheFolder should only be system path.
 * @see omniConverterCreateAsset for in-memory conversion.
 */
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetCacheFolder(const char* cacheFolder);

/**
 * Sets customizable log callback.
 */
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetLogCallback(OmniConverterLogCallback logCallback);

/**
 * Sets progress monitor.
 */
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetProgressCallback(OmniConverterProgressReporter progressCallback);

/**
 * Sets customizable write callback. You can use it to intercept the file write to
 * anywhere you like, like OV. If you don't set this, it will write to treat all paths
 * as local path and do local write.
 */
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetFileCallbacks(
    OmniConverterMakeDirs makeDirsCallback,
    OmniConverterReader readCallback,
    OmniConverterBinaryWriter binaryWriteCallback,
    OmniConverterPathExists fileExistsCallback,
    OmniConverterUsdWriter layerWriteCallback,
    OmniConverterFileCopy fileCopyCallback
);

/**
 * Sets customizable material load callback. By default, it will use fallback loading process
 * inside asset importer. If this callback is given, client will take ownership of the material load
 * by giving a set of material inputs reading from raw assets and returning the outputs that will be mapped
 * to USD.
 */
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetMaterialCallback(OmniConverterMaterialLoader materialLoader);


/**
 * DEPRECATED: This API is replaced with more general one (@see omniConverterCreateAsset)
 * Starts a future that converts asset to usd. This API is asynchronous you need to use the following
 * future API to do synchronization.
 * @param assetPath Asset that will be imported. It supports all formats that Assimp supports, see following
 *                  link for reference: https://github.com/assimp/assimp.
 * @param outputUSDPath USD export path. This is the top USD of the scene converted. Other USDs or Assets
 *                      will be put under the same directory.
 * @param flags Export flags. See OMNI_CONVERTER_FLAGS_* for reference. By default, it will export all.
 * @return Asynchronous OmniConverterFuture object.
 */
OMNI_ASSET_CONVERTER_EXPORT OmniConverterFuture* omniConverterCreateUSD(const char* assetPath, const char* outputUSDPath, int32_t flags);

/**
 * Starts a future that converts from one asset type to another. This API is asynchronous you need to use the following
 * future API to do synchronization.
 * @param assetPath Asset that will be imported. It supports USD, and all formats that Assimp supports, see following
 *                  link for reference: https://github.com/assimp/assimp. Btw, assetPath could also be
 * PXR_NS::UsdStageCache::Id. If it's cached stage id, it will query stage from USD cache by
 * PXR_NS::UsdUtilsStageCache::Get().Find. This is mainly for USD stage export.
 * @param outputAssetPath Asset export path. This is the top asset of the scene converted. Other referenced assets
 *                        will be put under the same directory. If outputAssetPath is anonymous identifier of USD, it
 * means to import the asset into an in-memory USD layer. This layer must be existed, and it will not copy other
 * referenced assets, like textures, but reference them directly. Also, it will always be imported as UsdPreviewSurface
 * for all materials. Also, it will only export one animation track and all imported objects will be in the same USD, no
 * matter flag OMNI_CONVERTER_FLAGS_SINGLE_MESH_FILE is set or not. Also, material load will be disabled even it's set.
 * @param flags Export flags. See OMNI_CONVERTER_FLAGS_* for reference. By default, it will export all.
 * @return Asynchronous OmniConverterFuture object.
 */
OMNI_ASSET_CONVERTER_EXPORT OmniConverterFuture* omniConverterCreateAsset(const char* assetPath, const char* outputAssetPath, int32_t flags);

OMNI_ASSET_CONVERTER_EXPORT OmniConverterFuture* omniConverterCreateCurveAsset(
    const char* assetPath,
    const char* outputAssetPath,
    int32_t flags,
    int32_t curveSubdivision
);

/**
 * Checks future current status.
 */
OMNI_ASSET_CONVERTER_EXPORT OmniConverterStatus omniConverterCheckFutureStatus(OmniConverterFuture* future);

/**
 * Gets detailed error.
 */
OMNI_ASSET_CONVERTER_EXPORT const char* omniConverterGetFutureDetailedError(OmniConverterFuture* future);

/**
 * Cancels future. Only when future is in progress, it's cancellable. omniConverterCheckFutureStatus will return
 * CANCELLED.
 */
OMNI_ASSET_CONVERTER_EXPORT void omniConverterCancelFuture(OmniConverterFuture* future);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterReleaseFuture(OmniConverterFuture* future);


/*******************Material interfaces*************************/
struct OmniConverterDouble2
{
    double value[2] = { 0.0, 0.0 };
};

struct OmniConverterDouble3
{
    double value[3] = { 0.0, 0.0, 0.0 };
};

struct OmniConverterDouble4
{
    double value[4] = {
        0.0,
    };
};

struct OmniConverterDouble9
{
    double value[9] = {
        0.0,
    };
};

struct OmniConverterDouble16
{
    double value[16] = {
        0.0,
    };
};

struct OmniConverterString
{
    const char* value;
    size_t length;
};

// A material description includes a group of <key, value> properties.
// OmniConverterMaterialProperty is used for both inputs and outputs of material loader.
// For inputs, it describes the material property reading from raw assets.
// For outputs, it's defined by client in material loader to describe the
// inputs to map into USD.
struct OmniConverterMaterialProperty;

// The exact type of property.
// You can refer OmniConverterMaterialPropertyValueType
// for the value type. While for output returned by
// material loader, it needs to know the exact property
// type that will be mapped to USD and MDL. This will
// come as part of the OmniConverterMDLPropertyMetadata.
enum OmniConverterMDLPropertyType
{
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_POINT3D,
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_VECTOR3D,
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_NORMAL3D,
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_COLOR3D,
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_COLOR4D,
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_QUATD,
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_MATRIX2D,
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_MATRIX3D,
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_MATRIX4D,
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_TEXCOORD2D,
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_TEXCOORD3D,
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_TOKEN,
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_ASSET,
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_UNDEFINED,
    OMNI_CONVERTER_MDL_PROPERTY_TYPE_COUNT
};

// OmniConverterMDLPropertyMetadata records the extra meta
// that will be used to precisely map to USD, which only applies
// to outputs of material loader.
struct OmniConverterMDLPropertyMetadata
{
    const char* displayName;
    const char* groupName;

    // Detailed type. If it's not given, it will
    // map to underlying value type specified by
    // OmniConverterMaterialPropertyValueType.
    OmniConverterMDLPropertyType detailType;

    bool hasDefaultValue;
    bool singlePrecision; // Store as single precision float in USD if it's true and value type is double.
    typedef union
    {
        bool boolValue;
        int32_t intValue;
        double doubleValue;
        double double2Value[2];
        double double3Value[3];
        double double4Value[4];
        double double9Value[9];
        double double16Value[16];
    } _Value;
    _Value defaultValue;

    bool hasMinValue;
    _Value minValue;

    bool hasMaxValue;
    _Value maxValue;

    // The color space of this property
    // It's empty by default.
    const char* colorSpace;
};

// It describes the underlying value type stored.
enum OmniConverterMaterialPropertyValueType
{
    OMNI_CONVERTER_VALUE_TYPE_BOOL,
    OMNI_CONVERTER_VALUE_TYPE_INT32,
    OMNI_CONVERTER_VALUE_TYPE_DOUBLE,
    OMNI_CONVERTER_VALUE_TYPE_DOUBLE2,
    OMNI_CONVERTER_VALUE_TYPE_DOUBLE3,
    OMNI_CONVERTER_VALUE_TYPE_DOUBLE4,
    OMNI_CONVERTER_VALUE_TYPE_DOUBLE9,
    OMNI_CONVERTER_VALUE_TYPE_DOUBLE16,
    OMNI_CONVERTER_VALUE_TYPE_STRING,
    OMNI_CONVERTER_VALUE_TYPE_UNDEFINED,
    OMNI_CONVERTER_VALUE_TYPES_COUNT
};

struct OmniConverterAssetHandle;

OMNI_ASSET_CONVERTER_EXPORT OmniConverterAssetHandle* omniConverterOpenAsset(const char* assetPath);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterCloseAsset(OmniConverterAssetHandle* handle);
OMNI_ASSET_CONVERTER_EXPORT size_t omniConverterGetNumMaterials(OmniConverterAssetHandle* handle);
OMNI_ASSET_CONVERTER_EXPORT OmniConverterMaterialDescription* omniConverterGetMaterialDescription(OmniConverterAssetHandle* handle, size_t index);
OMNI_ASSET_CONVERTER_EXPORT OmniConverterMaterialDescription* omniConverterCreateMaterialDescription(const char* materialName);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterReleaseMaterialDescription(OmniConverterMaterialDescription* material);
OMNI_ASSET_CONVERTER_EXPORT OmniConverterMaterialDescription* omniConverterCopyMaterialDescription(OmniConverterMaterialDescription* material);
OMNI_ASSET_CONVERTER_EXPORT size_t omniConverterGetNumInputProperties(OmniConverterMaterialDescription* material);
OMNI_ASSET_CONVERTER_EXPORT void omniConverteSetMaterialFilePath(
    OmniConverterMaterialDescription* material,
    const char* materialFilePath,
    const char* materialEntryIdentifier,
    bool builtIn
);
OMNI_ASSET_CONVERTER_EXPORT const char* omniConverteGetMaterialFilePath(OmniConverterMaterialDescription* material);
OMNI_ASSET_CONVERTER_EXPORT const char* omniConverteGetMaterialEntryIdentifier(OmniConverterMaterialDescription* material);
OMNI_ASSET_CONVERTER_EXPORT OmniConverterMaterialProperty* omniConverterGetInputProperty(OmniConverterMaterialDescription* material, size_t index);
OMNI_ASSET_CONVERTER_EXPORT size_t omniConverterGetNumOutputProperties(OmniConverterMaterialDescription* material);
OMNI_ASSET_CONVERTER_EXPORT OmniConverterMaterialProperty* omniConverterCreateOutputProperty(
    OmniConverterMaterialDescription* material,
    const char* name,
    OmniConverterMDLPropertyMetadata* metadata
);
OMNI_ASSET_CONVERTER_EXPORT void omniConverteSetOutputPropertyMDLMeta(
    OmniConverterMaterialProperty* property,
    OmniConverterMDLPropertyMetadata* metadata
);
OMNI_ASSET_CONVERTER_EXPORT OmniConverterMaterialProperty* omniConverterGetOutputProperty(OmniConverterMaterialDescription* material, size_t index);
OMNI_ASSET_CONVERTER_EXPORT const char* omniConverterGetMaterialName(OmniConverterMaterialDescription* material);
OMNI_ASSET_CONVERTER_EXPORT const char* omniConverterGetMaterialClassId(OmniConverterMaterialDescription* material);
OMNI_ASSET_CONVERTER_EXPORT const char* omniConverterGetPropertyName(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT const char* omniConverterGetPropertyMDLGroupName(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT const char* omniConverterGetPropertyMDLDisplayName(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT OmniConverterMaterialPropertyValueType omniConverterGetPropertyValueType(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT bool omniConverterIsTextureProperty(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetIsTextureProperty(OmniConverterMaterialProperty* property, bool value);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetTextureTranslation(OmniConverterMaterialProperty* property, OmniConverterDouble2 value);
OMNI_ASSET_CONVERTER_EXPORT OmniConverterDouble2 omniConverterGetTextureTranslation(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetTextureScale(OmniConverterMaterialProperty* property, OmniConverterDouble2 value);
OMNI_ASSET_CONVERTER_EXPORT OmniConverterDouble2 omniConverterGetTextureScale(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetBool(OmniConverterMaterialProperty* property, bool value);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetInt32(OmniConverterMaterialProperty* property, int32_t value);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetDouble(OmniConverterMaterialProperty* property, double value);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetDouble2(OmniConverterMaterialProperty* property, OmniConverterDouble2 value);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetDouble3(OmniConverterMaterialProperty* property, OmniConverterDouble3 value);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetDouble4(OmniConverterMaterialProperty* property, OmniConverterDouble4 value);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetDouble9(OmniConverterMaterialProperty* property, OmniConverterDouble9 value);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetDouble16(OmniConverterMaterialProperty* property, OmniConverterDouble16 value);
OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetString(OmniConverterMaterialProperty* property, OmniConverterString value);
OMNI_ASSET_CONVERTER_EXPORT bool omniConverterToBool(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT int32_t omniConverterToInt32(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT double omniConverterToDouble(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT OmniConverterDouble2 omniConverterToDouble2(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT OmniConverterDouble3 omniConverterToDouble3(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT OmniConverterDouble4 omniConverterToDouble4(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT OmniConverterDouble9 omniConverterToDouble9(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT OmniConverterDouble16 omniConverterToDouble16(OmniConverterMaterialProperty* property);
OMNI_ASSET_CONVERTER_EXPORT const char* omniConverterToString(OmniConverterMaterialProperty* property);
