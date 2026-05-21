// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "fbx_exporter.h"

#include "../../common/common.h"
#include "../../common/custom_fbx_io.h"
#include "../../common/fbx_common.h"
#include "../../utils/utils.h"

#include <fbxsdk.h>
#include <numeric>

#define ENSURE_STATUS_OK(status)                                                                                                                     \
    {                                                                                                                                                \
        auto res = status;                                                                                                                           \
        if (res != OmniConverterStatus::OK)                                                                                                          \
        {                                                                                                                                            \
            return res;                                                                                                                              \
        }                                                                                                                                            \
    }

static fbxsdk::FbxVector4 GfVec3fToFbxVector4(const PXR_NS::GfVec3f& vec3f)
{
    return fbxsdk::FbxVector4(vec3f[0], vec3f[1], vec3f[2], 1.0f);
}

static fbxsdk::FbxVector4 GfVec3dToFbxVector4(const PXR_NS::GfVec3d& vec3d)
{
    return fbxsdk::FbxVector4(vec3d[0], vec3d[1], vec3d[2], 1.0);
}

static fbxsdk::FbxDouble3 GfVec3fToFbxDouble3(const PXR_NS::GfVec3f& vec3f)
{
    return fbxsdk::FbxDouble3(vec3f[0], vec3f[1], vec3f[2]);
}

static fbxsdk::FbxDouble3 GfVec3dToFbxDouble3(const PXR_NS::GfVec3d& vec3d)
{
    return fbxsdk::FbxDouble3(vec3d[0], vec3d[1], vec3d[2]);
}

static fbxsdk::FbxVector2 GfVec2fToFbxVector2(const PXR_NS::GfVec2f& vec2f)
{
    return fbxsdk::FbxVector2(vec2f[0], vec2f[1]);
}

static fbxsdk::FbxColor GfVec3fToFbxColor(const PXR_NS::GfVec3f& vec3f)
{
    return fbxsdk::FbxColor(vec3f[0], vec3f[1], vec3f[2]);
}

static fbxsdk::FbxAMatrix GfMatrixToFbxAMatrix(const PXR_NS::GfMatrix4d& matrix)
{
    fbxsdk::FbxAMatrix fbxAMatrix;
    fbxAMatrix.SetRow(0, fbxsdk::FbxVector4(matrix[0][0], matrix[0][1], matrix[0][2], matrix[0][3]));
    fbxAMatrix.SetRow(1, fbxsdk::FbxVector4(matrix[1][0], matrix[1][1], matrix[1][2], matrix[1][3]));
    fbxAMatrix.SetRow(2, fbxsdk::FbxVector4(matrix[2][0], matrix[2][1], matrix[2][2], matrix[2][3]));
    fbxAMatrix.SetRow(3, fbxsdk::FbxVector4(matrix[3][0], matrix[3][1], matrix[3][2], matrix[3][3]));

    return fbxAMatrix;
}

static fbxsdk::FbxDouble3 ToFbxDouble3(const OmniConverterMaterialProperty& property)
{
    return fbxsdk::FbxDouble3(property.value.double3Value[0], property.value.double2Value[1], property.value.double2Value[2]);
}

const static std::string MATERIAL_DIR_NAME = "materials";
const static std::string TEXTURE_DIR_NAME = "textures";

OmniConverterStatus FbxSdkExporter::Export(const StagePtr& stage, std::string& detailedError)
{
    auto status = OmniConverterStatus::OK;
    const std::string& basePath = mExportContext->converterContext.GetOutputAssetDir();
    const std::string& fileName = mExportContext->converterContext.GetOutputAssetFileName();

    // Prepare export dirs and options
    mMaterialsExportRoot = PathUtils::JoinPaths(basePath, MATERIAL_DIR_NAME);
    mTexturesExportRoot = PathUtils::JoinPaths(mMaterialsExportRoot, TEXTURE_DIR_NAME);

    Log("Starting to Export asset with FBX exporter.");

    uint32_t totalSteps = GetTotalExportSteps(stage);
    mExportContext->StartProgress(totalSteps);

    ENSURE_STATUS_OK(ExportTextures(stage));
    PreprocessStage(stage);

    std::lock_guard<std::mutex> lock(gFbxManagerLock);
    auto manager = FbxManagerCreator();
    auto ioSettings = FbxSettingsCreator(manager.get(), IOSROOT);
    manager->SetIOSettings(ioSettings.get());

    auto fbxScene = ToFbxScene(manager.get(), stage);
    if (!fbxScene || mExportContext->IsExited())
    {
        return OmniConverterStatus::CANCELLED;
    }

    FbxDocumentInfo* sceneInfo = FbxDocumentInfo::Create(manager.get(), "SceneInfo");
    sceneInfo->mTitle = "Omniverse Converter";
    sceneInfo->mSubject = "usd-convert-asset";
    sceneInfo->mAuthor = "NVIDIA Omniverse";
    sceneInfo->mRevision = "6.0";
    sceneInfo->mKeywords = "USD to FBX, Omniverse";
    sceneInfo->mComment = "This asset is converted from USD.";
    fbxScene->SetSceneInfo(sceneInfo);
    if (stage->yAxis)
    {
        fbxScene->GetGlobalSettings().SetAxisSystem(fbxsdk::FbxAxisSystem::MayaYUp);
    }
    else
    {
        fbxScene->GetGlobalSettings().SetAxisSystem(fbxsdk::FbxAxisSystem::MayaZUp);
    }
    fbxScene->GetGlobalSettings().SetSystemUnit(fbxsdk::FbxSystemUnit(stage->worldUnits * 100.0));

    auto child = fbxScene->GetRootNode()->GetChild(0);
    auto exporter = FbxExporterCreator(manager.get(), "");
    auto fbxBlob = createOmniConverterBlob(nullptr, 0);
    CustomFBXWriteStream writeStream(manager.get(), fbxBlob);
    if (mExportContext->converterContext.EmbeddingTextures())
    {
        ioSettings->SetBoolProp(EXP_FBX_EMBEDDED, true);
    }
    bool exportSuccess = exporter->Initialize(&writeStream, nullptr, -1, ioSettings.get());
    if (!exportSuccess)
    {
        status = OmniConverterStatus::UNSUPPORTED_IMPORT_FORMAT;
    }
    else
    {
        exportSuccess = exporter->Export(fbxScene.get());
    }

    if (!exportSuccess)
    {
        Log("Failed to export asset " + mExportContext->converterContext.GetImportAssetPath());
        status = OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT;
    }
    else
    {
        UploadContent(mExportContext->converterContext.GetOutputAssetPath(), fbxBlob.get());
    }

    fbxScene.reset();
    exporter.reset();
    ioSettings.reset();
    manager.reset();

    return status;
}

OmniConverterStatus FbxSdkExporter::ExportTextures(const StagePtr& stage)
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
        UploadTextureIfNotEmpty(texture);
        ClearInMemoryTexture(texture);
        mExportContext->IncrementProgress();
    }

    return OmniConverterStatus::OK;
}

OmniConverterStatus FbxSdkExporter::UploadTextureIfNotEmpty(const TextureImagePtr& texture)
{
    if (!texture)
    {
        return OmniConverterStatus::OK;
    }

    if (texture->blob)
    {
        const std::string& filename = PathUtils::GetFileName(texture->originalPath, true);
        const std::string& targetPath = PathUtils::JoinPaths(mTexturesExportRoot, filename);
        UploadContent(targetPath, texture->blob.get());
        mTextureUploadPath[texture] = targetPath;
    }
    else
    {
        UploadFileInternal(texture->realPath, mTexturesExportRoot);
        mTextureUploadPath[texture] = mUploadedFiles[texture->realPath];
    }

    return OmniConverterStatus::OK;
}

OmniConverterStatus FbxSdkExporter::UploadFileInternal(const std::string& filePath, const std::string& targetDir)
{
    auto iter = mUploadedFiles.find(filePath);
    if (iter == mUploadedFiles.end())
    {
        const std::string& fileName = PathUtils::GetFileName(filePath, true);
        const std::string& outFilePath = PathUtils::JoinPaths(targetDir, fileName);
        mUploadedFiles[filePath] = outFilePath;
        if (!mExportContext->converterContext.CopyFile(outFilePath, filePath))
        {
            std::string detailedError = "Failed to copy file " + filePath + " to " + outFilePath + ".";
            Log(detailedError);
            return OmniConverterStatus::CANCELLED;
        }
    }

    return OmniConverterStatus::OK;
}

fbxsdk::FbxSurfaceMaterial* FbxSdkExporter::ToFbxMaterial(const StagePtr& stage, fbxsdk::FbxScene* fbxScene, const MaterialPtr& material)
{
    auto fbxMaterial = fbxsdk::FbxSurfacePhong::Create(fbxScene, material->name.c_str());
    if (!material->fallback)
    {
        auto ConnectDoubleProperty = [this](
                                         fbxsdk::FbxScene* fbxScene,
                                         const OmniConverterMaterialProperty& inputProperty,
                                         const std::string& fbxPropertyName,
                                         fbxsdk::FbxPropertyT<fbxsdk::FbxDouble>& fbxProperty
                                     )
        {
            if (inputProperty.displayName == fbxPropertyName)
            {
                if (inputProperty.isTextureProperty)
                {
                    const auto& translation = inputProperty.textureTranslation;
                    const auto& scale = inputProperty.textureScale;
                    auto fbxTexture = ToFbxFileTexture(fbxScene, inputProperty.stringValue, translation[0], translation[1], scale[0], scale[1], true);
                    fbxProperty.ConnectSrcObject(fbxTexture);
                }
                else
                {
                    fbxProperty.Set(inputProperty.value.doubleValue);
                }
            }
        };

        auto ConnectDouble3Property = [this](
                                          fbxsdk::FbxScene* fbxScene,
                                          const OmniConverterMaterialProperty& inputProperty,
                                          const std::string& fbxPropertyName,
                                          fbxsdk::FbxPropertyT<fbxsdk::FbxDouble3>& fbxProperty
                                      )
        {
            if (inputProperty.displayName == fbxPropertyName)
            {
                if (inputProperty.isTextureProperty)
                {
                    const auto& translation = inputProperty.textureTranslation;
                    const auto& scale = inputProperty.textureScale;
                    auto fbxTexture = ToFbxFileTexture(fbxScene, inputProperty.stringValue, translation[0], translation[1], scale[0], scale[1], true);
                    fbxProperty.ConnectSrcObject(fbxTexture);
                }
                else
                {
                    fbxProperty.Set(ToFbxDouble3(inputProperty));
                }
            }
        };

        for (const auto& inputProperty : material->inputProperties)
        {
            if (inputProperty.valueType == OMNI_CONVERTER_VALUE_TYPE_UNDEFINED)
            {
                continue;
            }

            ConnectDouble3Property(fbxScene, inputProperty, fbxsdk::FbxSurfacePhong::sDiffuse, fbxMaterial->Diffuse);
            ConnectDoubleProperty(fbxScene, inputProperty, fbxsdk::FbxSurfacePhong::sDiffuseFactor, fbxMaterial->DiffuseFactor);
            ConnectDouble3Property(fbxScene, inputProperty, fbxsdk::FbxSurfacePhong::sEmissive, fbxMaterial->Emissive);
            ConnectDoubleProperty(fbxScene, inputProperty, fbxsdk::FbxSurfacePhong::sEmissiveFactor, fbxMaterial->EmissiveFactor);
            ConnectDouble3Property(fbxScene, inputProperty, fbxsdk::FbxSurfacePhong::sTransparentColor, fbxMaterial->TransparentColor);
            ConnectDoubleProperty(fbxScene, inputProperty, fbxsdk::FbxSurfacePhong::sTransparencyFactor, fbxMaterial->TransparencyFactor);
            ConnectDouble3Property(fbxScene, inputProperty, fbxsdk::FbxSurfacePhong::sBump, fbxMaterial->Bump);
            ConnectDoubleProperty(fbxScene, inputProperty, fbxsdk::FbxSurfacePhong::sBumpFactor, fbxMaterial->BumpFactor);
            ConnectDouble3Property(fbxScene, inputProperty, fbxsdk::FbxSurfacePhong::sAmbient, fbxMaterial->Ambient);
            ConnectDoubleProperty(fbxScene, inputProperty, fbxsdk::FbxSurfacePhong::sAmbientFactor, fbxMaterial->AmbientFactor);
            ConnectDouble3Property(fbxScene, inputProperty, fbxsdk::FbxSurfacePhong::sDisplacementColor, fbxMaterial->DisplacementColor);
            ConnectDoubleProperty(fbxScene, inputProperty, fbxsdk::FbxSurfacePhong::sDisplacementFactor, fbxMaterial->DisplacementFactor);
        }
    }
    else
    {
        if (material->hasDiffuseColor)
        {
            fbxMaterial->Diffuse.Set(GfVec3fToFbxDouble3(material->diffuseColor));
        }

        const auto& diffuse = material->GetTextureReference(MaterialTextureType::DIFFUSE);
        if (diffuse.IsValid())
        {
            auto diffuseTexture = ToFbxTexture(stage, fbxScene, diffuse);
            fbxMaterial->Diffuse.ConnectSrcObject(diffuseTexture);
        }

        if (material->hasEmissiveColor)
        {
            fbxMaterial->Emissive.Set(GfVec3fToFbxDouble3(material->emissiveColor));
        }

        const auto& emissive = material->GetTextureReference(MaterialTextureType::EMISSIVE);
        if (emissive.IsValid())
        {
            auto emissiveTexture = ToFbxTexture(stage, fbxScene, emissive);
            fbxMaterial->Emissive.ConnectSrcObject(emissiveTexture);
        }

        const auto& normal = material->GetTextureReference(MaterialTextureType::NORMAL);
        if (normal.IsValid())
        {
            auto normalTexture = ToFbxTexture(stage, fbxScene, normal);
            fbxMaterial->Bump.ConnectSrcObject(normalTexture);
        }

        if (material->hasOpacity)
        {
            float opacity;
            if (material->opacityThreshold != 0.0f)
            {
                opacity = material->opacity > material->opacityThreshold ? 1.0f : 0.0f;
            }
            else
            {
                opacity = 1.0f - material->opacity;
            }
            fbxMaterial->TransparentColor.Set(fbxsdk::FbxDouble3(opacity, opacity, opacity));
        }

        const auto& opacity = material->GetTextureReference(MaterialTextureType::OPACITY);
        if (opacity.IsValid())
        {
            auto opacityTexture = ToFbxTexture(stage, fbxScene, opacity);
            fbxMaterial->TransparentColor.ConnectSrcObject(opacityTexture);
        }
    }

    return fbxMaterial;
}

fbxsdk::FbxCamera* FbxSdkExporter::ToFbxCamera(fbxsdk::FbxScene* fbxScene, const CameraPtr& camera)
{
    auto fbxCamera = fbxsdk::FbxCamera::Create(fbxScene, camera->name.c_str());
    fbxCamera->ProjectionType.Set(fbxsdk::FbxCamera::ePerspective);
    fbxCamera->AspectRatioMode = fbxsdk::FbxCamera::eFixedRatio;
    fbxCamera->SetAspect(fbxsdk::FbxCamera::eFixedRatio, (float)camera->horizonalAperture / camera->verticallAperture, 1.0);
    fbxCamera->SetNearPlane(camera->clippingNear);
    fbxCamera->SetFarPlane(camera->clippingFar);
    fbxCamera->FocalLength.Set(camera->focalLength);
    fbxCamera->FocusDistance.Set(camera->focusDistance);
    fbxCamera->SetApertureMode(fbxsdk::FbxCamera::eHorizontal);
    fbxCamera->FieldOfView.Set(fbxCamera->ComputeFieldOfView(camera->focalLength));
    fbxCamera->SetApertureWidth(camera->horizonalAperture / 25.4); // In inches
    fbxCamera->SetApertureHeight(camera->verticallAperture / 25.4); // In inches
    fbxCamera->Position.Set(GfVec3dToFbxVector4(camera->position));
    fbxCamera->InterestPosition.Set(GfVec3dToFbxVector4(camera->lookAt));
    fbxCamera->UpVector.Set(GfVec3dToFbxVector4(camera->up));

    return fbxCamera;
}

fbxsdk::FbxMesh* FbxSdkExporter::ToFbxMesh(fbxsdk::FbxScene* fbxScene, const MeshPtr& mesh)
{
    auto fbxMesh = fbxsdk::FbxMesh::Create(fbxScene, mesh->name.c_str());
    fbxMesh->InitControlPoints(mesh->points.size());
    auto controlPoints = fbxMesh->GetControlPoints();
    for (size_t i = 0; i < mesh->points.size(); i++)
    {
        controlPoints[i] = GfVec3fToFbxVector4(mesh->points[i]);
    }

    std::vector<size_t> partialSums;
    std::partial_sum(mesh->faceVertexCounts.begin(), mesh->faceVertexCounts.end(), std::back_inserter(partialSums));

    fbxsdk::FbxGeometryElementMaterial* materialElement = fbxMesh->CreateElementMaterial();
    materialElement->SetMappingMode(FbxGeometryElement::eByPolygon);
    materialElement->SetReferenceMode(FbxGeometryElement::eIndexToDirect);

    fbxsdk::FbxGeometryElementNormal* normalElement = nullptr;
    if (!mesh->normals.empty())
    {
        normalElement = fbxMesh->CreateElementNormal();
        normalElement->SetMappingMode(fbxsdk::FbxGeometryElement::eByPolygonVertex);
        normalElement->SetReferenceMode(fbxsdk::FbxGeometryElement::eDirect);
    }

    fbxsdk::FbxGeometryElementVertexColor* colorElement = nullptr;
    if (mesh->colors.size() > 0)
    {
        colorElement = fbxMesh->CreateElementVertexColor();
        colorElement->SetMappingMode(FbxGeometryElement::eByPolygonVertex);
        colorElement->SetReferenceMode(FbxGeometryElement::eDirect);
    }

    int currentMaterialIndex = -1;
    for (const auto& geomSubset : mesh->meshSubsets)
    {
        int materialIndex;
        if (geomSubset.materialIndex != INVALID_MATERIAL_INDEX)
        {
            currentMaterialIndex += 1;
            materialIndex = currentMaterialIndex;
        }
        else
        {
            materialIndex = -1;
        }

        for (size_t faceIndex : geomSubset.faceIndices)
        {
            size_t faceVerticesCount = mesh->faceVertexCounts[faceIndex];
            size_t baseVertice = partialSums[faceIndex] - faceVerticesCount;
            fbxMesh->BeginPolygon(materialIndex);
            for (size_t j = 0; j < faceVerticesCount; j++)
            {
                fbxMesh->AddPolygon(mesh->faceVertexIndices[baseVertice + j]);
            }
            fbxMesh->EndPolygon();

            if (!mesh->normals.empty())
            {
                for (size_t j = 0; j < faceVerticesCount; j++)
                {
                    normalElement->GetDirectArray().Add(GfVec3fToFbxVector4(mesh->normals[baseVertice + j]));
                }
            }

            if (mesh->colors.size() > 0)
            {
                for (size_t j = 0; j < faceVerticesCount; j++)
                {
                    colorElement->GetDirectArray().Add(GfVec3fToFbxColor(mesh->colors[0][baseVertice + j]));
                }
            }
        }
    }

    for (size_t i = 0; i < mesh->uvs.size(); i++)
    {
        if (mesh->uvs[i].size() > 0)
        {
            const std::string& uvSetName = "uv" + std::to_string(i);
            fbxsdk::FbxGeometryElementUV* uvElement = fbxMesh->CreateElementUV(uvSetName.c_str());
            uvElement->SetMappingMode(FbxGeometryElement::eByPolygonVertex);
            if (mesh->uvIndices.size() > i && mesh->uvIndices[i].size() > 0)
            {
                uvElement->SetReferenceMode(FbxGeometryElement::eIndexToDirect);
                for (size_t j = 0; j < mesh->uvs[i].size(); j++)
                {
                    uvElement->GetDirectArray().Add(GfVec2fToFbxVector2(mesh->uvs[i][j]));
                }

                for (const auto& geomSubset : mesh->meshSubsets)
                {
                    for (size_t faceIndex : geomSubset.faceIndices)
                    {
                        size_t faceVerticesCount = mesh->faceVertexCounts[faceIndex];
                        size_t baseVertice = partialSums[faceIndex] - faceVerticesCount;

                        for (size_t j = 0; j < faceVerticesCount; j++)
                        {
                            if (baseVertice + j >= mesh->uvIndices[i].size())
                            {
                                break;
                            }

                            uvElement->GetIndexArray().Add(mesh->uvIndices[i][baseVertice + j]);
                        }
                    }
                }
            }
            else
            {
                uvElement->SetReferenceMode(FbxGeometryElement::eDirect);
                for (const auto& geomSubset : mesh->meshSubsets)
                {
                    for (size_t faceIndex : geomSubset.faceIndices)
                    {
                        size_t faceVerticesCount = mesh->faceVertexCounts[faceIndex];
                        size_t baseVertice = partialSums[faceIndex] - faceVerticesCount;
                        for (size_t j = 0; j < faceVerticesCount; j++)
                        {
                            if (baseVertice + j >= mesh->uvs[i].size())
                            {
                                break;
                            }

                            uvElement->GetDirectArray().Add(GfVec2fToFbxVector2(mesh->uvs[i][baseVertice + j]));
                        }
                    }
                }
            }
        }
    }

    if (!mExportContext->converterContext.IgnoreAnimations() && mesh->pointCacheTimesamples.size() > 0)
    {
        ToCacheAnimationFrames(fbxScene, fbxMesh, mesh->pointCacheTimesamples);
    }

    return fbxMesh;
}

bool FbxSdkExporter::ToCacheAnimationFrames(fbxsdk::FbxScene* fbxScene, fbxsdk::FbxMesh* fbxMesh, const PointCacheTimesamples& pointCacheAnimation)
{
    fbxsdk::FbxVertexCacheDeformer* deformer = fbxsdk::FbxVertexCacheDeformer::Create(fbxScene, fbxMesh->GetName());
    fbxMesh->AddDeformer(deformer);

    fbxsdk::FbxCache* cache = fbxsdk::FbxCache::Create(fbxScene, fbxMesh->GetName());
    unsigned int vertexCount = (unsigned int)fbxMesh->GetControlPointsCount();
    fbxsdk::FbxStatus fbxStatus;

    std::string relativePath;
    const std::string& exportAssetDir = mExportContext->converterContext.GetOutputAssetDir();
    const std::string& exportAssetPath = mExportContext->converterContext.GetOutputAssetPath();
    // TODO there should be .pc2 format for 3dmax point cache
    std::string absolutePath = PathUtils::JoinPaths(exportAssetDir, std::string(fbxMesh->GetName()) + std::string(".xml"));
    PathUtils::ComputeRelativePath(absolutePath, exportAssetPath, relativePath);


    fbxsdk::FbxString rp(relativePath.c_str());
    fbxsdk::FbxString ap(absolutePath.c_str());

    cache->SetCacheFileName(rp, ap);
    // TODO: there are three cache format, how to support them?
    cache->SetCacheFileFormat(fbxsdk::FbxCache::eMayaCache);

    deformer->SetCache(cache);
    deformer->Channel = fbxMesh->GetName();
    deformer->Active = true;

    auto frameRate = fbxsdk::FbxTime::GetFrameRate(fbxScene->GetGlobalSettings().GetTimeMode());
    if (!cache->OpenFileForWrite(
            fbxsdk::FbxCache::eMCOneFile,
            frameRate,
            fbxMesh->GetName(),
            fbxsdk::FbxCache::eMCX,
            fbxsdk::FbxCache::eFloatVectorArray,
            "Points",
            &fbxStatus
        ))
    // TODO: there is how to open .pc2 format for 3dmax point cache
    // if (!cache->OpenFileForWrite(0, frameRate, pointCacheAnimation.size(), vertexCount, &fbxStatus))
    {
        return false;
    }
    unsigned int channelIndex = cache->GetChannelIndex(fbxMesh->GetName());


    auto timePerFrame = 1.0f / frameRate;
    auto numFrames = pointCacheAnimation.size();
    fbxsdk::FbxTime start = 0;
    fbxsdk::FbxTime end = numFrames;
    fbxsdk::FbxTimeSpan timeSpan(start, end);

    if (numFrames > 0)
    {
        fbxsdk::FbxTime step;
        step.SetTime(0, 0, 0, 1);
        fbxsdk::FbxTime currentTime = start;
        for (size_t i = 0; i < numFrames; i++)
        {
            std::vector<double> pointBuffer;
            for (size_t j = 0; j < pointCacheAnimation[i].size(); j++)
            {
                pointBuffer.push_back(pointCacheAnimation[i][j][0]);
                pointBuffer.push_back(pointCacheAnimation[i][j][1]);
                pointBuffer.push_back(pointCacheAnimation[i][j][2]);
            }

            // TODO: there is how to write .pc2 format for 3dmax point cache
            // cache->Write(i, pointBuffer.data(), &fbxStatus);

            cache->BeginWriteAt(currentTime);
            cache->Write(channelIndex, currentTime, pointBuffer.data(), vertexCount);
            cache->EndWriteAt();

            currentTime += step;
        }
    }

    cache->CloseFile();
    return true;
}

std::shared_ptr<fbxsdk::FbxScene> FbxSdkExporter::ToFbxScene(fbxsdk::FbxManager* fbxManager, const StagePtr& stage)
{
    auto fbxScene = FbxSceneCreator(fbxManager, "Scene");
    // Create all anim stacks first
    for (size_t i = 0; i < stage->animationTracks.size(); i++)
    {
        auto animStack = fbxsdk::FbxAnimStack::Create(fbxScene.get(), stage->animationTracks[i].name.c_str());
        mAllAnimStacks.push_back(animStack);
        auto animLayer = fbxsdk::FbxAnimLayer::Create(fbxScene.get(), "Base Layer");
        animStack->AddMember(animLayer);
        mAllAnimLayers.push_back(animLayer);
    }

    for (size_t i = 0; i < stage->materials.size(); i++)
    {
        mAllMaterials.push_back(ToFbxMaterial(stage, fbxScene.get(), stage->materials[i]));
    }

    for (const auto& mesh : stage->meshes)
    {
        if (mExportContext->IsExited())
        {
            return nullptr;
        }

        mExportContext->IncrementProgress();
        mMeshes[mesh] = ToFbxMesh(fbxScene.get(), mesh);
    }

    if (stage->rootNode->localTransform.IsIdentity() && !mStageNodeInfos[stage->rootNode])
    {
        for (const auto& childNode : stage->rootNode->children)
        {
            PopulateStageNodeTree(stage, childNode, fbxScene.get(), fbxScene->GetRootNode());
        }
    }
    else
    {
        PopulateStageNodeTree(stage, stage->rootNode, fbxScene.get(), fbxScene->GetRootNode());
    }

    // Attaches all skinned meshes under root node.
    auto skinnedMeshesNode = CreateSkinnedMeshes(stage, fbxScene.get());
    fbxScene->GetRootNode()->AddChild(skinnedMeshesNode);

    // Creates deformer
    for (const auto& skinMesh : stage->skinMeshes)
    {
        if (mExportContext->IsExited())
        {
            return nullptr;
        }

        const auto& mesh = stage->meshes[skinMesh->meshIndex];
        const auto& fbxMesh = mMeshes[mesh];
        FbxSkin* meshSkin = FbxSkin::Create(fbxScene.get(), (mesh->name + "Skin").c_str());
        for (size_t i = 0; i < skinMesh->influencedBoneNodes.size(); i++)
        {
            auto bone = skinMesh->influencedBoneNodes[i];
            auto boneNode = mBoneNodes[bone];

            fbxsdk::FbxCluster* cluster = fbxsdk::FbxCluster::Create(fbxScene.get(), (bone->name + "Cluster").c_str());
            cluster->SetLink(boneNode);
            cluster->SetLinkMode(FbxCluster::eNormalize);

            std::vector<size_t> infuencedVertexIndices;
            std::vector<float> infuencedVertexWeights;
            for (size_t j = 0; j < skinMesh->jointInfluences.size(); j += skinMesh->numBoneInfluencesPerVertex)
            {
                bool influenced = false;
                float weight = 0.0f;
                for (size_t k = j; k < j + skinMesh->numBoneInfluencesPerVertex; k++)
                {
                    if (skinMesh->jointInfluences[k] == i)
                    {
                        influenced = true;

                        // Accumulates weight since it's possible that it includes
                        // the same bone multiple times.
                        weight += skinMesh->jointWeights[k];
                    }
                }

                if (influenced && weight != 0.0f)
                {
                    infuencedVertexIndices.push_back(j / skinMesh->numBoneInfluencesPerVertex);
                    infuencedVertexWeights.push_back(weight);
                }
            }

            for (size_t j = 0; j < infuencedVertexIndices.size(); j++)
            {
                cluster->AddControlPointIndex(infuencedVertexIndices[j], infuencedVertexWeights[j]);
            }

            cluster->SetTransformLinkMatrix(GfMatrixToFbxAMatrix(bone->bindTransform));
            cluster->SetTransformMatrix(GfMatrixToFbxAMatrix(skinMesh->geomBindTransform));
            meshSkin->AddCluster(cluster);
        }
        fbxMesh->AddDeformer(meshSkin);
    }

    return fbxScene;
}

void FbxSdkExporter::PopulateStageNodeTree(
    const StagePtr& stage,
    const StageNodePtr& currentNode,
    fbxsdk::FbxScene* fbxScene,
    fbxsdk::FbxNode* parentFbxNode
)
{
    bool hasProps = mStageNodeInfos[currentNode];
    if (!hasProps)
    {
        return;
    }

    fbxsdk::FbxNode* node = nullptr;
    size_t objectsCount = currentNode->staticMeshInstances.size() + currentNode->cameras.size() + currentNode->lights.size();
    if (currentNode->isBoneNode)
    {
        objectsCount += 1;
    }

    if (objectsCount > 1 || objectsCount == 0)
    {
        node = fbxsdk::FbxNode::Create(fbxScene, currentNode->name.c_str());
        std::unordered_map<std::string, size_t> instancesCount;
        for (size_t i = 0; i < currentNode->staticMeshInstances.size(); i++)
        {
            const auto& mesh = stage->meshes[currentNode->staticMeshInstances[i]];
            const auto& meshName = mesh->name;
            std::string suffix;
            auto iter = instancesCount.find(meshName);
            if (iter != instancesCount.end())
            {
                suffix = "_instance" + std::to_string(iter->second);
                iter->second += 1;
            }
            else
            {
                suffix = "";
                instancesCount.insert({ meshName, 1 });
            }

            node->AddChild(CreateMesh(mesh, (meshName + suffix).c_str(), fbxScene));
        }

        for (size_t i = 0; i < currentNode->cameras.size(); i++)
        {
            const auto& camera = stage->cameras[i];
            node->AddChild(CreateCamera(camera, camera->name, fbxScene));
        }

        for (size_t i = 0; i < currentNode->lights.size(); i++)
        {
            const auto& light = stage->lights[i];
            node->AddChild(CreateLight(light, light->name, fbxScene));
        }

        if (currentNode->isBoneNode)
        {
            node->AddChild(CreateSkeleton(stage, currentNode, fbxScene));
        }
    }
    else // If it has only one attached object, don't create extra node.
    {
        if (currentNode->staticMeshInstances.size() > 0)
        {
            const auto& mesh = stage->meshes[currentNode->staticMeshInstances[0]];
            node = CreateMesh(mesh, currentNode->name, fbxScene);
        }

        if (currentNode->cameras.size() > 0)
        {
            const auto& camera = stage->cameras[currentNode->cameras[0]];
            node = CreateCamera(camera, currentNode->name, fbxScene);
        }

        if (currentNode->lights.size() > 0)
        {
            const auto& light = stage->lights[currentNode->lights[0]];
            node = CreateLight(light, currentNode->name, fbxScene);
        }

        if (currentNode->isBoneNode)
        {
            node = CreateSkeleton(stage, currentNode, fbxScene);
        }
    }

    SetDefaultTransform(node, currentNode->localTransform);
    for (size_t i = 0; i < stage->animationTracks.size(); i++)
    {
        if (i < currentNode->transformAnimationTracks.size())
        {
            auto animLayer = mAllAnimLayers[i];
            SetNodeTRSSamples(stage, node, mAllAnimLayers[i], currentNode->transformAnimationTracks[i]);
        }
    }
    parentFbxNode->AddChild(node);

    for (size_t i = 0; i < currentNode->children.size(); i++)
    {
        PopulateStageNodeTree(stage, currentNode->children[i], fbxScene, node);
    }
}

void FbxSdkExporter::PreprocessAllNodes(const StagePtr& stage, const StageNodePtr& node)
{
    if (!node)
    {
        return;
    }

    bool hasProps = node->cameras.size() > 0 || node->staticMeshInstances.size() > 0 || node->lights.size() > 0 || node->isBoneNode;
    for (const auto& child : node->children)
    {
        PreprocessAllNodes(stage, child);
        bool& childNodeHasProps = mStageNodeInfos[child];
        hasProps = hasProps || childNodeHasProps;
    }

    mStageNodeInfos[node] = hasProps;
}

void FbxSdkExporter::PreprocessStage(const StagePtr& stage)
{
    PreprocessAllNodes(stage, stage->rootNode);
}

void FbxSdkExporter::SetDefaultTransform(fbxsdk::FbxNode* node, const Transform& transform)
{
    const auto& trs = transform.GetTES();
    const auto& pivot = transform.GetPivot();
    node->LclTranslation.Set(GfVec3dToFbxDouble3(trs.t));
    node->LclScaling.Set(GfVec3dToFbxDouble3(trs.s));
    node->LclRotation.Set(GfVec3dToFbxDouble3(trs.r));
    if (pivot != ZERO_VEC_3D)
    {
        node->SetRotationPivot(fbxsdk::FbxNode::eSourcePivot, GfVec3dToFbxDouble3(pivot));
        node->SetScalingPivot(fbxsdk::FbxNode::eSourcePivot, GfVec3dToFbxDouble3(pivot));
    }
}

void FbxSdkExporter::SetDefaultMatrix(fbxsdk::FbxNode* node, const PXR_NS::GfMatrix4d& transform)
{
    const auto& trs = MathUtils::GfMatrixToTRS(transform);
    node->LclTranslation.Set(GfVec3dToFbxDouble3(trs.t));
    node->LclScaling.Set(GfVec3dToFbxDouble3(trs.s));
    node->LclRotation.Set(GfVec3dToFbxDouble3(trs.r));
}

void FbxSdkExporter::SetTimeSampledTranslation(
    fbxsdk::FbxNode* node,
    fbxsdk::FbxAnimLayer* animLayer,
    fbxsdk::FbxTime time,
    const PXR_NS::GfVec3d& sample
)
{
    auto animTranslationXCurve = node->LclTranslation.GetCurve(animLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
    auto animTranslationYCurve = node->LclTranslation.GetCurve(animLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
    auto animTranslationZCurve = node->LclTranslation.GetCurve(animLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);

    fbxsdk::FbxAnimCurve* translationCurves[3] = { animTranslationXCurve, animTranslationYCurve, animTranslationZCurve };
    for (size_t i = 0; i < 3; i++)
    {
        translationCurves[i]->KeyModifyBegin();
        auto keyIndex = translationCurves[i]->KeyAdd(time);
        translationCurves[i]->KeySetTime(keyIndex, time);
        translationCurves[i]->KeySetValue(keyIndex, sample[i]);
        translationCurves[i]->KeySetInterpolation(keyIndex, fbxsdk::FbxAnimCurveDef::eInterpolationConstant);
        translationCurves[i]->KeyModifyEnd();
    }
}

void FbxSdkExporter::SetTimeSampledScale(fbxsdk::FbxNode* node, fbxsdk::FbxAnimLayer* animLayer, fbxsdk::FbxTime time, const PXR_NS::GfVec3d& sample)
{
    auto animScaleXCurve = node->LclScaling.GetCurve(animLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
    auto animScaleYCurve = node->LclScaling.GetCurve(animLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
    auto animScaleZCurve = node->LclScaling.GetCurve(animLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);

    fbxsdk::FbxAnimCurve* scaleCurves[3] = { animScaleXCurve, animScaleYCurve, animScaleZCurve };

    for (size_t i = 0; i < 3; i++)
    {
        scaleCurves[i]->KeyModifyBegin();
        auto keyIndex = scaleCurves[i]->KeyAdd(time);
        scaleCurves[i]->KeySetTime(keyIndex, time);
        scaleCurves[i]->KeySetValue(keyIndex, sample[i]);
        scaleCurves[i]->KeySetInterpolation(keyIndex, fbxsdk::FbxAnimCurveDef::eInterpolationConstant);
        scaleCurves[i]->KeyModifyEnd();
    }
}

void FbxSdkExporter::SetTimeSampledRotationXYZ(
    fbxsdk::FbxNode* node,
    fbxsdk::FbxAnimLayer* animLayer,
    fbxsdk::FbxTime time,
    const PXR_NS::GfVec3d& sample
)
{
    auto animRotationXCurve = node->LclRotation.GetCurve(animLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
    auto animRotationYCurve = node->LclRotation.GetCurve(animLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
    auto animRotationZCurve = node->LclRotation.GetCurve(animLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);

    fbxsdk::FbxAnimCurve* rotationCurves[3] = { animRotationXCurve, animRotationYCurve, animRotationZCurve };

    for (size_t i = 0; i < 3; i++)
    {
        rotationCurves[i]->KeyModifyBegin();
        auto keyIndex = rotationCurves[i]->KeyAdd(time);
        rotationCurves[i]->KeySetTime(keyIndex, time);
        rotationCurves[i]->KeySetValue(keyIndex, sample[i]);
        rotationCurves[i]->KeySetInterpolation(keyIndex, fbxsdk::FbxAnimCurveDef::eInterpolationConstant);
        rotationCurves[i]->KeyModifyEnd();
    }
}

void FbxSdkExporter::SetNodeTRSSamples(
    const StagePtr& stage,
    fbxsdk::FbxNode* node,
    fbxsdk::FbxAnimLayer* animLayer,
    const TransformTimesamples& transformTimesamples
)
{
    const auto& translations = transformTimesamples.GetTranslationSamples();
    const auto& scalings = transformTimesamples.GetScaleSamples();
    const auto& rotationXYZs = transformTimesamples.GetRotationXYZSamples();
    for (size_t j = 0; j < translations.size(); j++)
    {
        fbxsdk::FbxTime time;
        time.SetFrame(j); // FIXME: Currently, it assumes 24 frames/s.
        SetTimeSampledTranslation(node, animLayer, time, translations[j]);
    }

    for (size_t j = 0; j < scalings.size(); j++)
    {
        fbxsdk::FbxTime time;
        time.SetFrame(j); // FIXME: Currently, it assumes 24 frames/s.
        SetTimeSampledScale(node, animLayer, time, scalings[j]);
    }

    for (size_t j = 0; j < rotationXYZs.size(); j++)
    {
        fbxsdk::FbxTime time;
        time.SetFrame(j); // FIXME: Currently, it assumes 24 frames/s.
        SetTimeSampledRotationXYZ(node, animLayer, time, rotationXYZs[j]);
    }
}

fbxsdk::FbxNode* FbxSdkExporter::CreateSkeleton(const StagePtr& stage, const StageNodePtr& boneNode, fbxsdk::FbxScene* fbxScene)
{
    fbxsdk::FbxString boneName(boneNode->name.c_str());
    fbxsdk::FbxSkeleton* skeletonAttribute = fbxsdk::FbxSkeleton::Create(fbxScene, boneName);
    if (boneNode->IsRootBone())
    {
        skeletonAttribute->SetSkeletonType(fbxsdk::FbxSkeleton::eRoot);
    }
    else if (boneNode->children.size() != 0)
    {
        skeletonAttribute->SetSkeletonType(fbxsdk::FbxSkeleton::eLimb);
    }
    else
    {
        // eEffector is synonymous to eRoot and it will not show as a bone in Maya.
        // So we should use eLimbNode here for the end bone node.
        skeletonAttribute->SetSkeletonType(fbxsdk::FbxSkeleton::eLimbNode);
    }

    fbxsdk::FbxNode* skeletonRoot = fbxsdk::FbxNode::Create(fbxScene, boneName);
    skeletonRoot->SetNodeAttribute(skeletonAttribute);
    mBoneNodes[boneNode] = skeletonRoot;

    return skeletonRoot;
}

fbxsdk::FbxNode* FbxSdkExporter::CreateMesh(const MeshPtr& mesh, const std::string& name, fbxsdk::FbxScene* fbxScene)
{
    auto meshNode = fbxsdk::FbxNode::Create(fbxScene, name.c_str());
    auto fbxMesh = mMeshes[mesh];
    meshNode->SetNodeAttribute(fbxMesh);
    for (const auto& geomSubset : mesh->meshSubsets)
    {
        if (geomSubset.materialIndex != INVALID_MATERIAL_INDEX)
        {
            meshNode->AddMaterial(mAllMaterials[geomSubset.materialIndex]);
        }
    }

    return meshNode;
}

fbxsdk::FbxNode* FbxSdkExporter::CreateCamera(const CameraPtr& camera, const std::string& name, fbxsdk::FbxScene* fbxScene)
{
    auto fbxCamera = ToFbxCamera(fbxScene, camera);
    auto cameraNode = fbxsdk::FbxNode::Create(fbxScene, name.c_str());
    fbxCamera->InterestPosition.Set(GfVec3dToFbxVector4(camera->lookAt));
    fbxCamera->Position.Set(GfVec3dToFbxVector4(camera->position));
    fbxCamera->UpVector.Set(GfVec3dToFbxVector4(camera->up));
    cameraNode->SetNodeAttribute(fbxCamera);

    return cameraNode;
}

fbxsdk::FbxNode* FbxSdkExporter::CreateLight(const LightPtr& light, const std::string& name, fbxsdk::FbxScene* fbxScene)
{
    auto fbxLight = fbxsdk::FbxLight::Create(fbxScene, light->name.c_str());
    auto lightNode = fbxsdk::FbxNode::Create(fbxScene, name.c_str());
    fbxLight->Color.Set(GfVec3fToFbxDouble3(light->color));
    fbxLight->OuterAngle.Set(light->outAngle);
    fbxLight->InnerAngle.Set(light->innerAngle);
    fbxLight->Intensity.Set(light->intensity);

    switch (light->type)
    {
        case LightType::POINT:
        {
            fbxLight->LightType.Set(fbxsdk::FbxLight::ePoint);
            break;
        }
        case LightType::SPHERE:
        {
            fbxLight->LightType.Set(fbxsdk::FbxLight::eArea);
            fbxLight->AreaLightShape.Set(fbxsdk::FbxLight::eSphere);
            break;
        }
        case LightType::DISTANT:
        {
            fbxLight->LightType.Set(fbxsdk::FbxLight::eDirectional);
            break;
        }
        case LightType::RECT:
        {
            fbxLight->LightType.Set(fbxsdk::FbxLight::eArea);
            break;
        }
        default:
            fbxLight->LightType.Set(fbxsdk::FbxLight::eVolume);
            break;
    }

    lightNode->SetNodeAttribute(fbxLight);
    return lightNode;
}


fbxsdk::FbxNode* FbxSdkExporter::CreateSkinnedMeshes(const StagePtr& stage, fbxsdk::FbxScene* fbxScene)
{
    auto skinnedMeshesNode = fbxsdk::FbxNode::Create(fbxScene, "SkinnedMeshes");
    for (const auto& skinMesh : stage->skinMeshes)
    {
        const auto& mesh = stage->meshes[skinMesh->meshIndex];
        auto meshNode = CreateMesh(mesh, mesh->name, fbxScene);
        SetDefaultMatrix(meshNode, skinMesh->geomBindTransform);
        skinnedMeshesNode->AddChild(meshNode);
    }

    return skinnedMeshesNode;
}

fbxsdk::FbxTexture* FbxSdkExporter::ToFbxTexture(const StagePtr& stage, fbxsdk::FbxScene* fbxScene, const TextureReference& textureReference)
{
    TextureImagePtr texture = stage->images[textureReference.imageIndex];
    const std::string& targetTexturepath = mTextureUploadPath[texture];
    auto fbxFileTexture = ToFbxFileTexture(
        fbxScene,
        targetTexturepath,
        textureReference.transform.translation[0],
        textureReference.transform.translation[1],
        textureReference.transform.scale[0],
        textureReference.transform.scale[1],
        textureReference.outputMode == TextureOutputMode::AVERAGE
    );
    fbxFileTexture
        ->SetRotation(textureReference.transform.rotation[0], textureReference.transform.rotation[1], textureReference.transform.rotation[2]);

    return fbxFileTexture;
}

fbxsdk::FbxFileTexture* FbxSdkExporter::ToFbxFileTexture(
    fbxsdk::FbxScene* fbxScene,
    const std::string& texturePath,
    double tx,
    double ty,
    double sx,
    double sy,
    bool monoAverage
)
{
    const std::string& textureName = PathUtils::GetFileName(texturePath);
    auto fbxTexture = fbxsdk::FbxFileTexture::Create(fbxScene, textureName.c_str());
    fbxTexture->SetFileName(texturePath.c_str());
    std::string relativePath;
    PathUtils::ComputeRelativePath(texturePath, mExportContext->converterContext.GetOutputAssetPath(), relativePath);
    fbxTexture->SetRelativeFileName(relativePath.c_str());
    fbxTexture->SetTextureUse(fbxsdk::FbxTexture::eStandard);
    fbxTexture->SetMappingType(fbxsdk::FbxTexture::eUV);
    if (monoAverage)
    {
        fbxTexture->SetAlphaSource(fbxsdk::FbxTexture::eRGBIntensity);
    }
    else
    {
        fbxTexture->SetAlphaSource(fbxsdk::FbxTexture::eBlack);
    }
    fbxTexture->SetScale(sx, sy);
    fbxTexture->SetTranslation(tx, ty);

    return fbxTexture;
}
