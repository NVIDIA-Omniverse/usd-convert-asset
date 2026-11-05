// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "assimp_exporter.h"

#include "../../common/common.h"
#include "../../utils/stage_utils.h"
#include "../../utils/utils.h"
#include "assimp/cexport.h"
#include "assimp/cfileio.h"
#include "assimp/cimport.h"
#include "assimp/pbrmaterial.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "assimp_exporter.h"

#include <pxr/usd/usdSkel/utils.h>

#include <algorithm>
#include <numeric>
#include <unordered_set>

#define ENSURE_STATUS_OK(status)                                                                                                                     \
    {                                                                                                                                                \
        auto res = status;                                                                                                                           \
        if (res != OmniConverterStatus::OK)                                                                                                          \
        {                                                                                                                                            \
            return res;                                                                                                                              \
        }                                                                                                                                            \
    }

const static std::string MATERIAL_DIR_NAME = "materials";
const static std::string TEXTURE_DIR_NAME = "textures";

// Bake the rest pose per vertex: a child joint can cancel its root's movement,
// and blended vertices need contributions from every influencing joint.
static MeshPtr BakeSkinRestPose(const MeshPtr& source, const SkinMeshPtr& skin)
{
    auto mesh = std::make_shared<Mesh>(*source);
    std::vector<PXR_NS::GfMatrix4d> transforms;
    std::vector<PXR_NS::GfMatrix3d> normalTransforms;
    for (const auto& bone : skin->influencedBoneNodes)
    {
        if (!bone)
        {
            return nullptr;
        }
        auto rest = bone->restTransform;
        auto parent = bone->parent.lock();
        while (parent && parent->isBoneNode)
        {
            rest *= parent->restTransform;
            parent = parent->parent.lock();
        }
        if (parent)
        {
            rest *= parent->worldTransformMatrix;
        }
        const auto transform = bone->bindTransform.GetInverse() * rest;
        transforms.push_back(transform);
        normalTransforms.push_back(transform.ExtractRotationMatrix().GetInverse().GetTranspose());
    }
    const auto normalBind = skin->geomBindTransform.ExtractRotationMatrix().GetInverse().GetTranspose();
    if (transforms.empty() || skin->numBoneInfluencesPerVertex == 0)
    {
        // Unbound skin data still has a useful geometry placement.
        for (auto& point : mesh->points)
        {
            point = PXR_NS::GfVec3f(skin->geomBindTransform.Transform(point));
        }
        for (auto& normal : mesh->normals)
        {
            normal = PXR_NS::GfVec3f((normal * normalBind).GetNormalized());
        }
        return mesh;
    }
    if (!PXR_NS::UsdSkelSkinPointsLBS(
            skin->geomBindTransform,
            transforms,
            skin->jointInfluences,
            skin->jointWeights,
            skin->numBoneInfluencesPerVertex,
            mesh->points
        ))
    {
        return nullptr;
    }
    if (!mesh->normals.empty())
    {
        const bool faceVarying = mesh->hasFaceVaryingNormals || mesh->normals.size() != mesh->points.size();
        const bool success = faceVarying ? PXR_NS::UsdSkelSkinFaceVaryingNormalsLBS(
                                               normalBind,
                                               normalTransforms,
                                               skin->jointInfluences,
                                               skin->jointWeights,
                                               skin->numBoneInfluencesPerVertex,
                                               mesh->faceVertexIndices,
                                               mesh->normals
                                           ) :
                                           PXR_NS::UsdSkelSkinNormalsLBS(
                                               normalBind,
                                               normalTransforms,
                                               skin->jointInfluences,
                                               skin->jointWeights,
                                               skin->numBoneInfluencesPerVertex,
                                               mesh->normals
                                           );
        if (!success)
        {
            return nullptr;
        }
    }
    return mesh;
}

static aiVector3D GfVec3fToAiVector3D(const PXR_NS::GfVec3f& vec3)
{
    return aiVector3D(vec3[0], vec3[1], vec3[2]);
}

static aiVector3D GfVec2fToAiVector3D(const PXR_NS::GfVec2f& vec2)
{
    return aiVector3D(vec2[0], vec2[1], 1.0f);
}

static aiColor4D GfVec3fToAiColor4D(const PXR_NS::GfVec3f& vec3)
{
    return aiColor4D(vec3[0], vec3[1], vec3[2], 1.0f);
}

static aiColor3D GfVec3fToAiColor3D(const PXR_NS::GfVec3f& vec3)
{
    return aiColor3D(vec3[0], vec3[1], vec3[2]);
}

static aiMatrix4x4 GfMatrixToAiMatrix(const PXR_NS::GfMatrix4d& matrix)
{
    return aiMatrix4x4(
        matrix[0][0],
        matrix[1][0],
        matrix[2][0],
        matrix[3][0],
        matrix[0][1],
        matrix[1][1],
        matrix[2][1],
        matrix[3][1],
        matrix[0][2],
        matrix[1][2],
        matrix[2][2],
        matrix[3][2],
        matrix[0][3],
        matrix[1][3],
        matrix[2][3],
        matrix[3][3]
    );
}

void ReleaseNodeTree(aiNode* rootNode)
{
    if (rootNode == nullptr)
    {
        return;
    }

    for (size_t i = 0; i < rootNode->mNumChildren; i++)
    {
        ReleaseNodeTree(rootNode->mChildren[i]);
        rootNode->mChildren[i] = nullptr;
    }
    delete[] rootNode->mMeshes;
    rootNode->mMeshes = nullptr;
    delete rootNode;
}

static void ReleaseAiScene(aiScene* scene)
{
    if (scene->mNumMaterials > 0)
    {
        for (size_t i = 0; i < scene->mNumMaterials; i++)
        {
            delete scene->mMaterials[i];
        }

        delete[] scene->mMaterials;
        scene->mMaterials = nullptr;
        scene->mNumMaterials = 0;
    }

    if (scene->mNumCameras > 0)
    {
        for (size_t i = 0; i < scene->mNumCameras; i++)
        {
            delete scene->mCameras[i];
        }

        delete[] scene->mCameras;
        scene->mCameras = nullptr;
        scene->mNumCameras = 0;
    }

    if (scene->mNumTextures > 0)
    {
        for (size_t i = 0; i < scene->mNumTextures; i++)
        {
            delete scene->mTextures[i];
        }

        delete[] scene->mTextures;
        scene->mTextures = nullptr;
        scene->mNumTextures = 0;
    }

    if (scene->mNumLights > 0)
    {
        for (size_t i = 0; i < scene->mNumLights; i++)
        {
            delete scene->mLights[i];
        }

        delete[] scene->mLights;
        scene->mLights = nullptr;
        scene->mNumLights = 0;
    }

    if (scene->mNumMeshes > 0)
    {
        for (size_t i = 0; i < scene->mNumMeshes; i++)
        {
            delete scene->mMeshes[i];
        }

        delete[] scene->mMeshes;
        scene->mMeshes = nullptr;
        scene->mNumMeshes = 0;
    }
    ReleaseNodeTree(scene->mRootNode);
    scene->mRootNode = nullptr;
}

OmniConverterStatus AssimpExporter::Export(const StagePtr& stage, std::string& detailedError)
{
    Log("Starting to Export asset with Assimp exporter.");

    auto status = OmniConverterStatus::OK;
    const std::string& outputAssetPath = mExportContext->converterContext.GetOutputAssetPath();
    const std::string& basePath = mExportContext->converterContext.GetOutputAssetDir();
    const std::string& fileName = mExportContext->converterContext.GetOutputAssetFileName();
    const std::string& extension = PathUtils::GetExtension(outputAssetPath);
    if (extension.empty())
    {
        detailedError = "Failed to export asset that has unknown extension asset:" + mExportContext->converterContext.GetOutputAssetPath() + ".";
        return OmniConverterStatus::UNSUPPORTED_EXPORT_FORMAT;
    }

    // Prepare export dirs and options
    if (mExportContext->converterContext.GetOutputAssetType() == AssetType::OBJ)
    {
        // Put all materials and textures to the same folder as obj file
        // as a lot asset viewers cannot show textures in other folders evenw with
        // correct relative paths.
        mMaterialsExportRoot = basePath;
        mTexturesExportRoot = basePath;
    }
    else
    {
        mMaterialsExportRoot = PathUtils::JoinPaths(basePath, MATERIAL_DIR_NAME);
        if (mExportContext->converterContext.ExportPreviewSurface())
        {
            mTexturesExportRoot = PathUtils::JoinPaths(basePath, TEXTURE_DIR_NAME);
        }
        else
        {
            mTexturesExportRoot = PathUtils::JoinPaths(mMaterialsExportRoot, TEXTURE_DIR_NAME);
        }
    }

    uint32_t totalSteps = GetTotalExportSteps(stage);
    mExportContext->StartProgress(totalSteps);

    ENSURE_STATUS_OK(ExportTextures(stage, detailedError));
    if (!stage->skinMeshes.empty())
    {
        Log("WARNING: Assimp export preserves skinned geometry in its rest pose, but does not export skin bindings.");
    }
    if (!mExportContext->converterContext.IgnoreAnimations() && !stage->animationTracks.empty())
    {
        Log("WARNING: Assimp export does not preserve animation; exporting static geometry.");
    }
    PreprocessAllNodes(stage, stage->rootNode);
    auto assimpScene = ToAiScene(stage, detailedError);
    if (!assimpScene)
    {
        if (mExportContext->IsExited())
        {
            return OmniConverterStatus::CANCELLED;
        }
        else
        {
            // Validation failed (e.g., STL with no triangular meshes)
            return OmniConverterStatus::UNSUPPORTED_EXPORT_FORMAT;
        }
    }

    struct AssimpFileIOContext
    {
        const OmniConverterContext* context;
        OmniConverterStatus* status;
        std::unordered_map<std::string, OmniConverterBlobPtr> cachedBlobs;
    };

    struct AssimpFile
    {
        std::string fileName;
        size_t currentPos = 0;
        AssimpFileIOContext* context = nullptr;
    };

    // Assimp io override to read file with customized IO
    static auto assimpFileWriteProc = [](aiFile* file, const char* buffer, size_t pSize, size_t count)
    {
        size_t dataSize = pSize * count;
        AssimpFile* assimpFile = (AssimpFile*)file->UserData;
        auto fileBlob = assimpFile->context->cachedBlobs[assimpFile->fileName];
        size_t oldSize = 0;
        if (fileBlob)
        {
            oldSize = fileBlob->size;
        }

        if (dataSize > 0 && (oldSize < assimpFile->currentPos || oldSize - assimpFile->currentPos < dataSize))
        {
            size_t newSize = assimpFile->currentPos + dataSize;
            uint8_t* newData = new uint8_t[newSize];
            if (oldSize > 0)
            {
                memcpy(newData, fileBlob->buffer, oldSize);
            }

            fileBlob = createOmniConverterBlob(newData, newSize);
        }

        memcpy((char*)fileBlob->buffer + assimpFile->currentPos, buffer, dataSize);
        assimpFile->currentPos += dataSize;
        assimpFile->context->cachedBlobs[assimpFile->fileName] = fileBlob;

        return count;
    };
    static auto assimpFileFlushProc = [](aiFile* file)
    {
    };

    static auto assimpFileTellProc = [](aiFile* file)
    {
        AssimpFile* assimpFile = (AssimpFile*)file->UserData;
        return assimpFile->currentPos;
    };

    static auto assimpFileSizeProc = [](aiFile* file)
    {
        AssimpFile* assimpFile = (AssimpFile*)file->UserData;
        auto fileBlob = assimpFile->context->cachedBlobs[assimpFile->fileName];
        size_t fileSize = 0;
        if (fileBlob)
        {
            fileSize = fileBlob->size;
        }

        return fileSize;
    };

    static auto assimpFileSeekProc = [](aiFile* file, size_t pos, aiOrigin origin)
    {
        AssimpFile* assimpFile = (AssimpFile*)file->UserData;
        auto fileBlob = assimpFile->context->cachedBlobs[assimpFile->fileName];
        size_t fileSize = 0;
        if (fileBlob)
        {
            fileSize = fileBlob->size;
        }

        if (aiOrigin_SET == origin)
        {
            assimpFile->currentPos = pos;
        }
        else if (aiOrigin_END == origin)
        {
            assimpFile->currentPos = fileSize - pos;
        }
        else
        {
            assimpFile->currentPos += pos;
        }

        return AI_SUCCESS;
    };

    static auto assimpFileReadProc = [](aiFile* file, char* buffer, size_t elementSize, size_t count) -> size_t
    {
        AssimpFile* assimpFile = (AssimpFile*)file->UserData;
        auto fileBlob = assimpFile->context->cachedBlobs[assimpFile->fileName];
        if (!file || !fileBlob)
        {
            return 0;
        }

        const size_t cnt = std::min(count, (fileBlob->size - assimpFile->currentPos) / elementSize);
        const size_t ofs = elementSize * cnt;

        ::memcpy(buffer, (char*)fileBlob->buffer + assimpFile->currentPos, ofs);
        assimpFile->currentPos += ofs;

        return cnt;
    };

    static auto assimpFileOpenProc = [](aiFileIO* fileIO, const char* file, const char* flags) -> aiFile*
    {
        AssimpFileIOContext* assimpContext = (AssimpFileIOContext*)fileIO->UserData;
        // It's possible that the same file will be opened multiple times.
        auto iter = assimpContext->cachedBlobs.find(file);
        if (iter != assimpContext->cachedBlobs.end())
        {
            assimpContext->cachedBlobs.insert({ file, createOmniConverterBlob(nullptr, 0) });
        }

        AssimpFile* assimpFile = new AssimpFile;
        assimpFile->fileName = file;
        assimpFile->context = assimpContext;

        aiFile* fileWrapper = new aiFile;
        fileWrapper->ReadProc = assimpFileReadProc;
        fileWrapper->WriteProc = assimpFileWriteProc;
        fileWrapper->FileSizeProc = assimpFileSizeProc;
        fileWrapper->TellProc = assimpFileTellProc;
        fileWrapper->FlushProc = assimpFileFlushProc;
        fileWrapper->SeekProc = assimpFileSeekProc;
        fileWrapper->UserData = (aiUserData)assimpFile;

        return fileWrapper;
    };

    static auto assimpFileCloseProc = [](aiFileIO* fileIO, aiFile* file)
    {
        AssimpFile* assimpFile = (AssimpFile*)file->UserData;
        delete assimpFile;
        delete file;
    };

    AssimpFileIOContext assimpFileIOContext;
    assimpFileIOContext.context = &mExportContext->converterContext;
    assimpFileIOContext.status = &status;

    aiFileIO fileIO;
    fileIO.OpenProc = assimpFileOpenProc;
    fileIO.CloseProc = assimpFileCloseProc;
    fileIO.UserData = (aiUserData)(&assimpFileIOContext);

    std::string formatId = StringUtils::ToLower(extension);
    if (formatId == "obj" && (mExportContext->converterContext.IgnoreMaterials() || assimpScene->mNumMaterials == 0))
    {
        formatId = "objnomtl";
    }
    else if (formatId == "stl")
    {
        formatId = "stl";
    }

    static auto aiLogFunc = [](const char* message, char* userData)
    {
        OmniConverterContext* context = (OmniConverterContext*)userData;
        context->Log(message);
    };

    struct aiLogStream logStream;
    logStream.callback = aiLogFunc;
    logStream.user = (char*)&mExportContext->converterContext;
    aiAttachLogStream(&logStream);

    aiReturn
        returnCode = aiExportSceneEx(assimpScene.get(), formatId.c_str(), mExportContext->converterContext.GetOutputAssetPath().c_str(), &fileIO, 0);

    aiDetachLogStream(&logStream);

    if (returnCode == aiReturn_FAILURE)
    {
        detailedError = "Failed to export unsupported format with assimp: " + mExportContext->converterContext.GetOutputAssetPath();
        return OmniConverterStatus::UNSUPPORTED_EXPORT_FORMAT;
    }

    for (auto blobs : assimpFileIOContext.cachedBlobs)
    {
        if (!UploadContent(blobs.first, blobs.second.get()))
        {
            detailedError = (formatId == "fbx" ? "Failed to write FBX file " : "Failed to write file ") + blobs.first;
            return OmniConverterStatus::FILE_WRITE_ERROR;
        }
    }

    return OmniConverterStatus::OK;
}

OmniConverterStatus AssimpExporter::ExportTextures(const StagePtr& stage, std::string& detailedError)
{
    Log("Starting to export textures...");
    static auto ClearInMemoryTexture = [](TextureImagePtr& texture)
    {
        if (texture)
        {
            texture->blob = nullptr;
        }
    };

    for (size_t i = 0; i < stage->images.size(); i++)
    {
        if (mExportContext->IsExited())
        {
            return OmniConverterStatus::CANCELLED;
        }

        TextureImagePtr texture = stage->images[i];
        ENSURE_STATUS_OK(UploadTextureIfNotEmpty(texture, detailedError));
        ClearInMemoryTexture(texture);
        mExportContext->IncrementProgress();
    }

    return OmniConverterStatus::OK;
}

OmniConverterStatus AssimpExporter::UploadTextureIfNotEmpty(const TextureImagePtr& texture, std::string& detailedError)
{
    if (texture->blob)
    {
        const std::string& filename = PathUtils::GetFileName(texture->originalPath, true);
        const std::string& targetPath = PathUtils::JoinPaths(mTexturesExportRoot, filename);
        if (!UploadContent(targetPath, texture->blob.get()))
        {
            detailedError = "Failed to write texture file " + targetPath + ".";
            Log(detailedError);
            return OmniConverterStatus::FILE_WRITE_ERROR;
        }
        mUploadedFiles[texture->originalPath] = targetPath;
        mTextureUploadPath[texture] = targetPath;
        mExportContext->IncrementProgress();
    }
    else
    {
        ENSURE_STATUS_OK(UploadFileInternal(texture->realPath, mTexturesExportRoot, detailedError));
        mTextureUploadPath[texture] = mUploadedFiles[texture->realPath];
    }

    return OmniConverterStatus::OK;
}

OmniConverterStatus AssimpExporter::UploadFileInternal(const std::string& filePath, const std::string& targetDir, std::string& detailedError)
{
    auto iter = mUploadedFiles.find(filePath);
    if (iter == mUploadedFiles.end())
    {
        const std::string& fileName = PathUtils::GetFileName(filePath, true);
        const std::string& outFilePath = PathUtils::JoinPaths(targetDir, fileName);
        if (!mExportContext->converterContext.CopyFile(outFilePath, filePath))
        {
            detailedError = "Failed to copy file " + filePath + " to " + outFilePath + ".";
            Log(detailedError);
            return OmniConverterStatus::FILE_WRITE_ERROR;
        }
        mUploadedFiles[filePath] = outFilePath;
    }
    mExportContext->IncrementProgress();

    return OmniConverterStatus::OK;
}

bool AssimpExporter::HasOnlyTriangularFaces(const MeshPtr& mesh) const
{
    for (size_t i = 0; i < mesh->faceVertexCounts.size(); i++)
    {
        if (mesh->faceVertexCounts[i] != 3)
        {
            return false;
        }
    }
    return true;
}

aiMaterial* AssimpExporter::ToAiMaterial(const StagePtr& stage, const MaterialPtr& material)
{
    auto ToRelativePath = [this, &stage](const size_t imageIndex)
    {
        TextureImagePtr texture = stage->images[imageIndex];

        std::string relativePath;
        const std::string& exportAssetPath = mExportContext->converterContext.GetOutputAssetPath();
        PathUtils::ComputeRelativePath(mTextureUploadPath[texture], exportAssetPath, relativePath);
        aiString path;
        path.Set(relativePath);

        return path;
    };

    aiMaterial* assimpMaterial = new aiMaterial;
    aiString name;
    name.Set(material->name);
    assimpMaterial->AddProperty(&name, AI_MATKEY_NAME);
    const auto& diffuse = material->GetTextureReference(MaterialTextureType::DIFFUSE);
    if (diffuse.IsValid())
    {
        aiString path = ToRelativePath(diffuse.imageIndex);
        assimpMaterial->AddProperty(&path, AI_MATKEY_TEXTURE_DIFFUSE(0));
    }

    if (material->hasDiffuseColor)
    {
        const auto& color = GfVec3fToAiColor3D(material->diffuseColor);
        assimpMaterial->AddProperty<aiColor3D>(&color, 1, AI_MATKEY_COLOR_DIFFUSE);
    }

    const auto& emissive = material->GetTextureReference(MaterialTextureType::EMISSIVE);
    if (emissive.IsValid())
    {
        aiString path = ToRelativePath(emissive.imageIndex);
        assimpMaterial->AddProperty(&path, AI_MATKEY_TEXTURE_EMISSIVE(0));
    }

    if (material->hasEmissiveColor)
    {
        const auto& color = GfVec3fToAiColor3D(material->emissiveColor);
        assimpMaterial->AddProperty<aiColor3D>(&color, 1, AI_MATKEY_COLOR_EMISSIVE);
    }

    const auto& occlusion = material->GetTextureReference(MaterialTextureType::OCCLUSION);
    if (occlusion.IsValid())
    {
        aiString path = ToRelativePath(occlusion.imageIndex);
        assimpMaterial->AddProperty(&path, AI_MATKEY_TEXTURE_LIGHTMAP(0));
    }

    const auto& opacity = material->GetTextureReference(MaterialTextureType::OPACITY);
    if (opacity.IsValid())
    {
        aiString path = ToRelativePath(opacity.imageIndex);
        assimpMaterial->AddProperty(&path, AI_MATKEY_TEXTURE_OPACITY(0));
    }

    if (material->hasOpacity)
    {
        assimpMaterial->AddProperty(&material->opacity, 1, AI_MATKEY_OPACITY);
    }

    const auto& normal = material->GetTextureReference(MaterialTextureType::NORMAL);
    if (normal.IsValid())
    {
        aiString path = ToRelativePath(normal.imageIndex);
        assimpMaterial->AddProperty(&path, AI_MATKEY_TEXTURE_NORMALS(0));
    }

    if (material->hasSpecularColor)
    {
        const auto& color = GfVec3fToAiColor3D(material->specularColor);
        assimpMaterial->AddProperty<aiColor3D>(&color, 1, AI_MATKEY_COLOR_SPECULAR);
    }

    if (material->hasGlossyFactor)
    {
        assimpMaterial->AddProperty(&material->glossyFactor, 1, AI_MATKEY_GLOSSINESS_FACTOR);
    }

    if (material->hasRoughnessFactor)
    {
        assimpMaterial->AddProperty(&material->roughnessFactor, 1, AI_MATKEY_ROUGHNESS_FACTOR);
    }

    if (material->hasMetallicFactor)
    {
        assimpMaterial->AddProperty(&material->metallicFactor, 1, AI_MATKEY_METALLIC_FACTOR);
    }

    if (material->hasClearCoatFactor)
    {
        assimpMaterial->AddProperty(&material->clearCoatFactor, 1, AI_MATKEY_CLEARCOAT_FACTOR);
    }

    if (material->hasClearCoatRoughnessFactor)
    {
        assimpMaterial->AddProperty(&material->clearCoatRoughnessFactor, 1, AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR);
    }

    if (material->hasSheenColor)
    {
        const auto& color = GfVec3fToAiColor3D(material->sheenColor);
        assimpMaterial->AddProperty<aiColor3D>(&color, 1, AI_MATKEY_SHEEN_COLOR_FACTOR);
    }

    if (material->hasSheenRoughnessFactor)
    {
        assimpMaterial->AddProperty(&material->sheenRoughnessFactor, 1, AI_MATKEY_SHEEN_ROUGHNESS_FACTOR);
    }

    if (material->hasTransmissionFactor)
    {
        assimpMaterial->AddProperty(&material->transmissionFactor, 1, AI_MATKEY_TRANSMISSION_FACTOR);
    }

    const auto& specular = material->GetTextureReference(MaterialTextureType::SPECULAR);
    if (specular.IsValid())
    {
        aiString path = ToRelativePath(specular.imageIndex);
        assimpMaterial->AddProperty(&path, AI_MATKEY_TEXTURE_SPECULAR(0));
    }

    const auto& clearCoat = material->GetTextureReference(MaterialTextureType::CLEARCOAT);
    if (clearCoat.IsValid())
    {
        aiString path = ToRelativePath(clearCoat.imageIndex);
        assimpMaterial->AddProperty(&path, AI_MATKEY_TEXTURE(aiTextureType_CLEARCOAT, 0));
    }

    const auto& clearCoatRoughness = material->GetTextureReference(MaterialTextureType::CLEARCOAT_ROUGHNESS);
    if (clearCoatRoughness.IsValid())
    {
        aiString path = ToRelativePath(clearCoatRoughness.imageIndex);
        assimpMaterial->AddProperty(&path, AI_MATKEY_TEXTURE(aiTextureType_CLEARCOAT, 1));
    }

    const auto& clearCoatNormal = material->GetTextureReference(MaterialTextureType::CLEARCOAT_NORMAL);
    if (clearCoatNormal.IsValid())
    {
        aiString path = ToRelativePath(clearCoatNormal.imageIndex);
        assimpMaterial->AddProperty(&path, AI_MATKEY_TEXTURE(aiTextureType_CLEARCOAT, 2));
    }

    const auto& sheen = material->GetTextureReference(MaterialTextureType::SHEEN);
    if (sheen.IsValid())
    {
        aiString path = ToRelativePath(sheen.imageIndex);
        assimpMaterial->AddProperty(&path, AI_MATKEY_TEXTURE(aiTextureType_SHEEN, 0));
    }

    const auto& sheenRoughness = material->GetTextureReference(MaterialTextureType::SHEEN_ROUGHNESS);
    if (sheenRoughness.IsValid())
    {
        aiString path = ToRelativePath(sheenRoughness.imageIndex);
        assimpMaterial->AddProperty(&path, AI_MATKEY_TEXTURE(aiTextureType_SHEEN, 1));
    }

    const auto& transmission = material->GetTextureReference(MaterialTextureType::TRANSMISSION);
    if (transmission.IsValid())
    {
        aiString path = ToRelativePath(transmission.imageIndex);
        assimpMaterial->AddProperty(&path, AI_MATKEY_TEXTURE(aiTextureType_TRANSMISSION, 0));
    }

    return assimpMaterial;
}

aiTexture* AssimpExporter::ToAiTexture(const TextureImagePtr& texture)
{
    aiTexture* assimpTexture = new aiTexture;
    std::string extension = PathUtils::GetExtension(texture->realPath);
    if (extension == "jpeg")
    {
        extension = "jpg";
    }

    const size_t formatHintSize = sizeof(assimpTexture->achFormatHint);
    const size_t formatHintLength = std::min(extension.size(), formatHintSize - 1);
    std::fill_n(assimpTexture->achFormatHint, formatHintSize, '\0');
    std::copy_n(extension.c_str(), formatHintLength, assimpTexture->achFormatHint);
    assimpTexture->mFilename = PathUtils::GetFileName(texture->realPath, true);
    if (texture->blob)
    {
        assimpTexture->mHeight = 0;
        assimpTexture->mWidth = texture->blob->size;
        assimpTexture->pcData = (aiTexel*)new uint8_t[assimpTexture->mWidth];
        std::memcpy(assimpTexture->pcData, texture->blob->buffer, texture->blob->size);
    }

    return assimpTexture;
}

std::vector<aiMesh*> AssimpExporter::ToAiMesh(const MeshPtr& mesh)
{
    std::vector<aiMesh*> meshes;
    const bool hasFaceVaryingNormals = mesh->hasFaceVaryingNormals || (!mesh->normals.empty() && mesh->normals.size() != mesh->points.size());
    for (const auto& subset : mesh->meshSubsets)
    {
        aiMesh* assimpMesh = new aiMesh;
        assimpMesh->mName = mesh->name;
        if (subset.materialIndex != INVALID_MATERIAL_INDEX && !mExportContext->converterContext.IgnoreMaterials())
        {
            assimpMesh->mMaterialIndex = subset.materialIndex;
        }

        size_t totalPoints = 0;
        for (size_t i = 0; i < subset.faceIndices.size(); i++)
        {
            size_t faceIndex = subset.faceIndices[i];
            totalPoints += mesh->faceVertexCounts[faceIndex];
        }
        assimpMesh->mNumVertices = totalPoints;
        assimpMesh->mVertices = new aiVector3D[assimpMesh->mNumVertices];
        if (!mesh->normals.empty())
        {
            assimpMesh->mNormals = new aiVector3D[assimpMesh->mNumVertices];
        }
        assimpMesh->mNumFaces = subset.faceIndices.size();
        assimpMesh->mFaces = new aiFace[assimpMesh->mNumFaces];
        size_t numUVSet = 0;
        for (size_t i = 0; i < mesh->uvs.size(); i++)
        {
            if (mesh->uvs[i].size() > 0)
            {
                numUVSet += 1;
            }
        }

        if (numUVSet > AI_MAX_NUMBER_OF_TEXTURECOORDS)
        {
            numUVSet = AI_MAX_NUMBER_OF_TEXTURECOORDS;
        }

        for (size_t i = 0; i < numUVSet; i++)
        {
            assimpMesh->mTextureCoords[i] = new aiVector3D[totalPoints];
            assimpMesh->mNumUVComponents[i] = 2;
        }

        size_t numColors;
        if (mesh->colors.size() < AI_MAX_NUMBER_OF_COLOR_SETS)
        {
            numColors = mesh->colors.size();
        }
        else
        {
            numColors = AI_MAX_NUMBER_OF_COLOR_SETS;
        }
        for (size_t i = 0; i < numColors; i++)
        {
            assimpMesh->mColors[i] = new aiColor4D[totalPoints];
        }

        std::vector<size_t> partialSums;
        std::partial_sum(mesh->faceVertexCounts.begin(), mesh->faceVertexCounts.end(), std::back_inserter(partialSums));
        size_t vertexIndex = 0;
        bool allTriangles = true;
        for (size_t i = 0; i < subset.faceIndices.size(); i++)
        {
            size_t faceIndex = subset.faceIndices[i];
            size_t faceVerticesCount = mesh->faceVertexCounts[faceIndex];
            if (faceVerticesCount != 3)
            {
                allTriangles = false;
            }
            size_t start = partialSums[faceIndex] - faceVerticesCount;
            size_t end = partialSums[faceIndex];
            assimpMesh->mFaces[i].mNumIndices = faceVerticesCount;
            assimpMesh->mFaces[i].mIndices = new unsigned int[faceVerticesCount];
            for (size_t j = start; j < end; j++)
            {
                const auto& faceVertexIndex = mesh->faceVertexIndices[j];
                const auto& point = GfVec3fToAiVector3D(mesh->points[faceVertexIndex]);
                assimpMesh->mVertices[vertexIndex] = point;
                if (!mesh->normals.empty())
                {
                    const size_t normalIndex = hasFaceVaryingNormals ? j : static_cast<size_t>(faceVertexIndex);
                    const auto& normal = GfVec3fToAiVector3D(mesh->normals[normalIndex]);
                    assimpMesh->mNormals[vertexIndex] = normal;
                }
                assimpMesh->mFaces[i].mIndices[j - start] = vertexIndex;
                size_t uvSetCount = 0;
                for (size_t k = 0; k < mesh->uvs.size(); k++)
                {
                    if (uvSetCount >= numUVSet)
                    {
                        break;
                    }

                    if (mesh->uvs[k].empty())
                    {
                        continue;
                    }
                    const auto& uv = GfVec2fToAiVector3D(mesh->uvs[k][j]);
                    assimpMesh->mTextureCoords[k][vertexIndex] = uv;
                    uvSetCount++;
                }
                for (size_t k = 0; k < numColors; k++)
                {
                    const auto& color = GfVec3fToAiColor4D(mesh->colors[k][j]);
                    assimpMesh->mColors[k][vertexIndex] = color;
                }
                vertexIndex++;
            }
        }

        if (allTriangles)
        {
            assimpMesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
        }
        else
        {
            assimpMesh->mPrimitiveTypes = aiPrimitiveType_POLYGON;
        }

        meshes.push_back(assimpMesh);
    }

    return meshes;
}

std::shared_ptr<aiScene> AssimpExporter::ToAiScene(const StagePtr& stage, std::string& detailedError)
{
    auto assimpScene = std::shared_ptr<aiScene>(new aiScene, ReleaseAiScene);

    mMeshIndices.clear();
    mMirroredMeshIndices.clear();
    std::vector<aiMesh*> allMeshes;
    std::vector<std::string> skippedMeshNames;
    bool isSTLExport = mExportContext->converterContext.GetOutputAssetType() == AssetType::STL;

    // Keep a separate baked mesh per binding, without changing shared static geometry.
    std::unordered_set<size_t> skinMeshIndices;
    for (const auto& skin : stage->skinMeshes)
    {
        skinMeshIndices.insert(skin->meshIndex);
    }
    std::vector<StageNodePtr> nodes;
    if (!skinMeshIndices.empty())
    {
        nodes.push_back(stage->rootNode);
    }
    while (!nodes.empty())
    {
        const auto node = nodes.back();
        nodes.pop_back();
        if (node)
        {
            for (const auto index : node->staticMeshInstances)
            {
                skinMeshIndices.erase(index);
            }
            nodes.insert(nodes.end(), node->children.begin(), node->children.end());
        }
    }

    for (size_t meshIndex = 0; meshIndex < stage->meshes.size(); ++meshIndex)
    {
        if (skinMeshIndices.count(meshIndex))
        {
            continue;
        }
        const auto& mesh = stage->meshes[meshIndex];
        if (mExportContext->IsExited())
        {
            return nullptr;
        }

        // STL format only supports triangular meshes - check and skip if needed
        if (isSTLExport && !HasOnlyTriangularFaces(mesh))
        {
            skippedMeshNames.push_back(mesh->name);
            continue;
        }

        const auto& meshes = ToAiMesh(mesh);
        std::copy(meshes.begin(), meshes.end(), std::back_inserter(allMeshes));
        std::vector<size_t> indices(meshes.size());
        std::iota(indices.begin(), indices.end(), allMeshes.size() - meshes.size());
        mMeshIndices[mesh] = indices;
    }

    if (mExportContext->converterContext.GetOutputAssetType() == AssetType::OBJ)
    {
        StageUtils::TraverseStageTree(
            stage->rootNode,
            [&](const StageNodePtr& node)
            {
                if (node->worldTransformMatrix.GetDeterminant() < 0)
                {
                    for (const auto index : node->staticMeshInstances)
                    {
                        const auto& mesh = stage->meshes[index];
                        if (mMirroredMeshIndices.count(mesh))
                        {
                            continue;
                        }
                        auto meshes = ToAiMesh(mesh);
                        auto& indices = mMirroredMeshIndices[mesh];
                        for (auto* mirrored : meshes)
                        {
                            for (unsigned face = 0; face < mirrored->mNumFaces; ++face)
                            {
                                auto& polygon = mirrored->mFaces[face];
                                std::reverse(polygon.mIndices, polygon.mIndices + polygon.mNumIndices);
                            }
                            indices.push_back(allMeshes.size());
                            allMeshes.push_back(mirrored);
                        }
                    }
                }
                return true;
            }
        );
    }

    std::vector<std::vector<size_t>> skinIndices;
    for (const auto& skin : stage->skinMeshes)
    {
        if (mExportContext->IsExited())
        {
            for (auto* mesh : allMeshes)
            {
                delete mesh;
            }
            return nullptr;
        }
        skinIndices.emplace_back();
        const auto& source = stage->meshes[skin->meshIndex];
        if (isSTLExport && !HasOnlyTriangularFaces(source))
        {
            skippedMeshNames.push_back(source->name);
            continue;
        }
        const auto mesh = BakeSkinRestPose(source, skin);
        if (!mesh)
        {
            Log("WARNING: Skipping skinned mesh with invalid joint influences: " + source->name);
            continue;
        }
        const auto meshes = ToAiMesh(mesh);
        auto& indices = skinIndices.back();
        indices.resize(meshes.size());
        std::iota(indices.begin(), indices.end(), allMeshes.size());
        allMeshes.insert(allMeshes.end(), meshes.begin(), meshes.end());
    }

    // Log warnings and handle errors for STL export validation
    if (isSTLExport && skippedMeshNames.size() > 0)
    {
        std::string warningMessage = "STL Export Warning: Skipping " + std::to_string(skippedMeshNames.size()) +
                                     " mesh(es) with non-triangular faces:";
        for (const auto& meshName : skippedMeshNames)
        {
            warningMessage += "\n  - " + meshName;
        }

        if (allMeshes.size() == 0)
        {
            detailedError = "Cannot export to STL: No meshes contain only triangular faces. " + std::to_string(skippedMeshNames.size()) +
                            " mesh(es) contain quads or n-gons.\n\n" + warningMessage +
                            "\n\nPlease triangulate the meshes before exporting to STL format.";
            return nullptr;
        }

        // Log warning for skipped meshes (success case with partial export)
        std::string proceedingMessage = "\n\nSTL Export: Proceeding with " + std::to_string(allMeshes.size()) + " valid mesh(es)";
        detailedError = warningMessage + proceedingMessage;
    }

    // This is for embedding textures support.
    if (!mExportContext->converterContext.IgnoreMaterials())
    {
        size_t numTextures = mUniqueInMemoryTexturesByPath.size();
        assimpScene->mNumTextures = numTextures;
        assimpScene->mTextures = new aiTexture*[numTextures];
        for (size_t i = 0; i < mUniqueInMemoryTexturesByPath.size(); i++)
        {
            assimpScene->mTextures[i] = ToAiTexture(mUniqueInMemoryTexturesByPath[i]);
            mUniqueInMemoryTexturesByPath[i]->blob = nullptr;
        }

        assimpScene->mNumMaterials = stage->materials.size();

        if (assimpScene->mNumMaterials > 0)
        {
            assimpScene->mMaterials = new aiMaterial*[assimpScene->mNumMaterials];
            for (size_t i = 0; i < stage->materials.size(); i++)
            {
                assimpScene->mMaterials[i] = ToAiMaterial(stage, stage->materials[i]);
            }
        }
        else
        {
            assimpScene->mMaterials = nullptr;
        }
    }
    else
    {
        // When materials are ignored, initialize to empty
        assimpScene->mNumMaterials = 0;
        assimpScene->mMaterials = nullptr;
    }

    // STL preprocessing and the FBX writer require a material table, including
    // for untextured meshes and when IgnoreMaterials() is enabled.
    if (isSTLExport || mExportContext->converterContext.GetOutputAssetType() == AssetType::FBX)
    {
        AddDefaultMaterialIfEmpty(assimpScene.get());
    }

    if (allMeshes.size() > 0)
    {
        assimpScene->mNumMeshes = allMeshes.size();
        assimpScene->mMeshes = new aiMesh*[assimpScene->mNumMeshes];
        for (size_t i = 0; i < assimpScene->mNumMeshes; i++)
        {
            assimpScene->mMeshes[i] = allMeshes[i];
        }
    }

    aiNode* rootNode = nullptr;
    PopulateStageNodeTree(stage, stage->rootNode, &rootNode);
    if (rootNode)
    {
        assimpScene->mRootNode = rootNode;
    }
    else
    {
        assimpScene->mRootNode = new aiNode;
    }

    // Skinned meshes have no static instances. Preserve their rest-pose geometry
    // in formats handled by Assimp even though this exporter has no skin support.
    for (size_t i = 0; i < stage->skinMeshes.size(); ++i)
    {
        const auto& mesh = stage->meshes[stage->skinMeshes[i]->meshIndex];
        PXR_NS::GfMatrix4d transform(1.0);
        const auto& indices = skinIndices[i];
        if (indices.empty())
        {
            continue;
        }
        auto node = new aiNode(mesh->name);
        if (rootNode)
        {
            transform *= stage->rootNode->worldTransformMatrix.GetInverse();
        }
        node->mTransformation = GfMatrixToAiMatrix(transform);
        node->mNumMeshes = indices.size();
        node->mMeshes = new unsigned int[indices.size()];
        std::copy(indices.begin(), indices.end(), node->mMeshes);
        assimpScene->mRootNode->addChildren(1, &node);
    }

    return assimpScene;
}

void AssimpExporter::AddDefaultMaterialIfEmpty(aiScene* assimpScene)
{
    // Assimp requires at least one material for STL preprocessing and FBX mesh
    // export. Unbound meshes already use the default aiMesh material index (0).
    if (assimpScene->mNumMaterials == 0)
    {
        // Always allocate a new array when creating the dummy material
        assimpScene->mMaterials = new aiMaterial*[1];
        assimpScene->mNumMaterials = 1;

        aiMaterial* dummyMaterial = new aiMaterial;
        aiString dummyName("DefaultMaterial");
        dummyMaterial->AddProperty(&dummyName, AI_MATKEY_NAME);
        assimpScene->mMaterials[0] = dummyMaterial;
    }
}

void AssimpExporter::PopulateStageNodeTree(const StagePtr& stage, const StageNodePtr& currentNode, aiNode** parentAssimpNode)
{
    bool hasProps = mStageNodeInfos[currentNode];
    if (!hasProps)
    {
        return;
    }

    auto node = new aiNode;
    node->mTransformation = GfMatrixToAiMatrix(currentNode->localTransform.GetMatrix());

    std::vector<size_t> allMeshIndices;
    const bool mirrored = mExportContext->converterContext.GetOutputAssetType() == AssetType::OBJ &&
                          currentNode->worldTransformMatrix.GetDeterminant() < 0;
    for (size_t i = 0; i < currentNode->staticMeshInstances.size(); i++)
    {
        const auto& mesh = stage->meshes[currentNode->staticMeshInstances[i]];
        const auto& meshIndices = mirrored ? mMirroredMeshIndices[mesh] : mMeshIndices[mesh];
        std::copy(meshIndices.begin(), meshIndices.end(), std::back_inserter(allMeshIndices));
    }

    node->mName = currentNode->name;
    node->mNumMeshes = allMeshIndices.size();
    if (node->mNumMeshes > 0)
    {
        node->mMeshes = new unsigned int[node->mNumMeshes];
        for (size_t i = 0; i < node->mNumMeshes; i++)
        {
            node->mMeshes[i] = allMeshIndices[i];
        }
    }

    if (!*parentAssimpNode)
    {
        *parentAssimpNode = node;
    }
    else
    {
        (*parentAssimpNode)->addChildren(1, &node);
    }

    for (size_t i = 0; i < currentNode->children.size(); i++)
    {
        PopulateStageNodeTree(stage, currentNode->children[i], &node);
    }
}

void AssimpExporter::PreprocessAllNodes(const StagePtr& stage, const StageNodePtr& node)
{
    if (!node)
    {
        return;
    }

    bool hasProps = false;
    // if (node->cameras.size() > 0 || includeMeshes || node->lights.size() > 0)
    if (node->staticMeshInstances.size() > 0) // Only meshes are supported for now
    {
        hasProps = true;
    }
    else
    {
        hasProps = false;
    }

    for (const auto& child : node->children)
    {
        PreprocessAllNodes(stage, child);
        bool& childNodeHasProps = mStageNodeInfos[child];
        hasProps = hasProps || childNodeHasProps;
    }

    mStageNodeInfos[node] = hasProps;
}

OmniConverterBlobPtr AssimpExporter::ReadTextureData(const TextureImagePtr& texture)
{
    if (texture->blob)
    {
        return texture->blob;
    }

    OmniConverterBlobPtr fileBlobRAII = mExportContext->converterContext.ReadFile(texture->realPath);
    return fileBlobRAII;
};
