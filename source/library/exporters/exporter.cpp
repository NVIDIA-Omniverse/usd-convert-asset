// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "exporter.h"

#include "../utils/utils.h"
#include "assimp/assimp_exporter.h"
#include "gltf/gltf_exporter.h"
#include "usd/usd_exporter.h"

#ifndef __aarch64__
#    include "fbx/fbx_exporter.h"
#endif

void Exporter::Log(const std::string& message)
{
    mExportContext->converterContext.Log(message);
}

bool Exporter::UploadContent(const std::string& path, OmniConverterBlob* blob)
{
    return mExportContext->converterContext.WriteBinary(path, blob);
}

size_t Exporter::GetTotalExportSteps(const StagePtr& stage) const
{
    size_t numEmbededTextures = 0;
    for (size_t i = 0; i < stage->materials.size(); i++)
    {
        auto material = stage->materials[i];
        if (!material->fallback)
        {
            for (const auto& property : material->inputProperties)
            {
                if (property.isTextureProperty)
                {
                    numEmbededTextures += 1;
                }
            }
        }
        else
        {
            numEmbededTextures += material->GetValidTexturesCount();
        }
    }


    uint32_t numMesheSteps = stage->meshes.size();
    uint32_t numMaterialSteps = mExportContext->converterContext.IgnoreMaterials() ? 0 :
                                                                                     stage->materials.size() +
                                                                                         numEmbededTextures
        ;
    uint32_t numAnimationSteps = mExportContext->converterContext.IgnoreAnimations() ? 0 : stage->animationTracks.size();
    uint32_t totalSteps = numMesheSteps + numMaterialSteps + numAnimationSteps + 1;

    return totalSteps;
}

std::shared_ptr<Exporter> CreateExporter(const OmniFutureThreadContextPtr& context)
{
    std::shared_ptr<Exporter> exporter;
    if (context->converterContext.IsOutputAssetUsdcOrUsdaOrUsdz())
    {
        exporter = std::make_shared<UsdExporter>(context);
    }
    else if (context->converterContext.IsOutputAssetGltfOrGlb())
    {
        exporter = std::make_shared<GltfExporter>(context);
    }
#ifndef __aarch64__
    else if (context->converterContext.GetOutputAssetType() == AssetType::FBX)
    {
        exporter = std::make_shared<FbxSdkExporter>(context);
    }
#endif
    else
    {
        exporter = std::make_shared<AssimpExporter>(context);
    }

    return exporter;
}
