// SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "importer.h"

#include "../utils/image_utils.h"
#include "assimp/assimp_importer.h"
#include "gltf/gltf_importer.h"
#include "obj/obj_importer.h"
#include "usd/usd_importer.h"

#ifndef __aarch64__
#    include "fbx/fbx_importer.h"
#endif

std::shared_ptr<Importer> CreateImporter(const OmniConverterContext& context)
{
    std::shared_ptr<Importer> importer;
    if (context.GetImportAssetType() == AssetType::OBJ)
    {
        importer = std::make_shared<ObjImporter>();
    }
    else if (context.IsImportAssetUsdcOrUsdaOrUsdz())
    {
        importer = std::make_shared<UsdSdkImporter>();
    }
    else if (context.IsImportAssetGltfOrGlb())
    {
        importer = std::make_shared<GltfImporter>();
    }
#ifndef __aarch64__
    else if (context.GetImportAssetType() == AssetType::FBX)
    {
        importer = std::make_shared<FbxSdkImporter>();
    }
#endif
    else
    {
        importer = std::make_shared<AssimpImporter>();
    }

    if (importer)
    {
        importer->SetCurveSubdivisionNumber(context.GetCurveSubdivisionNumber());
    }

    return importer;
}

bool Importer::HasAlphaChannel(const OmniFutureThreadContextPtr& context, const StagePtr& stage, size_t imageIndex) const
{
    if (imageIndex >= stage->images.size())
    {
        return false;
    }

    TextureImagePtr texture = stage->images[imageIndex];
    if (texture->blob)
    {
        auto& blob = texture->blob;
        return ImageUtils::HasAlphaChannel((uint8_t*)blob->buffer, blob->size);
    }
    else
    {
        texture->blob = context->converterContext.ReadFile(texture->realPath);
        if (!texture->blob)
        {
            return false;
        }

        return ImageUtils::HasAlphaChannel((uint8_t*)texture->blob->buffer, texture->blob->size);
    }
}
