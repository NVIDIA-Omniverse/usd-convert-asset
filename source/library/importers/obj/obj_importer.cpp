// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "obj_importer.h"

#include "../../utils/string_utils.h"
#include "../../utils/utils.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <string_view>

namespace
{
// tinyobjloader's tryParseDouble() only accepts digit-based floats. Tokens like
// "nan" / "inf" fail parse and silently fall back to 0.0, so a post-LoadObj
// isfinite() check never sees them. Scan raw vertex-attribute lines with
// strtod(), which does recognize non-finite literals.
struct ObjNonFiniteAttributes
{
    std::vector<bool> vertices;
    std::vector<bool> normals;
    std::vector<bool> texcoords;
};

ObjNonFiniteAttributes FindObjNonFiniteAttributes(std::string_view objData)
{
    ObjNonFiniteAttributes result;
    auto isSpace = [](char c)
    {
        return c == ' ' || c == '\t';
    };

    size_t offset = 0;
    while (offset < objData.size())
    {
        const size_t lineEnd = objData.find_first_of("\r\n", offset);
        std::string_view line = lineEnd == std::string_view::npos ? objData.substr(offset) : objData.substr(offset, lineEnd - offset);
        if (lineEnd == std::string_view::npos)
        {
            offset = objData.size();
        }
        else if (objData[lineEnd] == '\r' && lineEnd + 1 < objData.size() && objData[lineEnd + 1] == '\n')
        {
            offset = lineEnd + 2;
        }
        else
        {
            offset = lineEnd + 1;
        }

        size_t cursor = 0;
        while (cursor < line.size() && isSpace(line[cursor]))
        {
            ++cursor;
        }
        if (cursor >= line.size() || line[cursor] == '#')
        {
            continue;
        }

        // Match "v ", "vn ", or "vt " attribute lines only.
        const char first = line[cursor];
        if (first != 'v')
        {
            continue;
        }

        size_t keywordEnd = cursor + 1;
        if (keywordEnd < line.size() && (line[keywordEnd] == 'n' || line[keywordEnd] == 't'))
        {
            ++keywordEnd;
        }
        if (keywordEnd >= line.size() || !isSpace(line[keywordEnd]))
        {
            // Reject "usemtl", "vp", etc., and bare "v"/"vn"/"vt" with no values.
            continue;
        }

        // Copy the remainder so strtod can see a NUL-terminated buffer.
        const std::string values(line.substr(keywordEnd));
        const char* token = values.c_str();
        bool nonFinite = false;
        while (*token != '\0')
        {
            while (isSpace(*token))
            {
                ++token;
            }
            if (*token == '\0' || *token == '#')
            {
                break;
            }

            char* parseEnd = nullptr;
            errno = 0;
            const double value = std::strtod(token, &parseEnd);
            if (parseEnd == token)
            {
                // Not a float token; skip to next whitespace-separated token.
                while (*token != '\0' && !isSpace(*token) && *token != '#')
                {
                    ++token;
                }
                continue;
            }

            if (errno == ERANGE || !std::isfinite(value))
            {
                nonFinite = true;
            }
            token = parseEnd;
        }
        auto& flags = keywordEnd == cursor + 1 ? result.vertices : (line[cursor + 1] == 'n' ? result.normals : result.texcoords);
        flags.push_back(nonFinite);
    }

    return result;
}
} // namespace

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
        return nullptr;
    }

    if (!objBlob->buffer || objBlob->size == 0)
    {
        detailedError = "Asset " + importAssetPath + " is empty.";
        Log(detailedError);
        status = OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT;
        return nullptr;
    }

    std::string objData((const char*)objBlob->buffer, objBlob->size);
    const auto nonFiniteAttributes = FindObjNonFiniteAttributes(objData);
    const auto anyFlagged = [](const std::vector<bool>& flags)
    {
        return std::any_of(
            flags.begin(),
            flags.end(),
            [](bool value)
            {
                return value;
            }
        );
    };
    bool detectedNonFiniteAttributes = anyFlagged(nonFiniteAttributes.vertices) || anyFlagged(nonFiniteAttributes.normals) ||
                                       anyFlagged(nonFiniteAttributes.texcoords);

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

    auto hasNonFinite = [](const std::vector<tinyobj::real_t>& values, size_t index, size_t width, const std::vector<bool>& rawFlags)
    {
        if (index < rawFlags.size() && rawFlags[index])
        {
            return true;
        }
        const size_t offset = index * width;
        if (offset + width > values.size())
        {
            return false;
        }
        return std::any_of(
            values.begin() + offset,
            values.begin() + offset + width,
            [](tinyobj::real_t value)
            {
                return !std::isfinite(value);
            }
        );
    };
    // A finite double can overflow tinyobj's float storage. Detect parsed values
    // too, including records that no face references.
    for (const auto* values : { &attributes.vertices, &attributes.normals, &attributes.texcoords, &attributes.colors })
    {
        detectedNonFiniteAttributes = detectedNonFiniteAttributes || std::any_of(
                                                                         values->begin(),
                                                                         values->end(),
                                                                         [](tinyobj::real_t value)
                                                                         {
                                                                             return !std::isfinite(value);
                                                                         }
                                                                     );
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

    // tinyobjloader's fixIndex() converts a positive OBJ face index i to (i - 1) with no upper-bound
    // clamp against the declared attribute counts. A crafted .obj can therefore reference indices far
    // beyond the number of declared vertices/normals/texcoords/colors, so every attribute read below
    // must be validated to avoid out-of-bounds heap access on this attacker-controlled import path.
    const size_t numNormals = attributes.normals.size() / 3;
    const size_t numTexcoords = attributes.texcoords.size() / 2;
    const size_t numColors = attributes.colors.size() / 3;

    auto GetPoint = [&allPoints, numPoints](const tinyobj::shape_t& shape, size_t index) -> PXR_NS::GfVec3f
    {
        if (index >= shape.mesh.indices.size())
        {
            return PXR_NS::GfVec3f(0.0f);
        }
        tinyobj::index_t idx = shape.mesh.indices[index];
        if (idx.vertex_index < 0 || size_t(idx.vertex_index) >= numPoints)
        {
            return PXR_NS::GfVec3f(0.0f);
        }
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
        size_t skippedIndexOffsetFaces = 0;
        size_t skippedAttributeIndexFaces = 0;
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

            // A corrupted num_face_vertices can claim more corners than the indices array holds.
            // Guard the indices access itself before validating the attribute indices it points to.
            if (indexOffset + fv > shape.mesh.indices.size())
            {
                ++skippedIndexOffsetFaces;
                indexOffset += fv;
                continue;
            }

            // Validate every attribute index referenced by this face before emitting any geometry.
            // Skipping the whole face (rather than an individual corner) keeps faceVertexCounts,
            // normals, uvs, and colors consistent for the mesh.
            bool faceValid = true;
            bool faceNonFinite = false;
            for (size_t v = 0; v < fv; v++)
            {
                const tinyobj::index_t& idx = shape.mesh.indices[indexOffset + v];
                if (idx.vertex_index < 0 || size_t(idx.vertex_index) >= numPoints)
                {
                    faceValid = false;
                    break;
                }
                if (idx.normal_index >= 0 && size_t(idx.normal_index) >= numNormals)
                {
                    faceValid = false;
                    break;
                }
                if (idx.texcoord_index >= 0 && size_t(idx.texcoord_index) >= numTexcoords)
                {
                    faceValid = false;
                    break;
                }
                if (hasNonFinite(attributes.vertices, size_t(idx.vertex_index), 3, nonFiniteAttributes.vertices) ||
                    hasNonFinite(attributes.colors, size_t(idx.vertex_index), 3, nonFiniteAttributes.vertices) ||
                    (idx.normal_index >= 0 && hasNonFinite(attributes.normals, size_t(idx.normal_index), 3, nonFiniteAttributes.normals)) ||
                    (idx.texcoord_index >= 0 && hasNonFinite(attributes.texcoords, size_t(idx.texcoord_index), 2, nonFiniteAttributes.texcoords)))
                {
                    faceNonFinite = true;
                    break;
                }
            }
            if (faceNonFinite)
            {
                Log("WARNING: OBJ shape '" + shapeName + "' face " + std::to_string(f) +
                    " contains non-finite vertex attribute values. Skipping this face.");
                indexOffset += fv;
                continue;
            }
            if (!faceValid)
            {
                ++skippedAttributeIndexFaces;
                indexOffset += fv;
                continue;
            }

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
                    auto v0 = GetPoint(shape, indexOffset);
                    auto v1 = GetPoint(shape, indexOffset + 1);
                    auto v2 = GetPoint(shape, indexOffset + 2);
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

        const size_t skippedFaces = skippedIndexOffsetFaces + skippedAttributeIndexFaces;
        if (skippedFaces > 0)
        {
            Log("Skipping " + std::to_string(skippedFaces) + " invalid OBJ face(s) in " + importAssetPath + ".");
        }

        faceIndexOffset += numFaces;

        for (const auto& subsetMesh : subsetMeshes)
        {
            const auto& meshInfo = subsetMesh.second;

            // Every face for this material may have been skipped as invalid; don't emit empty meshes.
            if (meshInfo.mesh->faceVertexCounts.empty())
            {
                continue;
            }

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

            // These arrays were appended in face-corner order, independently of point order.
            // Equal point and corner counts do not make them per-point attributes.
            meshInfo.mesh->hasFaceVaryingNormals = meshInfo.hasNormals;
            meshInfo.mesh->hasFaceVaryingUVs = meshInfo.hasUVs;
            meshInfo.mesh->hasFaceVaryingColors = meshInfo.hasColors;

            MeshGeomSubset subset;
            subset.materialIndex = subsetMesh.first;
            subset.name = meshInfo.mesh->name;
            subset.faceIndices.resize(meshInfo.mesh->faceVertexCounts.size());
            std::iota(subset.faceIndices.begin(), subset.faceIndices.end(), 0);
            meshInfo.mesh->meshSubsets.push_back(subset);
            stage->meshes.push_back(meshInfo.mesh);
        }
    }

    stage->hasSkippedCorruptGeometry = detectedNonFiniteAttributes;
    if (detectedNonFiniteAttributes && stage->meshes.empty())
    {
        detailedError = "Asset " + importAssetPath + " contains non-finite vertex attribute values; no valid mesh geometry remains.";
        Log(detailedError);
        status = OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT;
        return nullptr;
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
