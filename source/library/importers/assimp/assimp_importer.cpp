// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "assimp_importer.h"

#include "../../utils/utils.h"
#include "assimp/cfileio.h"
#include "assimp/cimport.h"
#include "assimp/pbrmaterial.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <tinyxml2.h>

#include "assimp/Importer.hpp"

const static double FIXED_FPS = 24.0;


static PXR_NS::GfMatrix4d AiMatrixToGfMatrix(const aiMatrix4x4& matrix)
{
    return PXR_NS::GfMatrix4d(
        matrix.a1,
        matrix.b1,
        matrix.c1,
        matrix.d1,
        matrix.a2,
        matrix.b2,
        matrix.c2,
        matrix.d2,
        matrix.a3,
        matrix.b3,
        matrix.c3,
        matrix.d3,
        matrix.a4,
        matrix.b4,
        matrix.c4,
        matrix.d4
    );
}

static PXR_NS::GfVec3f AiVector3dToGfVector3f(const aiVector3D& vector)
{
    return PXR_NS::GfVec3f(vector.x, vector.y, vector.z);
}

static PXR_NS::GfVec3d AiVector3dToGfVector3D(const aiVector3D& vector)
{
    return PXR_NS::GfVec3d(vector.x, vector.y, vector.z);
}

static PXR_NS::GfVec2f AiVector3dToGfVector2f(const aiVector3D& vector)
{
    return PXR_NS::GfVec2f(vector.x, vector.y);
}

static PXR_NS::GfVec3f AiColor3DToGfVector3f(const aiColor3D& color)
{
    return PXR_NS::GfVec3f(color.r, color.g, color.b);
}

static PXR_NS::GfVec3f AiColor4DToGfVector3f(const aiColor4D& color)
{
    return PXR_NS::GfVec3f(color.r, color.g, color.b);
}

static PXR_NS::GfQuatd AiQuatenionToGfQuateniond(const aiQuaternion& q)
{
    return PXR_NS::GfQuatf(q.w, q.x, q.y, q.z);
}

std::string AssimpImporter::ComputeHash(const OmniFutureThreadContextPtr& context)
{
    return std::string();
}

StagePtr AssimpImporter::ImportStage(const OmniFutureThreadContextPtr& context, OmniConverterStatus& status, std::string& detailedError)
{
    mThreadContext = context;
    status = OmniConverterStatus::OK;
    const std::string importAssetPath = mThreadContext->converterContext.GetImportAssetPath();

    Log("Starting to import asset with Assimp importer.");

    static auto propsDeleter = [](aiPropertyStore* props)
    {
        if (props)
        {
            aiReleasePropertyStore(props);
        }
    };

    auto props = std::shared_ptr<aiPropertyStore>(aiCreatePropertyStore(), propsDeleter);
    aiSetImportPropertyInteger(props.get(), AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);
    aiSetImportPropertyInteger(props.get(), AI_CONFIG_IMPORT_REMOVE_EMPTY_BONES, 0);

    // For BVH imports, we don't create placeholder mesh for skeletons
    if (mThreadContext->converterContext.GetImportAssetType() == AssetType::BVH)
    {
        aiSetImportPropertyInteger(props.get(), AI_CONFIG_IMPORT_NO_SKELETON_MESHES, 1);
    }


    struct AssimpFileIOContext
    {
        const OmniConverterContext* context;
        OmniConverterStatus* status;
        std::string* detailedError;
        std::unordered_map<std::string, OmniConverterBlobPtr> cachedBlobs;
    };

    struct AssimpFile
    {
        OmniConverterBlobPtr blob;
        size_t currentPos = 0;
    };

    // Assimp io override to read file with customized IO
    static auto assimpFileWriteProc = [](aiFile* file, const char* buffer, size_t pSize, size_t count)
    {
        return (size_t)0;
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
        if (!assimpFile->blob)
        {
            return (size_t)0;
        }

        return assimpFile->blob->size;
    };

    static auto assimpFileSeekProc = [](aiFile* file, size_t pos, aiOrigin origin)
    {
        AssimpFile* assimpFile = (AssimpFile*)file->UserData;
        if (!assimpFile->blob)
        {
            return AI_FAILURE;
        }

        if (aiOrigin_SET == origin)
        {
            if (pos > assimpFile->blob->size)
            {
                return AI_FAILURE;
            }
            assimpFile->currentPos = pos;
        }
        else if (aiOrigin_END == origin)
        {
            if (pos > assimpFile->blob->size)
            {
                return AI_FAILURE;
            }
            assimpFile->currentPos = assimpFile->blob->size - pos;
        }
        else
        {
            if (pos + assimpFile->currentPos > assimpFile->blob->size)
            {
                return AI_FAILURE;
            }
            assimpFile->currentPos += pos;
        }

        return AI_SUCCESS;
    };

    static auto assimpFileReadProc = [](aiFile* file, char* buffer, size_t elementSize, size_t count) -> size_t
    {
        AssimpFile* assimpFile = (AssimpFile*)file->UserData;
        if (!file || !assimpFile->blob)
        {
            return 0;
        }

        const size_t cnt = std::min(count, (assimpFile->blob->size - assimpFile->currentPos) / elementSize);
        const size_t ofs = elementSize * cnt;

        ::memcpy(buffer, (char*)assimpFile->blob->buffer + assimpFile->currentPos, ofs);
        assimpFile->currentPos += ofs;

        return cnt;
    };

    static auto assimpFileOpenProc = [](aiFileIO* fileIO, const char* file, const char* flags) -> aiFile*
    {
        AssimpFileIOContext* assimpContext = (AssimpFileIOContext*)fileIO->UserData;
        OmniConverterBlobPtr assetBlobPtr;

        // It's possible that the same file will be opened multiple times.
        // Caches it to speed up data read.
        auto iter = assimpContext->cachedBlobs.find(file);
        if (iter != assimpContext->cachedBlobs.end())
        {
            assetBlobPtr = iter->second;
        }
        else
        {
            assetBlobPtr = assimpContext->context->ReadFile(file);
            if (!assetBlobPtr)
            {
                *assimpContext->detailedError = "Asset convert failed as file " + std::string(file) + " read failed.";
                assimpContext->context->Log(assimpContext->detailedError->c_str());
                *assimpContext->status = OmniConverterStatus::FILE_READ_ERROR;
                return nullptr;
            }
            else
            {
                assimpContext->cachedBlobs.insert({ file, assetBlobPtr });
            }
        }

        AssimpFile* assimpFile = new AssimpFile;
        assimpFile->blob = assetBlobPtr;

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

    static auto sceneDeleter = [](const aiScene* scene)
    {
        if (scene)
        {
            aiReleaseImport(scene);
        }
    };

    AssimpFileIOContext assimpFileIOContext;
    assimpFileIOContext.context = &mThreadContext->converterContext;
    assimpFileIOContext.status = &status;
    assimpFileIOContext.detailedError = &detailedError;

    aiFileIO fileIO;
    fileIO.OpenProc = assimpFileOpenProc;
    fileIO.CloseProc = assimpFileCloseProc;
    fileIO.UserData = (aiUserData)(&assimpFileIOContext);
    unsigned int ppsteps = 0;
    if (mThreadContext->converterContext.SmoothNormals())
    {
        ppsteps = ppsteps | aiProcess_GenSmoothNormals;
    }
    auto assimpScene = aiImportFileExWithProperties(importAssetPath.c_str(), ppsteps, &fileIO, props.get());
    auto sceneRAII = std::shared_ptr<const aiScene>(assimpScene, sceneDeleter);
    props.reset();
    assimpFileIOContext.cachedBlobs.clear();

    if (!sceneRAII || !sceneRAII->mRootNode)
    {
        detailedError = "Failed to load asset " + importAssetPath;
        status = OmniConverterStatus::UNSUPPORTED_IMPORT_FORMAT;
        Log(detailedError);
        return nullptr;
    }
    else if (!sceneRAII->mRootNode)
    {
        detailedError = "Failed to load asset " + mThreadContext->converterContext.GetImportAssetPath();
        status = OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT;
        Log(detailedError);
        return nullptr;
    }

    auto stage = std::make_shared<Stage>();
    stage->yAxis = true;

    // override up-axis with user-defined value
    if (mThreadContext->converterContext.ConvertUpZ())
    {
        stage->yAxis = false;
    }

    double unitsScale = 1.0;

    if (mThreadContext->converterContext.UseMeterPerUnit())
    {
        stage->worldUnits = 1.0;
    }
    else
    {
        stage->worldUnits = 0.01;
    }

    if (PXR_NS::TfStringEndsWith(importAssetPath, ".dae"))
    {
        // workaround for Assimp's wrong transform to mRootNode for COLLADA files
        // https://github.com/assimp/assimp/issues/849#issuecomment-269837820
        sceneRAII->mRootNode->mTransformation = aiMatrix4x4();

        // Load the COLLADA file and retrieve its unit used since it's not embedded in sceneRAII->mMetaData.
        // on load failure we just fallback to default unitScale like before
        tinyxml2::XMLDocument doc;
        doc.LoadFile(importAssetPath.c_str());
        if (doc.ErrorID() == 0)
        {
            tinyxml2::XMLElement* root = doc.RootElement();
            tinyxml2::XMLElement* asset = root->FirstChildElement("asset");
            tinyxml2::XMLElement* unit = asset->FirstChildElement("unit");

            double meters = 0.0;
            tinyxml2::XMLError result = unit->QueryDoubleAttribute("meter", &meters);
            if (result == tinyxml2::XML_SUCCESS && meters > 0.0)
            {
                if (mThreadContext->converterContext.KeepAssetUnits())
                {
                    stage->worldUnits = meters;
                    unitsScale = 1.0;
                }
                else if (mThreadContext->converterContext.UseMeterPerUnit())
                {
                    stage->worldUnits = 1.0;
                    unitsScale = meters;
                }
                else
                {
                    stage->worldUnits = 0.01;
                    unitsScale = meters * 100.0;
                }
            }
        }
    }

    PopulateAllMaterials(assimpScene, stage);
    PopulateAllCameras(assimpScene, stage);
    ReadAnimationInformation(assimpScene, stage);
    PopulateAllMeshes(assimpScene, stage, unitsScale);
    auto rootNode = assimpScene->mRootNode;
    aiMatrix4x4 identityMatrix;
    PopulateStageTree(assimpScene, rootNode, identityMatrix, stage, nullptr, unitsScale);
    PopulateAllSkeletons(assimpScene, rootNode, stage, stage->rootNode, unitsScale);

    sceneRAII.reset();
    MergeNamedMeshes(stage);

    Log("Asset import finished.");

    StageUtils::OptimizeMeshes(stage);

    return stage;
}

void AssimpImporter::Log(const std::string& message)
{
    mThreadContext->converterContext.Log(message.c_str());
}

std::vector<std::string> AssimpImporter::PopulateAllTextures(const StagePtr& stage, const aiScene* assimpScene)
{
    // Populates all embedded textures.
    std::unordered_set<std::string> allInMemoryTextures;
    std::vector<bool> isInMemoryTexture(assimpScene->mNumTextures, false);
    std::vector<std::string> sceneTexturePaths(assimpScene->mNumTextures);
    for (size_t i = 0; i < assimpScene->mNumTextures; i++)
    {
        auto assimpTexture = assimpScene->mTextures[i];
        std::string filepath = PathUtils::NormalizePath(std::string(assimpTexture->mFilename.C_Str()));
        if (assimpTexture->mHeight == 0)
        {
            isInMemoryTexture[i] = true;

            // It's possible that file name is empty like glTF,
            // rename it like $AssetName_texture$Index.png
            if (filepath.empty())
            {
                const std::string& assetName = PathUtils::GetFileName(mThreadContext->converterContext.GetImportAssetPath());
                const std::string& format = assimpTexture->achFormatHint;
                if (format.empty())
                {
                    continue;
                }
                filepath = assetName + "_texture" + std::to_string(allInMemoryTextures.size()) + "." + format;
            }
            allInMemoryTextures.insert(filepath);

            // texture->mWidth is size if height is zero
            OmniConverterBlob dataBlob = { (uint8_t*)assimpTexture->pcData, assimpTexture->mWidth, nullptr };
            auto texture = std::make_shared<TextureImage>();
            auto data = new uint8_t[assimpTexture->mWidth];
            memcpy(data, (uint8_t*)assimpTexture->pcData, assimpTexture->mWidth);
            texture->blob = createOmniConverterBlob((uint8_t*)data, assimpTexture->mWidth);
            texture->originalPath = filepath;
            stage->images.push_back(texture);
        }

        sceneTexturePaths[i] = filepath;
    }

    std::unordered_set<std::string> allNonInMemoryTextureFiles;
    if (assimpScene->HasMaterials())
    {
        for (size_t i = 0; i < assimpScene->mNumMaterials; i++)
        {
            auto material = assimpScene->mMaterials[i];
            for (size_t j = int(aiTextureType_DIFFUSE); j <= int(aiTextureType_TRANSMISSION); j++)
            {
                const auto& textures = GetMaterialTexturePaths(material, aiTextureType(j));
                for (const auto& texturePath : textures)
                {
                    auto iter = allInMemoryTextures.find(texturePath);
                    if (iter != allInMemoryTextures.end())
                    {
                        continue;
                    }

                    if (texturePath[0] == '*' && texturePath.size() > 1) // If it's texture reference
                    {
                        size_t index = std::atoi(texturePath.substr(1).c_str());
                        if (index < isInMemoryTexture.size())
                        {
                            if (!isInMemoryTexture[index])
                            {
                                allNonInMemoryTextureFiles.insert(sceneTexturePaths[index]);
                            }
                            continue;
                        }
                    }

                    allNonInMemoryTextureFiles.insert(texturePath);
                }
            }
        }
    }

    for (auto& texturePath : allNonInMemoryTextureFiles)
    {
        // It's possible that asset includes empty texture reference
        if (texturePath.length() == 0)
        {
            continue;
        }

        std::string path;
        const std::string& importBasePath = mThreadContext->converterContext.GetImportAssetDir();
        if (!PathUtils::IsAbsolutePath(texturePath))
        {
            path = PathUtils::JoinPaths(importBasePath, texturePath);
        }
        else
        {
            path = texturePath;
        }

        // Try to find texture from the dir of import asset.
        std::string filteredPath;
        if (!mThreadContext->converterContext.IsPathExisted(path))
        {
            auto fileName = PathUtils::GetFileName(path, true);
            path = PathUtils::JoinPaths(importBasePath, fileName);
            mThreadContext->converterContext.FilterTexturePath(path, filteredPath);
        }
        else
        {
            filteredPath = path;
        }

        if (!filteredPath.empty())
        {
            auto texture = std::make_shared<TextureImage>();
            texture->originalPath = texturePath;
            texture->realPath = filteredPath;
            stage->images.push_back(texture);
        }
        else
        {
            std::string warning = "[WARNING] TextureImage (" + path + ") cannot be found but is referenced in original asset.";
            Log(warning);
        }
    }

    return sceneTexturePaths;
}

void AssimpImporter::PopulateAllCameras(const aiScene* scene, const StagePtr& stage)
{
    if (mThreadContext->converterContext.IgnoreCameras())
    {
        return;
    }

    for (size_t i = 0; i < scene->mNumCameras; i++)
    {
        auto rawCamera = scene->mCameras[i];
        mNameAiCameraMapping.insert({ rawCamera->mName.C_Str(), rawCamera });
    }
}

void AssimpImporter::PopulateAllMaterials(const aiScene* assimpScene, const StagePtr& stage)
{
    if (mThreadContext->converterContext.IgnoreMaterials())
    {
        return;
    }

    const auto& allSceneTexturePaths = PopulateAllTextures(stage, assimpScene);
    for (size_t i = 0; i < assimpScene->mNumMaterials; i++)
    {
        auto assimpMaterial = assimpScene->mMaterials[i];
        auto material = std::make_shared<Material>();
        material->name = assimpMaterial->GetName().C_Str();
        if (material->name.empty())
        {
            material->name = "material";
        }

        auto FilterPath = [&allSceneTexturePaths](const std::string& path)
        {
            if (path.size() > 0 && path[0] == '*')
            {
                size_t index = std::atoi(path.substr(1).c_str());
                if (index < allSceneTexturePaths.size())
                {
                    return allSceneTexturePaths[index];
                }
            }

            return path;
        };

        aiColor3D color;
        if (assimpMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color) == aiReturn_SUCCESS)
        {
            material->diffuseColor = AiColor3DToGfVector3f(color);
            material->hasDiffuseColor = true;
        }

        if (assimpMaterial->Get(AI_MATKEY_COLOR_SPECULAR, color) == aiReturn_SUCCESS)
        {
            material->specularColor = AiColor3DToGfVector3f(color);
            material->hasSpecularColor = true;
        }

        if (assimpMaterial->Get(AI_MATKEY_COLOR_EMISSIVE, color) == aiReturn_SUCCESS)
        {
            material->emissiveColor = AiColor3DToGfVector3f(color);
            material->hasEmissiveColor = true;
        }

        float opacity = 0.0f;
        if (assimpMaterial->Get(AI_MATKEY_OPACITY, opacity) == aiReturn_SUCCESS)
        {
            material->opacity = opacity;
            material->hasOpacity = true;
        }

        float specularGlossyFactor = 1.0f;
        if (assimpMaterial->Get(AI_MATKEY_GLOSSINESS_FACTOR, specularGlossyFactor) == aiReturn_SUCCESS)
        {
            material->glossyFactor = specularGlossyFactor;
            material->hasGlossyFactor = true;
        }

        float roughness = 0.0f;
        if (assimpMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == aiReturn_SUCCESS)
        {
            material->roughnessFactor = roughness;
            material->hasRoughnessFactor = true;
        }

        float metalic = 0.0f;
        if (assimpMaterial->Get(AI_MATKEY_METALLIC_FACTOR, metalic) == aiReturn_SUCCESS)
        {
            material->metallicFactor = metalic;
            material->hasMetallicFactor = true;
        }

        float clearCoatFactor = 0.0f;
        if (assimpMaterial->Get(AI_MATKEY_CLEARCOAT_FACTOR, clearCoatFactor) == aiReturn_SUCCESS)
        {
            material->clearCoatFactor = clearCoatFactor;
        }

        float clearCoatRoughnessFactor = 0.0f;
        if (assimpMaterial->Get(AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR, clearCoatRoughnessFactor) == aiReturn_SUCCESS)
        {
            material->clearCoatRoughnessFactor = clearCoatRoughnessFactor;
            material->hasClearCoatRoughnessFactor = true;
        }

        float transmissionFactor = 0.0f;
        if (assimpMaterial->Get(AI_MATKEY_TRANSMISSION_FACTOR, transmissionFactor) == aiReturn_SUCCESS)
        {
            material->transmissionFactor = transmissionFactor;
            material->hasTransmissionFactor = true;
        }

        if (assimpMaterial->Get(AI_MATKEY_SHEEN_COLOR_FACTOR, color) == aiReturn_SUCCESS)
        {
            material->sheenColor = AiColor3DToGfVector3f(color);
            material->hasSheenColor = true;
        }

        float sheenRoughnessFactor = 0.0f;
        if (assimpMaterial->Get(AI_MATKEY_SHEEN_ROUGHNESS_FACTOR, sheenRoughnessFactor) == aiReturn_SUCCESS)
        {
            material->sheenRoughnessFactor = sheenRoughnessFactor;
            material->hasSheenRoughnessFactor = true;
        }

        auto LoadTexture = [&](aiTextureType assimpTextureType, int index, MaterialTextureType omniTextureType, TextureOutputMode outputMode)
        {
            static auto ToTextureMapMode = [](aiTextureMapMode textureMapMode)
            {
                switch (textureMapMode)
                {
                    case aiTextureMapMode_Wrap:
                        return TextureWrapMode::REPEAT;
                    case aiTextureMapMode_Clamp:
                        return TextureWrapMode::CLAMP;
                    case aiTextureMapMode_Mirror:
                        return TextureWrapMode::MIRROR;
                    default:
                        return TextureWrapMode::REPEAT;
                }

                return TextureWrapMode::REPEAT;
            };

            aiString path;
            unsigned int uvIndex;
            if (assimpMaterial->GetTexture(assimpTextureType, index, &path, nullptr, &uvIndex) == aiReturn_SUCCESS)
            {
                auto& textureReference = material->GetTextureReference(omniTextureType);
                textureReference.imageIndex = FindTexture(stage, FilterPath(path.C_Str()));
                textureReference.outputMode = outputMode;
                textureReference.uvIndex = uvIndex;

                aiTextureMapMode textureMapMode;
                assimpMaterial->Get(AI_MATKEY_MAPPINGMODE_U(assimpTextureType, index), textureMapMode);
                textureReference.wrapS = ToTextureMapMode(textureMapMode);
                assimpMaterial->Get(AI_MATKEY_MAPPINGMODE_V(assimpTextureType, index), textureMapMode);
                textureReference.wrapT = ToTextureMapMode(textureMapMode);
            }
        };

        LoadTexture(aiTextureType_DIFFUSE, 0, MaterialTextureType::DIFFUSE, TextureOutputMode::RGB);
        LoadTexture(aiTextureType_SPECULAR, 0, MaterialTextureType::SPECULAR, TextureOutputMode::RGB);
        LoadTexture(aiTextureType_EMISSIVE, 0, MaterialTextureType::EMISSIVE, TextureOutputMode::RGB);
        LoadTexture(aiTextureType_OPACITY, 0, MaterialTextureType::OPACITY, TextureOutputMode::AVERAGE);
        LoadTexture(aiTextureType_HEIGHT, 0, MaterialTextureType::NORMAL, TextureOutputMode::RGB);
        LoadTexture(aiTextureType_NORMALS, 0, MaterialTextureType::NORMAL, TextureOutputMode::RGB);

        auto& opacityTexture = material->GetTextureReference(MaterialTextureType::OPACITY);
        if (opacityTexture.IsValid() && HasAlphaChannel(mThreadContext, stage, opacityTexture.imageIndex))
        {
            opacityTexture.outputMode = TextureOutputMode::ALPHA;
        }

        auto& diffuseTexture = material->GetTextureReference(MaterialTextureType::DIFFUSE);
        if (!opacityTexture.IsValid() && diffuseTexture.IsValid() && HasAlphaChannel(mThreadContext, stage, diffuseTexture.imageIndex))
        {
            TextureReference copyReference = diffuseTexture;
            copyReference.outputMode = TextureOutputMode::ALPHA;
            material->SetTextureReference(MaterialTextureType::OPACITY, copyReference);
        }

        auto& glossyTexture = material->GetTextureReference(MaterialTextureType::GLOSSY);
        auto& specularTexture = material->GetTextureReference(MaterialTextureType::SPECULAR);
        if (!glossyTexture.IsValid() && specularTexture.IsValid() && HasAlphaChannel(mThreadContext, stage, specularTexture.imageIndex))
        {
            TextureReference copyReference = specularTexture;
            copyReference.outputMode = TextureOutputMode::ALPHA;
            material->SetTextureReference(MaterialTextureType::GLOSSY, copyReference);
        }

        if (glossyTexture.IsValid() && !material->hasGlossyFactor)
        {
            material->hasGlossyFactor = true;
            material->glossyFactor = 1.0f;
        }

        if (specularTexture.IsValid() && !material->hasSpecularColor)
        {
            material->specularColor = PXR_NS::GfVec3f(1.0f);
            material->hasSpecularColor = true;
        }

        // glTF importer uses aiTextureType_LIGHTMAP for occlusion.
        LoadTexture(aiTextureType_LIGHTMAP, 0, MaterialTextureType::OCCLUSION, TextureOutputMode::AVERAGE);
        LoadTexture(aiTextureType_AMBIENT_OCCLUSION, 0, MaterialTextureType::OCCLUSION, TextureOutputMode::AVERAGE);
        LoadTexture(AI_MATKEY_CLEARCOAT_TEXTURE, MaterialTextureType::CLEARCOAT, TextureOutputMode::AVERAGE);
        if (material->HasTextureReference(MaterialTextureType::CLEARCOAT) && !material->hasClearCoatFactor)
        {
            material->hasClearCoatFactor = true;
            material->clearCoatFactor = 1.0f;
        }

        LoadTexture(AI_MATKEY_CLEARCOAT_ROUGHNESS_TEXTURE, MaterialTextureType::CLEARCOAT_ROUGHNESS, TextureOutputMode::AVERAGE);
        if (material->HasTextureReference(MaterialTextureType::CLEARCOAT_ROUGHNESS) && !material->hasClearCoatRoughnessFactor)
        {
            material->hasClearCoatRoughnessFactor = true;
            material->hasClearCoatRoughnessFactor = 1.0f;
        }

        LoadTexture(AI_MATKEY_CLEARCOAT_NORMAL_TEXTURE, MaterialTextureType::CLEARCOAT_NORMAL, TextureOutputMode::RGB);
        LoadTexture(AI_MATKEY_TRANSMISSION_TEXTURE, MaterialTextureType::TRANSMISSION, TextureOutputMode::R);
        if (material->HasTextureReference(MaterialTextureType::TRANSMISSION) && !material->hasTransmissionFactor)
        {
            material->hasTransmissionFactor = true;
            material->transmissionFactor = 1.0f;
        }

        LoadTexture(AI_MATKEY_SHEEN_COLOR_TEXTURE, MaterialTextureType::SHEEN, TextureOutputMode::RGB);
        if (material->HasTextureReference(MaterialTextureType::SHEEN) && !material->hasSheenColor)
        {
            material->hasSheenColor = true;
            material->sheenColor = PXR_NS::GfVec3f(1.0f);
        }

        LoadTexture(AI_MATKEY_SHEEN_ROUGHNESS_TEXTURE, MaterialTextureType::SHEEN_ROUGHNESS, TextureOutputMode::ALPHA);
        if (material->HasTextureReference(MaterialTextureType::SHEEN_ROUGHNESS) && !material->hasSheenRoughnessFactor)
        {
            material->hasSheenRoughnessFactor = true;
            material->sheenColor = PXR_NS::GfVec3f(1.0f);
        }

        auto iter = mMaterialIndex.find(material->name);
        // Reuse material.
        if (iter != mMaterialIndex.end() && material == stage->materials[iter->second])
        {
            mMaterialMappedIndex.push_back(iter->second);
            continue;
        }

        if (iter == mMaterialIndex.end())
        {
            mMaterialNameIndex.insert({ material->name, 1 });
        }
        else
        {
            size_t& index = mMaterialNameIndex[material->name];
            material->name += std::to_string(index);
            index += 1;
        }
        stage->materials.push_back(material);
        size_t index = stage->materials.size() - 1;
        mMaterialMappedIndex.push_back(index);
        mMaterialIndex.insert({ material->name, index });
    }
}

void AssimpImporter::PopulateAllMeshes(const aiScene* assimpScene, const StagePtr& stage, double scale)
{
    if (assimpScene->mNumMeshes == 0)
    {
        return;
    }

    size_t totalMesh = 0;
    mAssimpMeshIndices.resize(assimpScene->mNumMeshes);
    for (size_t i = 0; i < assimpScene->mNumMeshes; i++)
    {
        // Skip invalid mesh if there are no vertices
        auto assimpMesh = assimpScene->mMeshes[i];

        bool isPointCloud = assimpMesh && assimpMesh->mNumVertices != 0 && assimpMesh->mNumFaces == 0;
        bool isInvalidMesh = !assimpMesh || assimpMesh->mNumVertices == 0;
        if (isInvalidMesh)
        {
            std::string meshName = std::string(assimpMesh->mName.C_Str());
            meshName = meshName.length() > 0 ? meshName : "mesh";
            std::string msg = "Skipped converting " + meshName + " as it's an invalid mesh or missing vertex data";
            Log(msg);
            continue;
        }

        mAssimpMeshIndices[i] = totalMesh;
        totalMesh += 1;

        auto mesh = std::make_shared<Mesh>();
        stage->meshes.push_back(mesh);
        mesh->name = assimpMesh->mName.C_Str();

        if (isPointCloud)
        {
            mesh->points.reserve(assimpMesh->mNumVertices);
            if (assimpMesh->HasNormals())
            {
                mesh->normals.reserve(assimpMesh->mNumVertices);
            }

            // Populate points and normals
            for (size_t j = 0; j < assimpMesh->mNumVertices; j++)
            {
                mesh->points.push_back(AiVector3dToGfVector3f(assimpMesh->mVertices[j]) * scale);
                if (assimpMesh->HasNormals())
                {
                    mesh->normals.push_back(AiVector3dToGfVector3f(assimpMesh->mNormals[j]));
                }
            }

            // Populate colors
            unsigned int numColorChannels = assimpMesh->GetNumColorChannels();
            if (numColorChannels > 0)
            {
                mesh->colors.resize(numColorChannels);
                for (unsigned int i = 0; i < numColorChannels; ++i)
                {
                    if (assimpMesh->HasVertexColors(i))
                    {
                        mesh->colors[i].reserve(assimpMesh->mNumVertices);
                        for (unsigned int j = 0; j < assimpMesh->mNumVertices; ++j)
                        {
                            mesh->colors[i].push_back(AiColor4DToGfVector3f(assimpMesh->mColors[i][j]));
                        }
                    }
                }
            }
        }
        else
        {
            mesh->uvs.resize(assimpMesh->GetNumUVChannels());
            mesh->colors.resize(assimpMesh->GetNumColorChannels());
            // Populate mesh details
            for (size_t j = 0; j < assimpMesh->mNumVertices; j++)
            {
                mesh->points.push_back(AiVector3dToGfVector3f(assimpMesh->mVertices[j]) * scale);
            }

            // Populate face vertex indices
            for (size_t j = 0; j < assimpMesh->mNumFaces; j++)
            {
                const aiFace& face = assimpMesh->mFaces[j];
                if (face.mNumIndices >= 3)
                {
                    for (size_t k = 0; k < face.mNumIndices; k++)
                    {
                        mesh->faceVertexIndices.push_back(face.mIndices[k]);
                    }
                }
            }

            // Face varying data
            for (size_t j = 0; j < assimpMesh->mNumFaces; j++)
            {
                const aiFace& face = assimpMesh->mFaces[j];
                if (face.mNumIndices >= 3)
                {
                    for (size_t k = 0; k < face.mNumIndices; k++)
                    {
                        if (assimpMesh->mNormals)
                        {
                            mesh->normals.push_back(AiVector3dToGfVector3f(assimpMesh->mNormals[face.mIndices[k]]));
                        }

                        for (size_t m = 0; m < mesh->uvs.size(); m++)
                        {
                            auto uv = AiVector3dToGfVector2f(assimpMesh->mTextureCoords[m][face.mIndices[k]]);
                            mesh->uvs[m].push_back(uv);
                        }

                        for (size_t m = 0; m < mesh->colors.size(); m++)
                        {
                            mesh->colors[m].push_back(AiColor4DToGfVector3f(assimpMesh->mColors[m][face.mIndices[k]]));
                        }
                    }
                    mesh->faceVertexCounts.push_back(face.mNumIndices);
                }
            }

            MeshGeomSubset subset;
            subset.faceIndices.resize(mesh->faceVertexCounts.size());
            if (!mThreadContext->converterContext.IgnoreMaterials())
            {
                subset.materialIndex = mMaterialMappedIndex[assimpMesh->mMaterialIndex];
                if (subset.materialIndex != INVALID_MATERIAL_INDEX)
                {
                    subset.name = stage->materials[subset.materialIndex]->name;
                }
            }
            else
            {
                subset.materialIndex = INVALID_MATERIAL_INDEX;
            }

            std::iota(subset.faceIndices.begin(), subset.faceIndices.end(), 0);
            mesh->meshSubsets.push_back(subset);
        }
    }
}

void AssimpImporter::PopulateStageTree(
    const aiScene* scene,
    const aiNode* currentNode,
    const aiMatrix4x4& parentWorldTransform,
    const StagePtr& stage,
    const StageNodePtr& parentStageNode,
    double scale
)
{
    aiMatrix4x4 currentTransform;
    std::string nodeName(currentNode->mName.C_Str());
    StageNodePtr stageNode;
    stageNode = std::make_shared<StageNode>(nodeName);
    stageNode->parent = parentStageNode;
    if (!parentStageNode)
    {
        stage->rootNode = stageNode;
    }
    else
    {
        parentStageNode->children.push_back(stageNode);
    }

    if (!mThreadContext->converterContext.IgnoreAnimations())
    {
        stageNode->transformAnimationTracks = GetNodeAnimation(stage->animationTracks, scene, currentNode, scale);
    }

    const auto& aiLocalTransform = currentNode->mTransformation;
    const auto& aiWorldTransform = parentWorldTransform * aiLocalTransform;

    // Apply scale to transforms, similar to FBX importer
    auto localMatrix = AiMatrixToGfMatrix(aiLocalTransform);
    auto worldMatrix = AiMatrixToGfMatrix(aiWorldTransform);

    // Scale the translation components
    auto localTranslation = localMatrix.ExtractTranslation();
    localMatrix.SetTranslateOnly(localTranslation * scale);

    auto worldTranslation = worldMatrix.ExtractTranslation();
    worldMatrix.SetTranslateOnly(worldTranslation * scale);

    stageNode->localTransform.SetMatrix(localMatrix);
    stageNode->worldTransformMatrix = worldMatrix;

    for (size_t i = 0; i < currentNode->mNumMeshes; i++)
    {
        auto meshIndex = currentNode->mMeshes[i];
        if (meshIndex > mAssimpMeshIndices.size() || mAssimpMeshIndices[meshIndex] == -1)
        {
            continue;
        }

        meshIndex = mAssimpMeshIndices[meshIndex];
        stageNode->staticMeshInstances.push_back(meshIndex);
    }

    auto iter = mNameAiCameraMapping.find(nodeName);
    if (iter != mNameAiCameraMapping.end())
    {
        auto rawCamera = iter->second;
        CameraPtr camera = std::make_shared<Camera>();
        camera->name = rawCamera->mName.C_Str();
        const auto& worldMatrix = stageNode->worldTransformMatrix;
        camera->position = worldMatrix.Transform(camera->position);
        camera->up = worldMatrix.TransformDir(camera->up);
        camera->lookAt = worldMatrix.Transform(camera->lookAt);
        if (rawCamera->mOrthographicWidth != 0)
        {
            PXR_NS::GfCamera gfCamera;
            gfCamera.SetOrthographicFromAspectRatioAndSize(
                rawCamera->mAspect,
                rawCamera->mOrthographicWidth * 10 * scale,
                PXR_NS::GfCamera::FOVHorizontal
            );
            camera->projectionType = PXR_NS::GfCamera::Orthographic;
            camera->horizonalAperture = gfCamera.GetHorizontalAperture();
            camera->verticallAperture = gfCamera.GetVerticalAperture();
            camera->focalLength = gfCamera.GetFocalLength();
        }
        else
        {
            camera->projectionType = PXR_NS::GfCamera::Perspective;

            PXR_NS::GfCamera gfCamera;
            gfCamera.SetPerspectiveFromAspectRatioAndFieldOfView(
                rawCamera->mAspect,
                AI_RAD_TO_DEG(rawCamera->mHorizontalFOV),
                PXR_NS::GfCamera::FOVHorizontal
            );

            camera->focalLength = gfCamera.GetFocalLength() * scale;
            camera->horizonalAperture = gfCamera.GetHorizontalAperture() * scale;
            camera->verticallAperture = gfCamera.GetVerticalAperture() * scale;
        }
        camera->clippingNear = rawCamera->mClipPlaneNear * scale;
        camera->clippingFar = rawCamera->mClipPlaneFar * scale;
        stage->cameras.push_back(camera);
        stageNode->cameras.push_back(stage->cameras.size() - 1);
    }

    for (size_t i = 0; i < currentNode->mNumChildren; i++)
    {
        PopulateStageTree(scene, currentNode->mChildren[i], aiWorldTransform, stage, stageNode, scale);
    }
}

void AssimpImporter::PopulateAllSkeletons(
    const aiScene* scene,
    const aiNode* currentNode,
    const std::shared_ptr<Stage>& stage,
    const std::shared_ptr<StageNode>& stageNode,
    double scale
)
{
    std::string nodeName(currentNode->mName.C_Str());
    if (isBoneNode(currentNode))
    {
        PopulateSkeleton(scene, currentNode, stage, stageNode, nullptr, scale);
    }
    else
    {
        for (size_t i = 0; i < currentNode->mNumChildren; i++)
        {
            const std::string& childNodeName = currentNode->mChildren[i]->mName.C_Str();
            PopulateAllSkeletons(scene, currentNode->mChildren[i], stage, stageNode->children[i], scale);
        }
    }
}

void AssimpImporter::PopulateSkeleton(
    const aiScene* scene,
    const aiNode* currentNode,
    const std::shared_ptr<Stage>& stage,
    const std::shared_ptr<StageNode>& stageNode,
    const std::shared_ptr<StageNode>& parentBoneNode,
    double scale
)
{
    const std::string& nodeName = currentNode->mName.C_Str();
    stageNode->isBoneNode = true;

    PXR_NS::GfMatrix4d bindMatrix;
    const auto& aiLocalTransform = currentNode->mTransformation;
    bindMatrix = AiMatrixToGfMatrix(aiLocalTransform);

    // Apply scale to the translation component of the local transform
    auto localTranslation = bindMatrix.ExtractTranslation();
    bindMatrix.SetTranslateOnly(localTranslation * scale);

    if (parentBoneNode)
    {
        const auto& parentBindTransform = parentBoneNode->bindTransform;
        bindMatrix = bindMatrix * parentBindTransform;
    }

    stageNode->bindTransform = bindMatrix;

    if (parentBoneNode)
    {
        const auto& parentBindTransform = parentBoneNode->bindTransform;
        stageNode->restTransform = bindMatrix * parentBindTransform.GetInverse();
    }
    else
    {
        stageNode->restTransform = stageNode->bindTransform;
    }

    for (size_t i = 0; i < currentNode->mNumChildren; i++)
    {
        PopulateSkeleton(scene, currentNode->mChildren[i], stage, stageNode->children[i], stageNode, scale);
    }
}


void AssimpImporter::MergeNamedMeshes(const StagePtr& stage)
{
    auto originalMeshArrays = stage->meshes;
    std::unordered_map<std::string, std::vector<MeshPtr>> namedMeshes;
    for (size_t i = 0; i < stage->meshes.size(); i++)
    {
        const auto& mesh = stage->meshes[i];
        namedMeshes[mesh->name].push_back(mesh);
    }

    std::unordered_map<MeshPtr, int> meshRemapping;
    std::vector<MeshPtr> newMeshes;
    for (const auto& namedMesh : namedMeshes)
    {
        auto mergedMesh = namedMesh.second[0];
        mergedMesh->name = namedMesh.first;
        for (size_t i = 1; i < namedMesh.second.size(); i++)
        {
            StageUtils::MergeMesh(mergedMesh, namedMesh.second[i]);
        }

        newMeshes.push_back(mergedMesh);
        for (const auto& mesh : namedMesh.second)
        {
            meshRemapping[mesh] = newMeshes.size() - 1;
        }
    }

    stage->meshes = newMeshes;

    // Retargeting mesh instances
    StageUtils::TraverseStageTree(
        stage->rootNode,
        [&stage, &meshRemapping, &originalMeshArrays](const StageNodePtr& stageNode)
        {
            std::unordered_set<int> uniqueMeshIndices;
            for (size_t meshIndex : stageNode->staticMeshInstances)
            {
                if (meshIndex < originalMeshArrays.size())
                {
                    const auto& originMesh = originalMeshArrays[meshIndex];
                    auto iter = meshRemapping.find(originMesh);
                    if (iter != meshRemapping.end())
                    {
                        uniqueMeshIndices.insert(iter->second);
                    }
                }
            }
            stageNode->staticMeshInstances.assign(uniqueMeshIndices.begin(), uniqueMeshIndices.end());

            return true;
        }
    );
}

void AssimpImporter::ReadAnimationInformation(const aiScene* assimpScene, const StagePtr& stage)
{
    if (mThreadContext->converterContext.IgnoreAnimations())
    {
        return;
    }

    for (size_t i = 0; i < assimpScene->mNumAnimations; i++)
    {
        auto rawAnimation = assimpScene->mAnimations[i];
        AnimationTrack animation;
        animation.name = std::string(rawAnimation->mName.data);
        if (rawAnimation->mTicksPerSecond <= 0)
        {
            animation.fps = FIXED_FPS;
        }
        else
        {
            animation.fps = rawAnimation->mTicksPerSecond;
        }

        double factor = FIXED_FPS / animation.fps;
        auto keyFrames = (int)(animation.fps * rawAnimation->mDuration);
        if (keyFrames > stage->maxKeyFrames)
        {
            stage->maxKeyFrames = keyFrames;
            stage->mutiplier = animation.fps / FIXED_FPS;
        }
        animation.keyFrames = keyFrames;
        stage->animationTracks.push_back(animation);
    }
}

const aiNodeAnim* AssimpImporter::FindNodeAnim(const aiAnimation* animation, const std::string& nodeName)
{
    for (size_t i = 0; i < animation->mNumChannels; i++)
    {
        const aiNodeAnim* nodeAnim = animation->mChannels[i];
        if (std::string(nodeAnim->mNodeName.data) == nodeName)
        {
            return nodeAnim;
        }
    }

    return nullptr;
}

int AssimpImporter::FindPosition(double animationTime, const aiNodeAnim* nodeAnim)
{
    for (size_t i = 1; i < nodeAnim->mNumPositionKeys; i++)
    {
        if (animationTime < nodeAnim->mPositionKeys[i].mTime)
        {
            return i - 1;
        }
    }

    return nodeAnim->mNumPositionKeys - 1;
}

int AssimpImporter::FindRotation(double animationTime, const aiNodeAnim* nodeAnim)
{
    for (size_t i = 1; i < nodeAnim->mNumRotationKeys; i++)
    {
        if (animationTime < nodeAnim->mRotationKeys[i].mTime)
        {
            return i - 1;
        }
    }

    return nodeAnim->mNumRotationKeys - 1;
}

int AssimpImporter::FindScaling(double animationTime, const aiNodeAnim* nodeAnim)
{
    for (size_t i = 1; i < nodeAnim->mNumScalingKeys; i++)
    {
        if (animationTime < nodeAnim->mScalingKeys[i].mTime)
        {
            return i - 1;
        }
    }

    return nodeAnim->mNumScalingKeys - 1;
}

TransformAnimationTracks AssimpImporter::GetNodeAnimation(
    const std::vector<AnimationTrack>& animTracks,
    const aiScene* assimpScene,
    const aiNode* node,
    double scale
)
{
    TransformAnimationTracks nodeAnimations;
    for (size_t i = 0; i < animTracks.size(); i++)
    {
        size_t keyFrames = animTracks[i].keyFrames;
        TransformTimesamples nodeAnimation = GetNodeAnimationFrames(node, animTracks[i].fps, keyFrames, assimpScene->mAnimations[i], scale);
        nodeAnimations.emplace_back(nodeAnimation);
    }

    return nodeAnimations;
}

bool AssimpImporter::isBoneNode(const aiNode* node)
{
    if (mRootBone && mRootBone == node)
    {
        return true;
    }

    // For BVH imports, imported nodes will all be bones since it's skeleton data only
    if (mThreadContext->converterContext.GetImportAssetType() == AssetType::BVH)
    {
        return true;
    }

    return false;
}


TransformTimesamples AssimpImporter::GetNodeAnimationFrames(const aiNode* node, size_t fps, size_t keyFrames, aiAnimation* animation, double scale)
{
    PXR_NS::VtVec3dArray t;
    PXR_NS::VtVec3dArray s;
    PXR_NS::VtQuatdArray q;

    std::string nodeName(node->mName.data);
    const aiNodeAnim* nodeAnim = FindNodeAnim(animation, nodeName);
    if (nodeAnim)
    {
        // Fixed 24fps per second.
        double factor = FIXED_FPS / fps;
        double frameTime = 1.0 / factor;
        for (size_t j = 0; j < keyFrames; j++)
        {
            double currentTime = j / factor;
            aiVector3D translation;
            aiVector3D scaling;
            aiQuaternion rotation;
            if (InterpolatedScaling(node, nodeAnim, currentTime, frameTime, scaling))
            {
                s.push_back(AiVector3dToGfVector3D(scaling));
            }

            if (InterpolatedPosition(node, nodeAnim, currentTime, frameTime, translation))
            {
                t.push_back(AiVector3dToGfVector3D(translation) * scale);
            }

            if (InterpolatedRotation(node, nodeAnim, currentTime, frameTime, rotation))
            {
                q.push_back(AiQuatenionToGfQuateniond(rotation));
            }
        }
    }

    return TransformTimesamples(t, q, s);
}

bool AssimpImporter::InterpolatedPosition(
    const aiNode* node,
    const aiNodeAnim* nodeAnim,
    double animationTime,
    double frameTime,
    aiVector3D& translation
)
{
    size_t numPositionKeys = nodeAnim->mNumPositionKeys;
    if (numPositionKeys <= 1 || animationTime >= nodeAnim->mPositionKeys[numPositionKeys - 1].mTime + frameTime)
    {
        return false;
    }

    if (animationTime < nodeAnim->mPositionKeys[0].mTime)
    {
        translation = nodeAnim->mPositionKeys[0].mValue;
    }

    int position = FindPosition(animationTime, nodeAnim);
    if (position == nodeAnim->mNumPositionKeys - 1)
    {
        translation = nodeAnim->mPositionKeys[position].mValue;
    }

    int nextPosition = position + 1;
    double deltaTime = nodeAnim->mPositionKeys[nextPosition].mTime - nodeAnim->mPositionKeys[position].mTime;
    float factor = float((animationTime - nodeAnim->mPositionKeys[position].mTime) / deltaTime);

    auto start = nodeAnim->mPositionKeys[position].mValue;
    auto end = nodeAnim->mPositionKeys[nextPosition].mValue;
    auto delta = end - start;

    translation = start + factor * delta;

    return true;
}

bool AssimpImporter::InterpolatedRotation(const aiNode* node, const aiNodeAnim* nodeAnim, double animationTime, double frameTime, aiQuaternion& orient)
{
    size_t numRotationKeys = nodeAnim->mNumRotationKeys;
    if (numRotationKeys <= 1 || animationTime >= nodeAnim->mRotationKeys[numRotationKeys - 1].mTime + frameTime)
    {
        return false;
    }

    if (animationTime < nodeAnim->mRotationKeys[0].mTime)
    {
        orient = nodeAnim->mRotationKeys[0].mValue;
    }

    int position = FindRotation(animationTime, nodeAnim);
    if (position == nodeAnim->mNumRotationKeys - 1)
    {
        orient = nodeAnim->mRotationKeys[position].mValue;
    }

    int nextPosition = position + 1;
    double deltaTime = nodeAnim->mRotationKeys[nextPosition].mTime - nodeAnim->mRotationKeys[position].mTime;
    float factor = float((animationTime - nodeAnim->mRotationKeys[position].mTime) / deltaTime);

    auto start = nodeAnim->mRotationKeys[position].mValue;
    auto end = nodeAnim->mRotationKeys[nextPosition].mValue;

    aiQuaternion::Interpolate(orient, start, end, factor);

    return true;
}

bool AssimpImporter::InterpolatedScaling(const aiNode* node, const aiNodeAnim* nodeAnim, double animationTime, double frameTime, aiVector3D& scale)
{
    size_t numScalingKeys = nodeAnim->mNumScalingKeys;
    if (numScalingKeys <= 1 || animationTime >= nodeAnim->mScalingKeys[numScalingKeys - 1].mTime + frameTime)
    {
        return false;
    }

    if (animationTime < nodeAnim->mScalingKeys[0].mTime)
    {
        scale = nodeAnim->mScalingKeys[0].mValue;
    }

    int position = FindScaling(animationTime, nodeAnim);
    if (position == nodeAnim->mNumScalingKeys - 1)
    {
        scale = nodeAnim->mScalingKeys[position].mValue;
    }

    int nextPosition = position + 1;
    double deltaTime = nodeAnim->mScalingKeys[nextPosition].mTime - nodeAnim->mScalingKeys[position].mTime;
    float factor = float((animationTime - nodeAnim->mScalingKeys[position].mTime) / deltaTime);

    auto start = nodeAnim->mScalingKeys[position].mValue;
    auto end = nodeAnim->mScalingKeys[nextPosition].mValue;
    auto delta = end - start;

    scale = start + factor * delta;

    return true;
}

aiQuaternion AssimpImporter::Nlerp(aiQuaternion qa, aiQuaternion qb, float blend)
{
    aiQuaternion result;
    aiQuaternion::Interpolate(result, qa, qb, blend);

    return result.Normalize();
}

size_t AssimpImporter::FindTexture(const StagePtr& stage, const std::string& path)
{
    for (size_t i = 0; i < stage->images.size(); i++)
    {
        if (path == stage->images[i]->originalPath)
        {
            return i;
        }
    }

    return -1;
}

std::vector<std::string> AssimpImporter::GetMaterialTexturePaths(aiMaterial* material, const aiTextureType& type)
{
    std::vector<std::string> textures;
    for (size_t i = 0; i < material->GetTextureCount(type); i++)
    {
        aiString aiPath;
        material->GetTexture(type, i, &aiPath);
        std::string filename(aiPath.C_Str());
        if (!filename.empty())
        {
            textures.push_back(filename);
        }
    }

    return textures;
}
