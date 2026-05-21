// SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "file_format.h"

#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#if PXR_MINOR_VERSION < 25 || (PXR_MINOR_VERSION == 25 && PXR_PATCH_VERSION < 11)
#    include <pxr/usd/usd/usdaFileFormat.h>
#else
#    include <pxr/usd/sdf/usdaFileFormat.h>
#endif
#include <pxr/usd/usdGeom/metrics.h>

#include <iostream>
#include <usd_convert_asset.h>

PXR_NAMESPACE_OPEN_SCOPE

// Helper macro for file format token namespace change in OpenUSD 25.11
#if PXR_MINOR_VERSION >= 25 && PXR_PATCH_VERSION >= 11
#    define USDA_FILE_FORMAT_TOKENS SdfUsdaFileFormatTokens
#else
#    define USDA_FILE_FORMAT_TOKENS UsdUsdaFileFormatTokens
#endif

using std::string;

TF_DEFINE_PUBLIC_TOKENS(OmniAssetFileFormatTokens, OMNIASSET_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(OmniAssetFileFormat, SdfFileFormat);
}

static bool stringToBool(std::string str)
{
    str = PXR_NS::TfStringToLower(str);
    return str == "1" || str == "true";
}

OmniAssetFileFormat::OmniAssetFileFormat()
    : SdfFileFormat(
          OmniAssetFileFormatTokens->Id,
          OmniAssetFileFormatTokens->Version,
          OmniAssetFileFormatTokens->Target,
          { "fbx", "FBX", "obj", "OBJ", "gltf", "glb", "md5", "lxo" }
      ),
      _usda(SdfFileFormat::FindById(USDA_FILE_FORMAT_TOKENS->Id))
{
}

OmniAssetFileFormat::~OmniAssetFileFormat()
{
}

bool OmniAssetFileFormat::CanRead(const string& filePath) const
{
    // XXX: Add more verification of file header magic
    auto extension = TfGetExtension(filePath);
    if (extension.empty())
    {
        return false;
    }

    return extension == this->GetFormatId();
}

bool OmniAssetFileFormat::Read(SdfLayer* layer, const string& resolvedPath, bool metadataOnly) const
{

    auto tempLayer = PXR_NS::SdfLayer::CreateAnonymous();
    int flags = OMNI_CONVERTER_FLAGS_CREATE_WORLD_AS_DEFAULT_PRIM;
    std::string layerPath;
    PXR_NS::SdfLayer::FileFormatArguments arguments;
    PXR_NS::SdfLayer::SplitIdentifier(layer->GetIdentifier(), &layerPath, &arguments);
    auto iter = arguments.find("USE_METER_PER_UNIT");
    if (iter != arguments.end() && stringToBool(iter->second))
    {
        flags |= OMNI_CONVERTER_FLAGS_USE_METER_PER_UNIT;
    }
    else
    {
        iter = arguments.find("USE_CENTIMETER_PER_UNIT");
        if (iter == arguments.end() || !stringToBool(iter->second))
        {
            flags |= OMNI_CONVERTER_FLAGS_KEEP_WORLD_UNITS;
        }
    }

    const std::string& assetPath = layer->GetRepositoryPath().empty() ? resolvedPath : layer->GetRepositoryPath();
    auto future = omniConverterCreateAsset(assetPath.c_str(), tempLayer->GetIdentifier().c_str(), flags);
    OmniConverterStatus status = OmniConverterStatus::OK;
    while (true)
    {
        status = omniConverterCheckFutureStatus(future);
        if (status != OmniConverterStatus::IN_PROGRESS)
        {
            break;
        }
    }

    omniConverterReleaseFuture(future);

    if (status == OmniConverterStatus::OK)
    {
        layer->TransferContent(tempLayer);

        return true;
    }

    return false;
}

bool OmniAssetFileFormat::ReadFromString(SdfLayer* layer, const std::string& str) const
{
    // XXX: For now, defer to the usda file format for this. May need to
    //      revisit this as the alembic reader gets fully fleshed out.
    return _usda->ReadFromString(layer, str);
}

bool OmniAssetFileFormat::WriteToString(const SdfLayer& layer, std::string* str, const std::string& comment) const
{
    // XXX: For now, defer to the usda file format for this.  We don't support
    // writing Usd content as other formats at this moment.
    return SdfFileFormat::FindById(USDA_FILE_FORMAT_TOKENS->Id)->WriteToString(layer, str, comment);
}

bool OmniAssetFileFormat::WriteToStream(const SdfSpecHandle& spec, std::ostream& out, size_t indent) const
{
    // XXX: For now, defer to the usda file format for this.  We don't support
    // writing Usd content as other formats at this moment.
    return SdfFileFormat::FindById(USDA_FILE_FORMAT_TOKENS->Id)->WriteToStream(spec, out, indent);
}

PXR_NAMESPACE_CLOSE_SCOPE
