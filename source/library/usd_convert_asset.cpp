// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#define OMNI_ASSET_CONVERTER_EXPORTS
#include "usd_convert_asset.h"

#include "converter_future.h"
#include "importers/importer.h"
#include "pxr_includes.h"
#include "usd_convert_asset_internal.h"
#include "utils/unicode.h"
#include "utils/utils.h"

#include <algorithm>
#include <fstream>

static std::mutex gConverterMutex;
static OmniConverterLogCallback gConverterLogCallback = nullptr;
static OmniConverterMakeDirs gConverterMakeDirsCallback = nullptr;
static OmniConverterReader gConverterReadCallback = nullptr;
static OmniConverterBinaryWriter gConverterBinaryWriteCallback = nullptr;
static OmniConverterUsdWriter gConverterLayerWriteCallback = nullptr;
static OmniConverterUsdWriter gConverterFileCopyCallback = nullptr;
static OmniConverterPathExists gConverterPathExistsCallback = nullptr;
static OmniConverterProgressReporter gConverterProgressReporter = nullptr;
static OmniConverterMaterialLoader gConverterMaterialLoader = nullptr;
static std::string gCacheFolder;

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetCacheFolder(const char* cacheFolder)
{
    std::lock_guard<decltype(gConverterMutex)> lock(gConverterMutex);
    if (cacheFolder && cacheFolder[0] != '\0')
    {
        gCacheFolder = cacheFolder;
    }
    else
    {
        gCacheFolder.clear();
    }
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetLogCallback(OmniConverterLogCallback logCallback)
{
    std::lock_guard<decltype(gConverterMutex)> lock(gConverterMutex);
    gConverterLogCallback = logCallback;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetProgressCallback(OmniConverterProgressReporter progressCallback)
{
    std::lock_guard<decltype(gConverterMutex)> lock(gConverterMutex);
    gConverterProgressReporter = progressCallback;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetMaterialCallback(OmniConverterMaterialLoader materialLoader)
{
    std::lock_guard<decltype(gConverterMutex)> lock(gConverterMutex);
    gConverterMaterialLoader = materialLoader;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetFileCallbacks(
    OmniConverterMakeDirs makeDirsCallback,
    OmniConverterReader readCallback,
    OmniConverterBinaryWriter binaryWriteCallback,
    OmniConverterPathExists fileExistsCallback,
    OmniConverterUsdWriter layerWriteCallback,
    OmniConverterFileCopy fileCopyCallback
)
{
    std::lock_guard<decltype(gConverterMutex)> lock(gConverterMutex);
    gConverterMakeDirsCallback = makeDirsCallback;
    gConverterReadCallback = readCallback;
    gConverterBinaryWriteCallback = binaryWriteCallback;
    gConverterPathExistsCallback = fileExistsCallback;
    gConverterLayerWriteCallback = layerWriteCallback;
    gConverterFileCopyCallback = fileCopyCallback;
}


static OmniConverterContext CreateConverterContext(
    const char* assetPath,
    const char* outputAssetPath,
    int32_t flags,
    bool populateMaterialsOnly = false
)
{
    OmniConverterCallbacks callbacks;

    static OmniConverterLogCallback defaultLogCallback = [](const char* message)
    {
    };

    static OmniConverterMakeDirs defaultMakeDirCallback = [](const char* path)
    {
        return PathUtils::MakeDirectories(path);
    };

    static OmniConverterMakeDirs defaultPathExistsCallback = [](const char* path)
    {
        return PathUtils::IsPathExisted(path);
    };

    static OmniConverterReader defaultReadCallback = [](const char* path, OmniConverterBlob* blob)
    {
        PXR_NS::ArResolver& resolver = PXR_NS::ArGetResolver();
#if PXR_MINOR_VERSION > 21 || (PXR_MINOR_VERSION == 21 && PXR_PATCH_VERSION >= 11)
        std::shared_ptr<PXR_NS::ArAsset> asset = resolver.OpenAsset(PXR_NS::ArResolvedPath(path));
#else
        std::shared_ptr<PXR_NS::ArAsset> asset = resolver.OpenAsset(path);
#endif

        if (!asset)
        {
            return false;
        }

        blob->size = asset->GetSize();
        if (blob->size > 0)
        {
            blob->buffer = new uint8_t[asset->GetSize()];
            if (asset->Read(blob->buffer, asset->GetSize(), 0) == 0)
            {
                delete[] (uint8_t*)blob->buffer;
                blob->size = 0;
                return false;
            }
        }

        blob->deleter = gBlobDefaultDataDeleter;
        return true;
    };

    static OmniConverterBinaryWriter defaultBinaryWriteCallback = [](const char* path, OmniConverterBlob* blob)
    {
        if (!blob || !blob->buffer || blob->size == 0)
        {
            return false;
        }

#if defined(_WIN32)
        std::string filename = "\\\\?\\" + std::string(path);
        std::replace(filename.begin(), filename.end(), '/', '\\');
#else
        std::string filename = path;
#endif
        std::ofstream os(filename, std::ios::out | std::ios::binary);
        if (!os.is_open())
        {
            return false;
        }
        os.write((const char*)blob->buffer, blob->size);

        return true;
    };

    static OmniConverterUsdWriter defaultLayerWriteCallback = [](const char* targetPath, const char* layerIdentifier)
    {
        auto layer = PXR_NS::SdfLayer::Find(layerIdentifier);
        if (!layer)
        {
            return false;
        }

        bool equal = false;
        PXR_NS::SdfLayerRefPtr targetLayer = nullptr;
        // Checks paths firstly.
        if (PathUtils::Equal(layerIdentifier, targetPath))
        {
            equal = true;
        }
        else
        {
            // Otherwise, checkes layer to see if they are the same layer.
            // It's possible that paths are not equal but layers are equal which is depdendent
            // on the implementation of resolver.
            targetLayer = PXR_NS::SdfLayer::FindOrOpen(targetPath);
            if (targetLayer == layer)
            {
                equal = true;
            }
        }

        if (equal)
        {
            if (layer->IsAnonymous())
            {
                return true;
            }

            return layer->Save();
        }

        if (!targetLayer)
        {
            auto format = PXR_NS::SdfFileFormat::FindByExtension(targetPath);
            targetLayer = PXR_NS::SdfLayer::New(format, targetPath);
        }
        else
        {
            targetLayer->Clear();
        }

        if (!targetLayer)
        {
            return false;
        }

        targetLayer->TransferContent(layer);
        if (!targetLayer->IsAnonymous())
        {
            return targetLayer->Save();
        }

        return true;
    };

    static OmniConverterProgressReporter defaultProgressReporter = [](OmniConverterFuture* future, uint32_t progress, uint32_t total)
    {
    };

    std::string cacheFolder;
    {
        std::lock_guard<decltype(gConverterMutex)> lock(gConverterMutex);
        callbacks.logCallback = gConverterLogCallback ? gConverterLogCallback : defaultLogCallback;
        callbacks.readCallback = gConverterReadCallback ? gConverterReadCallback : defaultReadCallback;
        callbacks.binaryWriteCallback = gConverterBinaryWriteCallback ? gConverterBinaryWriteCallback : defaultBinaryWriteCallback;
        callbacks.makeDirsCallback = gConverterMakeDirsCallback ? gConverterMakeDirsCallback : defaultMakeDirCallback;
        callbacks.pathExistsCallback = gConverterPathExistsCallback ? gConverterPathExistsCallback : defaultPathExistsCallback;
        callbacks.progressCallback = gConverterProgressReporter ? gConverterProgressReporter : defaultProgressReporter;
        callbacks.layerWriteCallback = gConverterLayerWriteCallback ? gConverterLayerWriteCallback : defaultLayerWriteCallback;
        callbacks.fileCopyCallback = gConverterFileCopyCallback ? gConverterFileCopyCallback : nullptr;

        // For in-memory import, material loader will be disabled.
        if (outputAssetPath && !PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(outputAssetPath))
        {
            callbacks.materialLoader = gConverterMaterialLoader;
        }


        cacheFolder = gCacheFolder;
    }

    std::string importAssetPathString;
    std::string outputAssetPathString;
    if (assetPath)
    {
        importAssetPathString = assetPath;
    }

    if (outputAssetPath)
    {
        outputAssetPathString = outputAssetPath;
    }

    return OmniConverterContext(importAssetPathString, outputAssetPathString, callbacks, cacheFolder, flags, populateMaterialsOnly);
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterFuture* omniConverterCreateUSD(const char* assetPath, const char* outputUSDPath, int32_t flags)
{
    return omniConverterCreateAsset(assetPath, outputUSDPath, flags);
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterFuture* omniConverterCreateAsset(const char* usdPath, const char* outputAssetPath, int32_t flags)
{
    const auto& context = CreateConverterContext(usdPath, outputAssetPath, flags);
    auto future = new OmniConverterFuture(context);
    future->Start();

    return future;
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterFuture* omniConverterCreateCurveAsset(
    const char* usdPath,
    const char* outputAssetPath,
    int32_t flags,
    int32_t curveSubdivision
)
{
    auto context = CreateConverterContext(usdPath, outputAssetPath, flags);
    context.SetCurveSubdivisionNumber(curveSubdivision);
    auto future = new OmniConverterFuture(context);
    future->Start();

    return future;
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterStatus omniConverterCheckFutureStatus(OmniConverterFuture* future)
{
    return future->GetStatus();
}

OMNI_ASSET_CONVERTER_EXPORT const char* omniConverterGetFutureDetailedError(OmniConverterFuture* future)
{
    return future->GetDetailedError().c_str();
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterCancelFuture(OmniConverterFuture* future)
{
    future->Stop();
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterReleaseFuture(OmniConverterFuture* future)
{
    delete future;
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterAssetHandle* omniConverterOpenAsset(const char* assetPath)
{
    // Populate materials only.
    auto context = CreateConverterContext(assetPath, nullptr, 0, true);
    const auto& importAssetType = context.GetImportAssetType();
    if (importAssetType != AssetType::FBX && !context.IsImportAssetUsdcOrUsdaOrUsdz())
    {
        context.Log("WARNING: Skipping to open asset " + std::string(assetPath) + " as only FBX and USD are supported currently.");

        return nullptr;
    }

    if (!context.IsPathExisted(context.GetImportAssetPath()))
    {
        context.Log("Open asset failed as " + context.GetImportAssetPath() + " cannot be found.");
        return nullptr;
    }

    OmniConverterStatus status = OmniConverterStatus::OK;
    std::string detailedError;
    auto importer = CreateImporter(context);
    auto futureContext = std::make_shared<OmniConverterFuture::FutureThreadContext>();
    futureContext->converterContext = context;
    auto importedStage = importer->ImportStage(futureContext, status, detailedError);
    if (importedStage && status == OmniConverterStatus::OK)
    {
        OmniConverterAssetHandle* assetHandle = new OmniConverterAssetHandle;
        for (const auto& material : importedStage->materials)
        {
            OmniConverterMaterialDescription description;
            description.name = material->name;
            description.classId = material->materialType;
            description.inputProperties = material->rawAssetProperties;
            assetHandle->materials.push_back(description);
        }

        return assetHandle;
    }
    else
    {
        context.Log("Populates materials from asset " + context.GetImportAssetPath() + " failed with unknown error.");

        return nullptr;
    }
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterCloseAsset(OmniConverterAssetHandle* handle)
{
    if (handle)
    {
        delete handle;
    }
}

OMNI_ASSET_CONVERTER_EXPORT size_t omniConverterGetNumMaterials(OmniConverterAssetHandle* handle)
{
    if (handle)
    {
        return handle->materials.size();
    }
    else
    {
        return 0;
    }
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterMaterialDescription* omniConverterGetMaterialDescription(OmniConverterAssetHandle* handle, size_t index)
{
    if (handle)
    {
        return &handle->materials[index];
    }
    else
    {
        return nullptr;
    }
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterMaterialDescription* omniConverterCreateMaterialDescription(const char* materialName)
{
    OmniConverterMaterialDescription* material = new OmniConverterMaterialDescription;
    material->name = materialName;
    return material;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterReleaseMaterialDescription(OmniConverterMaterialDescription* material)
{
    delete material;
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterMaterialDescription* omniConverterCopyMaterialDescription(OmniConverterMaterialDescription* material)
{
    OmniConverterMaterialDescription* newMaterial = new OmniConverterMaterialDescription(*material);

    return newMaterial;
}

OMNI_ASSET_CONVERTER_EXPORT size_t omniConverterGetNumInputProperties(OmniConverterMaterialDescription* material)
{
    return material->inputProperties.size();
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverteSetMaterialFilePath(
    OmniConverterMaterialDescription* material,
    const char* materialFilePath,
    const char* materialEntryIdentifier,
    bool builtIn
)
{
    if (materialFilePath)
    {
        material->materialPath = materialFilePath;
    }

    if (materialEntryIdentifier)
    {
        material->entryIdentifier = materialEntryIdentifier;
    }

    material->builtin = builtIn;
}

OMNI_ASSET_CONVERTER_EXPORT const char* omniConverteGetMaterialFilePath(OmniConverterMaterialDescription* material)
{
    return material->materialPath.c_str();
}

OMNI_ASSET_CONVERTER_EXPORT const char* omniConverteGetMaterialEntryIdentifier(OmniConverterMaterialDescription* material)
{
    return material->entryIdentifier.c_str();
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterMaterialProperty* omniConverterGetInputProperty(OmniConverterMaterialDescription* material, size_t index)
{
    if (index < material->inputProperties.size())
    {
        return &material->inputProperties[index];
    }

    return nullptr;
}

OMNI_ASSET_CONVERTER_EXPORT size_t omniConverterGetNumOutputProperties(OmniConverterMaterialDescription* material)
{
    return material->outputProperties.size();
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterMaterialProperty* omniConverterCreateOutputProperty(
    OmniConverterMaterialDescription* material,
    const char* name,
    OmniConverterMDLPropertyMetadata* metadata
)
{
    material->outputProperties.push_back(OmniConverterMaterialProperty());
    auto& property = material->outputProperties.back();
    if (metadata)
    {
        if (name)
        {
            property.name = name;
        }

        if (metadata->displayName)
        {
            property.displayName = metadata->displayName;
        }

        if (metadata->groupName)
        {
            property.groupName = metadata->groupName;
        }

        if (metadata->colorSpace)
        {
            property.colorSpace = metadata->colorSpace;
        }

        property.singlePrecision = metadata->singlePrecision;
        property.detailedType = metadata->detailType;
        property.hasDefaultValue = metadata->hasDefaultValue;
        property.defaultValue = metadata->defaultValue;
        property.hasMinValue = metadata->hasMinValue;
        property.minValue = metadata->minValue;
        property.hasMaxValue = metadata->hasMaxValue;
        property.maxValue = metadata->maxValue;
    }

    return &property;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverteSetOutputPropertyMDLMeta(
    OmniConverterMaterialProperty* property,
    OmniConverterMDLPropertyMetadata* metadata
)
{
    if (metadata)
    {
        if (metadata->displayName)
        {
            property->displayName = metadata->displayName;
        }

        if (metadata->groupName)
        {
            property->groupName = metadata->groupName;
        }
    }
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterMaterialProperty* omniConverterGetOutputProperty(OmniConverterMaterialDescription* material, size_t index)
{
    if (index < material->outputProperties.size())
    {
        return &material->outputProperties[index];
    }

    return nullptr;
}

OMNI_ASSET_CONVERTER_EXPORT const char* omniConverterGetMaterialName(OmniConverterMaterialDescription* material)
{
    return material->name.c_str();
}

OMNI_ASSET_CONVERTER_EXPORT const char* omniConverterGetMaterialClassId(OmniConverterMaterialDescription* material)
{
    return material->classId.c_str();
}

OMNI_ASSET_CONVERTER_EXPORT const char* omniConverterGetPropertyName(OmniConverterMaterialProperty* property)
{
    return property->name.c_str();
}

OMNI_ASSET_CONVERTER_EXPORT const char* omniConverterGetPropertyMDLGroupName(OmniConverterMaterialProperty* property)
{
    return property->groupName.c_str();
}

OMNI_ASSET_CONVERTER_EXPORT const char* omniConverterGetPropertyMDLDisplayName(OmniConverterMaterialProperty* property)
{
    return property->displayName.c_str();
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterMaterialPropertyValueType omniConverterGetPropertyValueType(OmniConverterMaterialProperty* property)
{
    return property->valueType;
}

OMNI_ASSET_CONVERTER_EXPORT bool omniConverterIsTextureProperty(OmniConverterMaterialProperty* property)
{
    return property->isTextureProperty;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetIsTextureProperty(OmniConverterMaterialProperty* property, bool value)
{
    property->isTextureProperty = value;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetTextureTranslation(OmniConverterMaterialProperty* property, OmniConverterDouble2 value)
{
    property->textureTranslation[0] = value.value[0];
    property->textureTranslation[1] = value.value[1];
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterDouble2 omniConverterGetTextureTranslation(OmniConverterMaterialProperty* property)
{
    return { property->textureTranslation[0], property->textureTranslation[1] };
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetTextureScale(OmniConverterMaterialProperty* property, OmniConverterDouble2 value)
{
    property->textureScale[0] = value.value[0];
    property->textureScale[1] = value.value[1];
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterDouble2 omniConverterGetTextureScale(OmniConverterMaterialProperty* property)
{
    return { property->textureScale[0], property->textureScale[1] };
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetBool(OmniConverterMaterialProperty* property, bool value)
{
    property->value.boolValue = value;
    property->valueType = OMNI_CONVERTER_VALUE_TYPE_BOOL;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetInt32(OmniConverterMaterialProperty* property, int32_t value)
{
    property->value.intValue = value;
    property->valueType = OMNI_CONVERTER_VALUE_TYPE_INT32;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetDouble(OmniConverterMaterialProperty* property, double value)
{
    property->value.doubleValue = value;
    property->valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetDouble2(OmniConverterMaterialProperty* property, OmniConverterDouble2 value)
{
    property->value.double2Value[0] = value.value[0];
    property->value.double2Value[1] = value.value[1];
    property->valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE2;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetDouble3(OmniConverterMaterialProperty* property, OmniConverterDouble3 value)
{
    property->value.double3Value[0] = value.value[0];
    property->value.double3Value[1] = value.value[1];
    property->value.double3Value[2] = value.value[2];
    property->valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE3;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetDouble4(OmniConverterMaterialProperty* property, OmniConverterDouble4 value)
{
    property->value.double4Value[0] = value.value[0];
    property->value.double4Value[1] = value.value[1];
    property->value.double4Value[2] = value.value[2];
    property->value.double4Value[3] = value.value[3];
    property->valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE4;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetDouble9(OmniConverterMaterialProperty* property, OmniConverterDouble9 value)
{
    memcpy(property->value.double9Value, value.value, sizeof(value.value));
    property->valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE9;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetDouble16(OmniConverterMaterialProperty* property, OmniConverterDouble16 value)
{
    memcpy(property->value.double16Value, value.value, sizeof(value.value));
    property->valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE16;
}

OMNI_ASSET_CONVERTER_EXPORT void omniConverterSetString(OmniConverterMaterialProperty* property, OmniConverterString value)
{
    if (value.length > 0 && value.value)
    {
        property->stringValue.assign(value.value, value.value + value.length);
        property->valueType = OMNI_CONVERTER_VALUE_TYPE_STRING;
    }
}

OMNI_ASSET_CONVERTER_EXPORT bool omniConverterToBool(OmniConverterMaterialProperty* property)
{
    if (property->valueType == OMNI_CONVERTER_VALUE_TYPE_BOOL)
    {
        return property->value.boolValue;
    }

    return false;
}

OMNI_ASSET_CONVERTER_EXPORT int32_t omniConverterToInt32(OmniConverterMaterialProperty* property)
{
    if (property->valueType == OMNI_CONVERTER_VALUE_TYPE_INT32)
    {
        return property->value.intValue;
    }

    return 0;
}

OMNI_ASSET_CONVERTER_EXPORT double omniConverterToDouble(OmniConverterMaterialProperty* property)
{
    if (property->valueType == OMNI_CONVERTER_VALUE_TYPE_DOUBLE)
    {
        return property->value.doubleValue;
    }

    return 0.0;
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterDouble2 omniConverterToDouble2(OmniConverterMaterialProperty* property)
{
    if (property->valueType == OMNI_CONVERTER_VALUE_TYPE_DOUBLE2)
    {
        return { property->value.double2Value[0], property->value.double2Value[1] };
    }

    return OmniConverterDouble2();
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterDouble3 omniConverterToDouble3(OmniConverterMaterialProperty* property)
{
    if (property->valueType == OMNI_CONVERTER_VALUE_TYPE_DOUBLE3)
    {
        return { property->value.double3Value[0], property->value.double3Value[1], property->value.double3Value[2] };
    }

    return OmniConverterDouble3();
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterDouble4 omniConverterToDouble4(OmniConverterMaterialProperty* property)
{
    if (property->valueType == OMNI_CONVERTER_VALUE_TYPE_DOUBLE4)
    {
        return { property->value.double4Value[0], property->value.double4Value[1], property->value.double4Value[2], property->value.double4Value[3] };
    }

    return OmniConverterDouble4();
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterDouble9 omniConverterToDouble9(OmniConverterMaterialProperty* property)
{
    if (property->valueType == OMNI_CONVERTER_VALUE_TYPE_DOUBLE9)
    {
        OmniConverterDouble9 value;
        memcpy(value.value, property->value.double16Value, sizeof(property->value.double9Value));

        return value;
    }

    return OmniConverterDouble9();
}

OMNI_ASSET_CONVERTER_EXPORT OmniConverterDouble16 omniConverterToDouble16(OmniConverterMaterialProperty* property)
{
    if (property->valueType == OMNI_CONVERTER_VALUE_TYPE_DOUBLE16)
    {
        OmniConverterDouble16 value;
        memcpy(value.value, property->value.double16Value, sizeof(property->value.double16Value));

        return value;
    }

    return OmniConverterDouble16();
}

OMNI_ASSET_CONVERTER_EXPORT const char* omniConverterToString(OmniConverterMaterialProperty* property)
{
    if (property->valueType == OMNI_CONVERTER_VALUE_TYPE_STRING)
    {
        return property->stringValue.c_str();
    }

    return "";
}
