// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "obj_importer.h"

#include "../../utils/string_utils.h"
#include "../../utils/utils.h"

#include <algorithm>
#include <cmath>
#include <numeric>

class OmniConverterMaterialReader : public tinyobj::MaterialReader
{
public:

    OmniConverterMaterialReader(const OmniConverterContext& converterContext) : mContext(converterContext)
    {
    }

    virtual bool operator()(
        const std::string& matId,
        std::vector<tinyobj::material_t>* materials,
        std::map<std::string, int>* matMap,
        std::string* warn,
        std::string* err
    ) override
    {
        // FIXME: It's possible that mtl file has space in its name.
        // tinyobjloader will split the name according to space. It
        // needs to record all tokens until it has .mtl file extension.
        if (mLastToken.empty())
        {
            mLastToken = matId;
        }
        else
        {
            mLastToken += " " + matId;
        }

        if (StringUtils::ToLower(PathUtils::GetExtension(mLastToken)) != "mtl")
        {
            return false;
        }

        std::string mtlPath = mLastToken;
        mLastToken.clear();
        if (!PathUtils::IsAbsolutePath(mtlPath))
        {
            mtlPath = PathUtils::JoinPaths(mContext.GetImportAssetDir(), mtlPath);
        }

        auto mdlBlob = mContext.ReadFile(mtlPath);
        if (!mdlBlob)
        {
            std::string error = "Failed to read " + mtlPath;
            if (err)
            {
                *err = error;
            }
            mContext.Log(error);

            return false;
        }

        std::string mdlData((const char*)mdlBlob->buffer, mdlBlob->size);
        std::istringstream sourceStream(mdlData);

        tinyobj::LoadMtl(matMap, materials, &sourceStream, warn, err);

        return true;
    }

private:

    OmniConverterContext mContext;
    std::string mLastToken;
};

std::string ObjImporter::ComputeHash(const OmniFutureThreadContextPtr& context)
{
    return std::string();
}

void ObjImporter::NormalizeMaterialPaths(std::vector<tinyobj::material_t>& materials)
{
    for (auto& material : materials)
    {
        StringUtils::ReplaceAll(material.diffuse_texname, "\\\\", "/");
        StringUtils::ReplaceAll(material.specular_texname, "\\\\", "/");
        StringUtils::ReplaceAll(material.ambient_texname, "\\\\", "/");
        StringUtils::ReplaceAll(material.emissive_texname, "\\\\", "/");
        StringUtils::ReplaceAll(material.specular_highlight_texname, "\\\\", "/");
        StringUtils::ReplaceAll(material.bump_texname, "\\\\", "/");
        StringUtils::ReplaceAll(material.displacement_texname, "\\\\", "/");
        StringUtils::ReplaceAll(material.alpha_texname, "\\\\", "/");
        StringUtils::ReplaceAll(material.reflection_texname, "\\\\", "/");
        StringUtils::ReplaceAll(material.roughness_texname, "\\\\", "/");
        StringUtils::ReplaceAll(material.metallic_texname, "\\\\", "/");
        StringUtils::ReplaceAll(material.sheen_texname, "\\\\", "/");
        StringUtils::ReplaceAll(material.normal_texname, "\\\\", "/");

        // also replace single back slashes because diffuse texture path was resolving to @./textures/.\SpiderTex.jpg@ for example
        StringUtils::ReplaceAll(material.diffuse_texname, "\\", "/");
        StringUtils::ReplaceAll(material.specular_texname, "\\", "/");
        StringUtils::ReplaceAll(material.ambient_texname, "\\", "/");
        StringUtils::ReplaceAll(material.emissive_texname, "\\", "/");
        StringUtils::ReplaceAll(material.specular_highlight_texname, "\\", "/");
        StringUtils::ReplaceAll(material.bump_texname, "\\", "/");
        StringUtils::ReplaceAll(material.displacement_texname, "\\", "/");
        StringUtils::ReplaceAll(material.alpha_texname, "\\", "/");
        StringUtils::ReplaceAll(material.reflection_texname, "\\", "/");
        StringUtils::ReplaceAll(material.roughness_texname, "\\", "/");
        StringUtils::ReplaceAll(material.metallic_texname, "\\", "/");
        StringUtils::ReplaceAll(material.sheen_texname, "\\", "/");
        StringUtils::ReplaceAll(material.normal_texname, "\\", "/");
    }
}

StagePtr ObjImporter::ImportStage(const OmniFutureThreadContextPtr& context, OmniConverterStatus& status, std::string& detailedError)
{
    mThreadContext = context;

    Log("Starting to import asset with Obj importer.");
    status = OmniConverterStatus::OK;
    const std::string importAssetPath = mThreadContext->converterContext.GetImportAssetPath();
    auto objBlob = mThreadContext->converterContext.ReadFile(importAssetPath);
    if (!objBlob)
    {
        detailedError = "Failed to read " + importAssetPath + ".";
        Log(detailedError);
        status = OmniConverterStatus::FILE_NOT_EXISTED;
    }

    std::string objData((const char*)objBlob->buffer, objBlob->size);
    std::istringstream sourceStream(objData);

    OmniConverterMaterialReader materialReader(mThreadContext->converterContext);
    tinyobj::attrib_t attributes;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warning;
    std::string error;

    if (!tinyobj::LoadObj(&attributes, &shapes, &materials, &warning, &error, &sourceStream, &materialReader, false, false))
    {
        status = OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT;
        detailedError = "Failed to load asset " + importAssetPath + ", error: \"" + error + "\", warning: \"" + warning + "\".";
        Log(detailedError);
        return nullptr;
    }

    // Normalize material paths (e.g., convert backslashes to forward slashes)
    if (!materials.empty())
    {
        NormalizeMaterialPaths(materials);
    }

    auto stage = std::make_shared<Stage>();
    if (mThreadContext->converterContext.UseMeterPerUnit())
    {
        stage->worldUnits = 1.0;
    }
    stage->rootNode = std::make_shared<StageNode>("World");
    for (size_t i = 0; i < materials.size(); i++)
    {
        stage->materials.push_back(ToMaterial(stage, materials[i]));
    }

    // override up-axis with user-defined value
    if (mThreadContext->converterContext.ConvertUpZ())
    {
        stage->yAxis = false;
    }

    size_t numPoints = attributes.vertices.size() / 3;
    PXR_NS::VtVec3fArray allPoints(numPoints);
    for (size_t i = 0; i < numPoints; i++)
    {
        tinyobj::real_t vx = attributes.vertices[3 * i + 0];
        tinyobj::real_t vy = attributes.vertices[3 * i + 1];
        tinyobj::real_t vz = attributes.vertices[3 * i + 2];
        allPoints[i] = PXR_NS::GfVec3f(vx, vy, vz);
    }

    auto GetPoint = [&allPoints](const tinyobj::shape_t& shape, const tinyobj::attrib_t& attributes, size_t index)
    {
        tinyobj::index_t idx = shape.mesh.indices[index];
        return allPoints[idx.vertex_index];
    };

    struct MeshInfo
    {
        MeshPtr mesh;
        std::unordered_map<size_t, size_t> uniqueIndices;
        std::set<size_t> originalIndices;
        std::vector<size_t> originalFaceVertexIndices;
        bool hasUVs = false;
        bool hasNormals = false;
        bool hasColors = false;
    };

    size_t faceIndexOffset = 0;
    for (const auto& shape : shapes)
    {
        std::unordered_map<size_t, MeshInfo> subsetMeshes; // Group mesh with materials
        size_t numFaces = shape.mesh.num_face_vertices.size();
        size_t indexOffset = 0;
        for (size_t f = 0; f < numFaces; f++)
        {
            size_t materialId = shape.mesh.material_ids[f];
            if (materialId >= materials.size())
            {
                materialId = INVALID_MATERIAL_INDEX;
            }

            std::string shapeName;
            if (shape.name.empty())
            {
                shapeName = "mesh";
            }
            else
            {
                shapeName = shape.name;
            }

            auto iter = subsetMeshes.find(materialId);
            if (iter == subsetMeshes.end())
            {
                MeshPtr mesh = std::make_shared<Mesh>();
                mesh->uvs.resize(1);
                mesh->colors.resize(1);
                if (subsetMeshes.empty())
                {
                    mesh->name = shapeName;
                }
                else
                {
                    mesh->name = shapeName + std::to_string(subsetMeshes.size());
                }

                MeshInfo info;
                info.mesh = mesh;
                iter = subsetMeshes.insert({ materialId, info }).first;
            }

            auto& meshInfo = iter->second;
            if (!meshInfo.mesh)
            {
                MeshPtr mesh = std::make_shared<Mesh>();
                mesh->uvs.resize(1);
                mesh->colors.resize(1);
                if (subsetMeshes.empty())
                {
                    mesh->name = shapeName;
                }
                else
                {
                    mesh->name = shapeName + std::to_string(subsetMeshes.size());
                }
                meshInfo.mesh = mesh;
            }

            auto& mesh = meshInfo.mesh;
            auto& meshUniqueIndices = meshInfo.uniqueIndices;
            size_t fv = size_t(shape.mesh.num_face_vertices[f]);
            mesh->faceVertexCounts.push_back(fv);
            for (size_t v = 0; v < fv; v++)
            {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
                meshInfo.originalIndices.insert(idx.vertex_index);
                meshInfo.originalFaceVertexIndices.push_back(idx.vertex_index);

                if (idx.normal_index >= 0)
                {
                    tinyobj::real_t nx = attributes.normals[3 * size_t(idx.normal_index) + 0];
                    tinyobj::real_t ny = attributes.normals[3 * size_t(idx.normal_index) + 1];
                    tinyobj::real_t nz = attributes.normals[3 * size_t(idx.normal_index) + 2];
                    mesh->normals.push_back(PXR_NS::GfVec3f(nx, ny, nz));
                    meshInfo.hasNormals = true;
                }
                else // It's possible that part of the subset does not have normals
                {
                    auto v0 = GetPoint(shape, attributes, indexOffset);
                    auto v1 = GetPoint(shape, attributes, indexOffset + 1);
                    auto v2 = GetPoint(shape, attributes, indexOffset + 2);
                    auto normal = (v1 - v0) ^ (v2 - v0);
                    mesh->normals.push_back(normal);
                }

                if (idx.texcoord_index >= 0)
                {
                    tinyobj::real_t tx = attributes.texcoords[2 * size_t(idx.texcoord_index) + 0];
                    tinyobj::real_t ty = attributes.texcoords[2 * size_t(idx.texcoord_index) + 1];
                    mesh->uvs[0].push_back(PXR_NS::GfVec2f(tx, ty));
                    meshInfo.hasUVs = true;
                }
                else // It's possible that part of the subset does not have uvs
                {
                    mesh->uvs[0].push_back(PXR_NS::GfVec2f(0.5f, 0.5f));
                }

                if (attributes.colors.size() > 0)
                {
                    tinyobj::real_t r = attributes.colors[3 * size_t(idx.vertex_index) + 0];
                    tinyobj::real_t g = attributes.colors[3 * size_t(idx.vertex_index) + 1];
                    tinyobj::real_t b = attributes.colors[3 * size_t(idx.vertex_index) + 2];
                    mesh->colors[0].push_back(PXR_NS::GfVec3f(r, g, b));
                    meshInfo.hasColors = true;
                }
                else // It's possible that part of the subset does not have colors
                {
                    mesh->colors[0].push_back(PXR_NS::GfVec3f(0.0f));
                }
            }
            indexOffset += fv;
        }
        faceIndexOffset += numFaces;

        for (const auto& subsetMesh : subsetMeshes)
        {
            const auto& meshInfo = subsetMesh.second;

            // re-order the face vertex indices and add mesh point
            std::unordered_map<size_t, size_t> indiceMap;
            for (auto oriIndexIter = meshInfo.originalIndices.begin(); oriIndexIter != meshInfo.originalIndices.end(); oriIndexIter++)
            {
                if (*oriIndexIter < allPoints.size())
                {
                    meshInfo.mesh->points.push_back(allPoints[*oriIndexIter]);
                    indiceMap[*oriIndexIter] = meshInfo.mesh->points.size() - 1;
                }
            }

            for (auto oriFvIndexIter = meshInfo.originalFaceVertexIndices.begin(); oriFvIndexIter != meshInfo.originalFaceVertexIndices.end();
                 oriFvIndexIter++)
            {
                if (indiceMap.find(*oriFvIndexIter) != indiceMap.end())
                {
                    meshInfo.mesh->faceVertexIndices.push_back(indiceMap[*oriFvIndexIter]);
                }
            }

            if (!meshInfo.hasNormals)
            {
                meshInfo.mesh->normals.clear();
            }

            if (!meshInfo.hasUVs)
            {
                meshInfo.mesh->uvs.clear();
            }

            if (!meshInfo.hasColors)
            {
                meshInfo.mesh->colors.clear();
            }

            MeshGeomSubset subset;
            subset.materialIndex = subsetMesh.first;
            subset.name = meshInfo.mesh->name;
            subset.faceIndices.resize(meshInfo.mesh->faceVertexCounts.size());
            std::iota(subset.faceIndices.begin(), subset.faceIndices.end(), 0);
            meshInfo.mesh->meshSubsets.push_back(subset);
            stage->meshes.push_back(meshInfo.mesh);
        }
    }

    auto groupNode = std::make_shared<StageNode>(mThreadContext->converterContext.GetImportAssetFileName());
    groupNode->staticMeshInstances.resize(stage->meshes.size());
    std::iota(groupNode->staticMeshInstances.begin(), groupNode->staticMeshInstances.end(), 0);
    stage->rootNode->children.push_back(groupNode);

    return stage;
}

void ObjImporter::Log(const std::string& message)
{
    mThreadContext->converterContext.Log(message.c_str());
}

MaterialPtr ObjImporter::ToMaterial(const StagePtr& stage, const tinyobj::material_t& objMaterial)
{
    auto material = std::make_shared<Material>();
    material->name = objMaterial.name;
    auto diffuseColor = PXR_NS::GfVec3f(objMaterial.diffuse[0], objMaterial.diffuse[1], objMaterial.diffuse[2]);
    // Diffuse "Kd" default is 0.6 if Kd's value is missing from thne MTL file
    if (!PXR_NS::GfIsClose(diffuseColor, PXR_NS::GfVec3f(0.6), 1e-06))
    {
        material->diffuseColor = diffuseColor;
        material->hasDiffuseColor = true;
    }

    auto emissiveColor = PXR_NS::GfVec3f(objMaterial.emission[0], objMaterial.emission[1], objMaterial.emission[2]);
    if (!PXR_NS::GfIsClose(emissiveColor, PXR_NS::GfVec3f(0.0), 1e-06))
    {
        material->emissiveColor = emissiveColor;
        material->hasEmissiveColor = true;
    }
    material->opacity = objMaterial.dissolve;
    if (!PXR_NS::GfIsClose(material->opacity, 1.0, 1e-6))
    {
        material->hasOpacity = true;
    }

    FillTextureReference(stage, material, MaterialTextureType::DIFFUSE, objMaterial.diffuse_texname, objMaterial.diffuse_texopt);
    FillTextureReference(stage, material, MaterialTextureType::EMISSIVE, objMaterial.emissive_texname, objMaterial.emissive_texopt);
    FillTextureReference(stage, material, MaterialTextureType::OPACITY, objMaterial.alpha_texname, objMaterial.alpha_texopt);
    FillTextureReference(stage, material, MaterialTextureType::METALLIC, objMaterial.metallic_texname, objMaterial.metallic_texopt);
    FillTextureReference(stage, material, MaterialTextureType::ROUGHNESS, objMaterial.roughness_texname, objMaterial.roughness_texopt);
    FillTextureReference(stage, material, MaterialTextureType::NORMAL, objMaterial.bump_texname, objMaterial.normal_texopt);
    auto& diffuseTexture = material->GetTextureReference(MaterialTextureType::DIFFUSE);
    auto& opacityTexture = material->GetTextureReference(MaterialTextureType::OPACITY);
    bool diffuseHasAlpha = HasAlphaChannel(mThreadContext, stage, diffuseTexture.imageIndex);
    if (diffuseHasAlpha)
    {
        if (!opacityTexture.IsValid() && diffuseTexture.IsValid())
        {
            material->hasOpacity = true;
            TextureReference copyReference = diffuseTexture;
            copyReference.outputMode = TextureOutputMode::ALPHA;
            material->SetTextureReference(MaterialTextureType::OPACITY, copyReference);
        }
        else if (opacityTexture.imageIndex == diffuseTexture.imageIndex)
        {
            opacityTexture.outputMode = TextureOutputMode::ALPHA;
        }
    }

    return material;
}

std::string ObjImporter::ToAbsolutePath(const std::string& path)
{
    std::string absolutePath = path;
    if (!PathUtils::IsAbsolutePath(absolutePath))
    {
        absolutePath = PathUtils::JoinPaths(mThreadContext->converterContext.GetImportAssetDir(), absolutePath);
    }

    return absolutePath;
}

void ObjImporter::FillTextureReference(
    const StagePtr& stage,
    const MaterialPtr& material,
    MaterialTextureType textureType,
    const std::string& path,
    const tinyobj::texture_option_t& textureOption
)
{
    if (path.empty())
    {
        return;
    }

    const std::string& imagePath = ToAbsolutePath(path);
    auto iter = mTextureIndex.find(imagePath);
    size_t imageIndex = -1;
    if (iter == mTextureIndex.end())
    {
        TextureImagePtr image = std::make_shared<TextureImage>();
        image->originalPath = path;
        image->realPath = imagePath;
        stage->images.push_back(image);
        imageIndex = stage->images.size() - 1;
        mTextureIndex.insert({ imagePath, imageIndex });
    }
    else
    {
        imageIndex = iter->second;
    }

    auto& textureReference = material->GetTextureReference(textureType);
    textureReference.imageIndex = imageIndex;
    if (textureType == MaterialTextureType::OPACITY)
    {
        material->hasOpacity = true;
    }
}
