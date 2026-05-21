// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../../converter_future.h"
#include "../../stage.h"
#include "../../usd_convert_asset_internal.h"
#include "../exporter.h"
#include "assimp/scene.h"

class AssimpExporter : public Exporter
{
public:

    AssimpExporter(const OmniFutureThreadContextPtr& context) : Exporter(context)
    {
    }

    virtual ~AssimpExporter(){};

    virtual OmniConverterStatus Export(const StagePtr& stage, std::string& detailedError);

private:

    OmniConverterStatus ExportTextures(const StagePtr& stage, std::string& detailedError);
    OmniConverterStatus UploadTextureIfNotEmpty(const TextureImagePtr& texture, std::string& detailedError);
    OmniConverterStatus UploadFileInternal(const std::string& filePath, const std::string& targetDir, std::string& detailedError);
    bool HasOnlyTriangularFaces(const MeshPtr& mesh) const;

    aiMaterial* ToAiMaterial(const StagePtr& stage, const MaterialPtr& material);
    aiTexture* ToAiTexture(const TextureImagePtr& texture);
    std::vector<aiMesh*> ToAiMesh(const MeshPtr& mesh);
    std::shared_ptr<aiScene> ToAiScene(const StagePtr& stage, std::string& detailedError);
    void AddDummyMaterialForSTL(aiScene* assimpScene);
    void PopulateStageNodeTree(const StagePtr& stage, const StageNodePtr& currentNode, aiNode** parentAssimpNode = nullptr);
    void PreprocessAllNodes(const StagePtr& stage, const StageNodePtr& node);

    OmniConverterBlobPtr ReadTextureData(const TextureImagePtr& texture);

    std::string mMaterialsExportRoot;
    std::string mTexturesExportRoot;

    // mUploadedFiles will be used to check if corresponding texture
    // path has already been handled.
    // mTextureUploadPath will be used to save the map between original
    // texture path and finally target path or it's the reference index
    // if it's to export embedding textures.
    // mUniqueTexturesByPath will save textures that is unique by path, which
    // will be used to export to AiScene::mTextures if it's to export embedding
    // textures.
    std::unordered_map<std::string, std::string> mUploadedFiles;
    std::unordered_map<TextureImagePtr, std::string> mTextureUploadPath;
    std::vector<TextureImagePtr> mUniqueInMemoryTexturesByPath;

    // If this stage includes valid props like meshes, cameras, and lights
    std::unordered_map<StageNodePtr, bool> mStageNodeInfos;

    // Assimp does not support subset, so if a mesh has more than one subset,
    // it will be splitted into several parts. This map is used to record
    // all the meshes that's splitted from original mesh.
    std::unordered_map<MeshPtr, std::vector<size_t>> mMeshIndices;
};
