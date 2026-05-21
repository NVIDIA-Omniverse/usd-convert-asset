// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../../thirdparty/tiny_obj_loader.h"
#include "../importer.h"


class ObjImporter : public Importer
{
public:

    virtual std::string ComputeHash(const OmniFutureThreadContextPtr& context);
    virtual StagePtr ImportStage(const OmniFutureThreadContextPtr& context, OmniConverterStatus& status, std::string& detailedError) final;

private:

    void Log(const std::string& message);
    MaterialPtr ToMaterial(const StagePtr& stage, const tinyobj::material_t& objMaterial);
    void NormalizeMaterialPaths(std::vector<tinyobj::material_t>& materials);
    std::string ToAbsolutePath(const std::string& path);
    void FillTextureReference(
        const StagePtr& stage,
        const MaterialPtr& material,
        MaterialTextureType textureType,
        const std::string& path,
        const tinyobj::texture_option_t& textureOption
    );

    OmniFutureThreadContextPtr mThreadContext;
    std::unordered_map<std::string, size_t> mTextureIndex;
};
