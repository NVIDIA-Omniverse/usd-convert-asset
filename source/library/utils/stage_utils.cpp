// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "stage_utils.h"

#include "../common/common.h"

#include <cmath>
#include <numeric>


namespace
{
struct PointRemap
{
    std::vector<int> pointIndices;
    std::vector<size_t> keptPointIndices;
};

struct PointDataDomains
{
    bool hasFaceVaryingNormals = false;
    std::vector<bool> hasFaceVaryingColors;
    std::vector<bool> hasFaceVaryingUVs;
};

struct FaceRemap
{
    PXR_NS::VtIntArray faceVertexCounts;
    std::vector<size_t> keptFaceVertexIndices;
    std::vector<int> faceIndices;
};

template <typename ArrayType>
void CompactArray(ArrayType& values, const std::vector<size_t>& keptIndices)
{
    ArrayType compactedValues;
    compactedValues.reserve(keptIndices.size());
    for (size_t index : keptIndices)
    {
        compactedValues.push_back(values[index]);
    }
    values = std::move(compactedValues);
}

bool IsFinite(const PXR_NS::GfVec3f& point)
{
    return std::isfinite(point[0]) && std::isfinite(point[1]) && std::isfinite(point[2]);
}

bool IsZeroAreaFace(const Mesh& mesh, size_t faceVertexOffset, size_t faceVertexCount)
{
    if (faceVertexCount < 3)
    {
        return true;
    }

    const int originIndex = mesh.faceVertexIndices[faceVertexOffset];
    const PXR_NS::GfVec3f& origin = mesh.points[originIndex];
    if (!IsFinite(origin))
    {
        return true;
    }

    const int secondIndex = mesh.faceVertexIndices[faceVertexOffset + 1];
    const PXR_NS::GfVec3f& second = mesh.points[secondIndex];
    if (!IsFinite(second))
    {
        return true;
    }

    PXR_NS::GfVec3f previous = second - origin;
    PXR_NS::GfVec3f vectorArea(0.0f);
    for (size_t i = 2; i < faceVertexCount; i++)
    {
        const int pointIndex = mesh.faceVertexIndices[faceVertexOffset + i];
        const PXR_NS::GfVec3f& point = mesh.points[pointIndex];
        if (!IsFinite(point))
        {
            return true;
        }
        if (faceVertexCount == 3 && (origin == second || origin == point || second == point))
        {
            return true;
        }
        const PXR_NS::GfVec3f current = point - origin;
        vectorArea += PXR_NS::GfCross(previous, current);
        previous = current;
    }

    // Match ZeroAreaFaceChecker. A positive tolerance would remove valid faces from the inventory.
    return PXR_NS::GfDot(vectorArea, vectorArea) == 0.0f;
}

bool CompactIndexedValues(PXR_NS::VtVec2fArray& values, PXR_NS::VtIntArray& indices)
{
    std::vector<bool> keepValues(values.size(), false);
    for (int index : indices)
    {
        if (index < 0 || static_cast<size_t>(index) >= values.size())
        {
            return false;
        }
        keepValues[index] = true;
    }

    std::vector<int> valueRemapping(values.size(), -1);
    PXR_NS::VtVec2fArray compactedValues;
    for (size_t i = 0; i < values.size(); i++)
    {
        if (keepValues[i])
        {
            valueRemapping[i] = static_cast<int>(compactedValues.size());
            compactedValues.push_back(values[i]);
        }
    }
    for (int& index : indices)
    {
        index = valueRemapping[index];
    }
    values = std::move(compactedValues);
    return true;
}

void DiscardInvalidUvData(const MeshPtr& mesh)
{
    const size_t pointCount = mesh->points.size();
    const size_t faceVertexCount = mesh->faceVertexIndices.size();
    for (size_t i = 0; i < mesh->uvs.size(); i++)
    {
        const bool hasUvIndices = i < mesh->uvIndices.size() && !mesh->uvIndices[i].empty();
        bool validUvData = true;
        if (hasUvIndices)
        {
            validUvData = mesh->uvIndices[i].size() == pointCount || mesh->uvIndices[i].size() == faceVertexCount;
            for (const int uvIndex : mesh->uvIndices[i])
            {
                if (uvIndex < 0 || static_cast<size_t>(uvIndex) >= mesh->uvs[i].size())
                {
                    validUvData = false;
                    break;
                }
            }
        }
        else if (!mesh->uvs[i].empty())
        {
            validUvData = mesh->uvs[i].size() == pointCount || mesh->uvs[i].size() == faceVertexCount;
        }

        if (!validUvData)
        {
            mesh->uvs[i].clear();
            if (i < mesh->uvIndices.size())
            {
                mesh->uvIndices[i].clear();
            }
        }
    }
}

bool ApplyFaceRemap(const MeshPtr& mesh, FaceRemap&& faceRemap)
{
    const size_t pointCount = mesh->points.size();
    const size_t faceVertexCount = mesh->faceVertexIndices.size();
    const auto IsFaceVarying = [pointCount, faceVertexCount](const auto& values, bool hasFaceVaryingData)
    {
        return hasFaceVaryingData || (!values.empty() && values.size() != pointCount && values.size() == faceVertexCount);
    };
    const auto HasFaceVertexCount = [faceVertexCount](const auto& values)
    {
        return values.empty() || values.size() == faceVertexCount;
    };

    bool hasFaceVaryingNormals = IsFaceVarying(mesh->normals, mesh->hasFaceVaryingNormals);
    for (const auto& frame : mesh->normalCacheTimesamples)
    {
        hasFaceVaryingNormals = hasFaceVaryingNormals || IsFaceVarying(frame, mesh->hasFaceVaryingNormals);
    }
    if (hasFaceVaryingNormals)
    {
        if (!HasFaceVertexCount(mesh->normals))
        {
            return false;
        }
        for (const auto& frame : mesh->normalCacheTimesamples)
        {
            if (!HasFaceVertexCount(frame))
            {
                return false;
            }
        }
    }
    std::vector<bool> hasFaceVaryingColors(mesh->colors.size());
    for (size_t i = 0; i < mesh->colors.size(); i++)
    {
        hasFaceVaryingColors[i] = IsFaceVarying(mesh->colors[i], mesh->hasFaceVaryingColors);
        if (hasFaceVaryingColors[i] && !HasFaceVertexCount(mesh->colors[i]))
        {
            return false;
        }
    }
    std::vector<bool> hasFaceVaryingUVs(mesh->uvs.size());
    for (size_t i = 0; i < mesh->uvs.size(); i++)
    {
        const bool hasUvIndices = i < mesh->uvIndices.size() && !mesh->uvIndices[i].empty();
        const bool hasFaceVaryingUvIndices = hasUvIndices && mesh->uvIndices[i].size() == faceVertexCount;
        const bool hasVertexUvIndices = hasUvIndices && mesh->uvIndices[i].size() == pointCount;
        if (hasUvIndices)
        {
            if (!hasFaceVaryingUvIndices && !hasVertexUvIndices)
            {
                return false;
            }
            for (int uvIndex : mesh->uvIndices[i])
            {
                if (uvIndex < 0 || static_cast<size_t>(uvIndex) >= mesh->uvs[i].size())
                {
                    return false;
                }
            }
        }
        hasFaceVaryingUVs[i] = IsFaceVarying(mesh->uvs[i], mesh->hasFaceVaryingUVs);
        if (!hasUvIndices && hasFaceVaryingUVs[i] && !HasFaceVertexCount(mesh->uvs[i]))
        {
            return false;
        }
    }

    CompactArray(mesh->faceVertexIndices, faceRemap.keptFaceVertexIndices);
    if (hasFaceVaryingNormals && mesh->normals.size() == faceVertexCount)
    {
        CompactArray(mesh->normals, faceRemap.keptFaceVertexIndices);
    }
    for (auto& frame : mesh->normalCacheTimesamples)
    {
        if (hasFaceVaryingNormals && frame.size() == faceVertexCount)
        {
            CompactArray(frame, faceRemap.keptFaceVertexIndices);
        }
    }
    for (size_t i = 0; i < mesh->colors.size(); i++)
    {
        if (hasFaceVaryingColors[i] && mesh->colors[i].size() == faceVertexCount)
        {
            CompactArray(mesh->colors[i], faceRemap.keptFaceVertexIndices);
        }
    }
    for (size_t i = 0; i < mesh->uvs.size(); i++)
    {
        const bool hasUvIndices = i < mesh->uvIndices.size() && !mesh->uvIndices[i].empty();
        if (hasUvIndices)
        {
            const bool hasFaceVaryingUvIndices = mesh->uvIndices[i].size() == faceVertexCount;
            if (hasFaceVaryingUvIndices)
            {
                CompactArray(mesh->uvIndices[i], faceRemap.keptFaceVertexIndices);
                if (!CompactIndexedValues(mesh->uvs[i], mesh->uvIndices[i]))
                {
                    return false;
                }
            }
        }
        else if (hasFaceVaryingUVs[i] && mesh->uvs[i].size() == faceVertexCount)
        {
            CompactArray(mesh->uvs[i], faceRemap.keptFaceVertexIndices);
        }
    }

    mesh->faceVertexCounts = std::move(faceRemap.faceVertexCounts);
    for (auto& subset : mesh->meshSubsets)
    {
        PXR_NS::VtIntArray remappedFaceIndices;
        for (int faceIndex : subset.faceIndices)
        {
            if (faceIndex >= 0 && static_cast<size_t>(faceIndex) < faceRemap.faceIndices.size() && faceRemap.faceIndices[faceIndex] >= 0)
            {
                remappedFaceIndices.push_back(faceRemap.faceIndices[faceIndex]);
            }
        }
        subset.faceIndices = std::move(remappedFaceIndices);
    }
    return true;
}

bool RemoveZeroAreaFaces(const MeshPtr& mesh)
{
    if (!mesh->pointCacheTimesamples.empty())
    {
        return true;
    }

    const size_t faceVertexCount = mesh->faceVertexIndices.size();
    FaceRemap faceRemap;
    faceRemap.faceIndices.resize(mesh->faceVertexCounts.size(), -1);
    size_t faceVertexOffset = 0;
    for (size_t faceIndex = 0; faceIndex < mesh->faceVertexCounts.size(); faceIndex++)
    {
        const int count = mesh->faceVertexCounts[faceIndex];
        if (count < 0 || faceVertexOffset + static_cast<size_t>(count) > faceVertexCount)
        {
            return false;
        }
        for (size_t i = faceVertexOffset; i < faceVertexOffset + static_cast<size_t>(count); i++)
        {
            const int pointIndex = mesh->faceVertexIndices[i];
            if (pointIndex < 0 || static_cast<size_t>(pointIndex) >= mesh->points.size())
            {
                return false;
            }
        }

        if (!IsZeroAreaFace(*mesh, faceVertexOffset, static_cast<size_t>(count)))
        {
            faceRemap.faceIndices[faceIndex] = static_cast<int>(faceRemap.faceVertexCounts.size());
            faceRemap.faceVertexCounts.push_back(count);
            for (size_t i = faceVertexOffset; i < faceVertexOffset + static_cast<size_t>(count); i++)
            {
                faceRemap.keptFaceVertexIndices.push_back(i);
            }
        }
        faceVertexOffset += static_cast<size_t>(count);
    }
    if (faceVertexOffset != faceVertexCount || faceRemap.faceVertexCounts.size() == mesh->faceVertexCounts.size())
    {
        return faceVertexOffset == faceVertexCount;
    }

    return ApplyFaceRemap(mesh, std::move(faceRemap));
}

size_t GetMeshIndex(const Stage& stage, const MeshPtr& mesh)
{
    for (size_t i = 0; i < stage.meshes.size(); i++)
    {
        if (stage.meshes[i] == mesh)
        {
            return i;
        }
    }
    return stage.meshes.size();
}

void ClearMeshData(Stage& stage, const MeshPtr& mesh)
{
    mesh->points.clear();
    mesh->pointCacheTimesamples.clear();
    mesh->normalCacheTimesamples.clear();
    mesh->faceVertexCounts.clear();
    mesh->faceVertexIndices.clear();
    mesh->normals.clear();
    mesh->uvs.clear();
    mesh->uvIndices.clear();
    mesh->colors.clear();
    mesh->meshSubsets.clear();

    const size_t meshIndex = GetMeshIndex(stage, mesh);
    for (const auto& skinMesh : stage.skinMeshes)
    {
        if (skinMesh->meshIndex == meshIndex)
        {
            skinMesh->jointInfluences.clear();
            skinMesh->jointWeights.clear();
        }
    }
}

bool RepairTopology(const MeshPtr& mesh)
{
    const size_t pointCount = mesh->points.size();
    const size_t faceVertexCount = mesh->faceVertexIndices.size();
    FaceRemap faceRemap;
    faceRemap.keptFaceVertexIndices.reserve(faceVertexCount);
    faceRemap.faceIndices.resize(mesh->faceVertexCounts.size(), -1);
    size_t faceVertexOffset = 0;
    for (size_t faceIndex = 0; faceIndex < mesh->faceVertexCounts.size(); faceIndex++)
    {
        const int count = mesh->faceVertexCounts[faceIndex];
        if (count < 0)
        {
            return false;
        }

        const size_t faceVertexEnd = faceVertexOffset + static_cast<size_t>(count);
        if (faceVertexEnd > faceVertexCount)
        {
            break;
        }

        int repairedCount = 0;
        for (size_t i = faceVertexOffset; i < faceVertexEnd; i++)
        {
            const int pointIndex = mesh->faceVertexIndices[i];
            if (pointIndex >= 0 && static_cast<size_t>(pointIndex) < pointCount)
            {
                faceRemap.keptFaceVertexIndices.push_back(i);
                repairedCount++;
            }
        }
        if (repairedCount >= 3)
        {
            faceRemap.faceIndices[faceIndex] = static_cast<int>(faceRemap.faceVertexCounts.size());
            faceRemap.faceVertexCounts.push_back(repairedCount);
        }
        else
        {
            faceRemap.keptFaceVertexIndices.resize(faceRemap.keptFaceVertexIndices.size() - repairedCount);
        }
        faceVertexOffset = faceVertexEnd;
    }

    bool topologyChanged = faceRemap.keptFaceVertexIndices.size() != faceVertexCount ||
                           faceRemap.faceVertexCounts.size() != mesh->faceVertexCounts.size();
    if (!topologyChanged)
    {
        for (size_t i = 0; i < faceRemap.faceVertexCounts.size(); i++)
        {
            if (faceRemap.faceVertexCounts[i] != mesh->faceVertexCounts[i])
            {
                topologyChanged = true;
                break;
            }
        }
    }
    if (!topologyChanged)
    {
        return true;
    }

    return ApplyFaceRemap(mesh, std::move(faceRemap));
}

bool BuildPointRemap(const Mesh& mesh, PointRemap& pointRemap)
{
    const size_t pointCount = mesh.points.size();
    std::vector<bool> keepPoints(pointCount, false);
    for (int pointIndex : mesh.faceVertexIndices)
    {
        if (pointIndex < 0 || static_cast<size_t>(pointIndex) >= pointCount)
        {
            return false;
        }
        keepPoints[pointIndex] = true;
    }

    pointRemap.pointIndices.resize(pointCount);
    pointRemap.keptPointIndices.reserve(pointCount);
    for (size_t pointIndex = 0; pointIndex < pointCount; pointIndex++)
    {
        if (keepPoints[pointIndex])
        {
            pointRemap.pointIndices[pointIndex] = static_cast<int>(pointRemap.keptPointIndices.size());
            pointRemap.keptPointIndices.push_back(pointIndex);
        }
    }
    return true;
}

bool ValidatePointData(const Stage& stage, const MeshPtr& mesh, PointDataDomains& pointDataDomains, size_t& meshIndex)
{
    const size_t pointCount = mesh->points.size();
    auto HasPointCount = [pointCount](const auto& values)
    {
        return values.empty() || values.size() == pointCount;
    };
    const size_t faceVertexCount = mesh->faceVertexIndices.size();
    auto IsFaceVarying = [pointCount, faceVertexCount](const auto& values, bool hasFaceVaryingData)
    {
        return hasFaceVaryingData || (!values.empty() && values.size() != pointCount && values.size() == faceVertexCount);
    };
    auto HasFaceVertexCount = [faceVertexCount](const auto& values)
    {
        return values.empty() || values.size() == faceVertexCount;
    };
    auto HasPointOrFaceVertexCount = [pointCount, faceVertexCount](const auto& values)
    {
        return values.empty() || values.size() == pointCount || values.size() == faceVertexCount;
    };

    for (const auto& frame : mesh->pointCacheTimesamples)
    {
        if (frame.empty() || !HasPointCount(frame))
        {
            return false;
        }
    }

    pointDataDomains.hasFaceVaryingNormals = IsFaceVarying(mesh->normals, mesh->hasFaceVaryingNormals);
    if (!pointDataDomains.hasFaceVaryingNormals)
    {
        for (const auto& frame : mesh->normalCacheTimesamples)
        {
            if (IsFaceVarying(frame, false))
            {
                pointDataDomains.hasFaceVaryingNormals = true;
                break;
            }
        }
    }
    if (pointDataDomains.hasFaceVaryingNormals)
    {
        if (!HasFaceVertexCount(mesh->normals))
        {
            return false;
        }
        for (const auto& frame : mesh->normalCacheTimesamples)
        {
            if (!HasFaceVertexCount(frame))
            {
                return false;
            }
        }
    }
    else
    {
        if (!HasPointCount(mesh->normals))
        {
            return false;
        }
        for (const auto& frame : mesh->normalCacheTimesamples)
        {
            if (!HasPointCount(frame))
            {
                return false;
            }
        }
    }

    pointDataDomains.hasFaceVaryingColors.resize(mesh->colors.size());
    for (size_t i = 0; i < mesh->colors.size(); i++)
    {
        pointDataDomains.hasFaceVaryingColors[i] = IsFaceVarying(mesh->colors[i], mesh->hasFaceVaryingColors);
        if (pointDataDomains.hasFaceVaryingColors[i] ? !HasFaceVertexCount(mesh->colors[i]) : !HasPointCount(mesh->colors[i]))
        {
            return false;
        }
    }

    pointDataDomains.hasFaceVaryingUVs.resize(mesh->uvs.size());
    for (size_t i = 0; i < mesh->uvs.size(); i++)
    {
        const bool hasUvIndices = i < mesh->uvIndices.size() && !mesh->uvIndices[i].empty();
        if (hasUvIndices && !HasPointOrFaceVertexCount(mesh->uvIndices[i]))
        {
            return false;
        }
        pointDataDomains.hasFaceVaryingUVs[i] = IsFaceVarying(mesh->uvs[i], mesh->hasFaceVaryingUVs);
        if (!hasUvIndices && (pointDataDomains.hasFaceVaryingUVs[i] ? !HasFaceVertexCount(mesh->uvs[i]) : !HasPointCount(mesh->uvs[i])))
        {
            return false;
        }
        if (hasUvIndices)
        {
            for (int uvIndex : mesh->uvIndices[i])
            {
                if (uvIndex < 0 || static_cast<size_t>(uvIndex) >= mesh->uvs[i].size())
                {
                    return false;
                }
            }
        }
    }

    meshIndex = GetMeshIndex(stage, mesh);
    for (const auto& skinMesh : stage.skinMeshes)
    {
        if (skinMesh->meshIndex != meshIndex)
        {
            continue;
        }
        const size_t influenceCount = skinMesh->numBoneInfluencesPerVertex;
        if (influenceCount == 0 || skinMesh->jointInfluences.size() != pointCount * influenceCount ||
            skinMesh->jointWeights.size() != pointCount * influenceCount)
        {
            return false;
        }
    }
    return true;
}

template <typename ArrayType>
void CompactPointElementArray(ArrayType& values, const std::vector<size_t>& keptPointIndices, size_t elementSize)
{
    ArrayType compactedValues;
    compactedValues.reserve(keptPointIndices.size() * elementSize);
    for (size_t pointIndex : keptPointIndices)
    {
        const auto begin = values.begin() + pointIndex * elementSize;
        std::copy(begin, begin + elementSize, std::back_inserter(compactedValues));
    }
    values = std::move(compactedValues);
}

void CompactPointData(const MeshPtr& mesh, const PointRemap& pointRemap, const PointDataDomains& pointDataDomains)
{
    for (int& pointIndex : mesh->faceVertexIndices)
    {
        pointIndex = pointRemap.pointIndices[pointIndex];
    }

    CompactArray(mesh->points, pointRemap.keptPointIndices);
    for (auto& frame : mesh->pointCacheTimesamples)
    {
        if (!frame.empty())
        {
            CompactArray(frame, pointRemap.keptPointIndices);
        }
    }
    if (!pointDataDomains.hasFaceVaryingNormals)
    {
        if (!mesh->normals.empty())
        {
            CompactArray(mesh->normals, pointRemap.keptPointIndices);
        }
        for (auto& frame : mesh->normalCacheTimesamples)
        {
            if (!frame.empty())
            {
                CompactArray(frame, pointRemap.keptPointIndices);
            }
        }
    }
    for (size_t i = 0; i < mesh->colors.size(); i++)
    {
        if (!pointDataDomains.hasFaceVaryingColors[i] && !mesh->colors[i].empty())
        {
            CompactArray(mesh->colors[i], pointRemap.keptPointIndices);
        }
    }
    for (size_t i = 0; i < mesh->uvs.size(); i++)
    {
        const bool hasUvIndices = i < mesh->uvIndices.size() && !mesh->uvIndices[i].empty();
        if (!hasUvIndices && !pointDataDomains.hasFaceVaryingUVs[i] && !mesh->uvs[i].empty())
        {
            CompactArray(mesh->uvs[i], pointRemap.keptPointIndices);
        }
    }
}

template <typename ArrayType>
bool CompactIndexedArray(ArrayType& values, PXR_NS::VtIntArray& indices)
{
    std::vector<bool> usedValues(values.size(), false);
    for (int index : indices)
    {
        if (index < 0 || static_cast<size_t>(index) >= values.size())
        {
            return false;
        }
        usedValues[index] = true;
    }

    std::vector<int> remappedIndices(values.size(), -1);
    std::vector<size_t> keptValueIndices;
    keptValueIndices.reserve(values.size());
    for (size_t i = 0; i < usedValues.size(); i++)
    {
        if (usedValues[i])
        {
            remappedIndices[i] = static_cast<int>(keptValueIndices.size());
            keptValueIndices.push_back(i);
        }
    }

    if (keptValueIndices.size() == values.size())
    {
        return true;
    }

    CompactArray(values, keptValueIndices);
    for (int& index : indices)
    {
        index = remappedIndices[index];
    }
    return true;
}

bool CompactIndexedUvData(const MeshPtr& mesh)
{
    const size_t indexedUvSetCount = std::min(mesh->uvs.size(), mesh->uvIndices.size());
    for (size_t i = 0; i < indexedUvSetCount; i++)
    {
        if (!mesh->uvIndices[i].empty() && !CompactIndexedArray(mesh->uvs[i], mesh->uvIndices[i]))
        {
            return false;
        }
    }
    return true;
}

bool ExpandVertexIndexedUvData(const MeshPtr& mesh)
{
    const size_t pointCount = mesh->points.size();
    const size_t faceVertexCount = mesh->faceVertexIndices.size();
    const size_t indexedUvSetCount = std::min(mesh->uvs.size(), mesh->uvIndices.size());
    for (size_t i = 0; i < indexedUvSetCount; i++)
    {
        if (mesh->uvIndices[i].size() != pointCount || mesh->uvIndices[i].size() == faceVertexCount)
        {
            continue;
        }

        PXR_NS::VtIntArray expandedIndices;
        expandedIndices.reserve(faceVertexCount);
        for (int pointIndex : mesh->faceVertexIndices)
        {
            if (pointIndex < 0 || static_cast<size_t>(pointIndex) >= mesh->uvIndices[i].size())
            {
                return false;
            }
            expandedIndices.push_back(mesh->uvIndices[i][pointIndex]);
        }
        mesh->uvIndices[i] = std::move(expandedIndices);
    }
    return true;
}

void CompactSkinData(Stage& stage, size_t meshIndex, const std::vector<size_t>& keptPointIndices)
{
    for (const auto& skinMesh : stage.skinMeshes)
    {
        if (skinMesh->meshIndex == meshIndex)
        {
            CompactPointElementArray(skinMesh->jointInfluences, keptPointIndices, skinMesh->numBoneInfluencesPerVertex);
            CompactPointElementArray(skinMesh->jointWeights, keptPointIndices, skinMesh->numBoneInfluencesPerVertex);
        }
    }
}

bool CompactMeshPointsAfterTopologyRepair(Stage& stage, const MeshPtr& mesh)
{
    if (mesh->points.empty() || mesh->faceVertexIndices.empty())
    {
        ClearMeshData(stage, mesh);
        return true;
    }

    PointRemap pointRemap;
    if (!BuildPointRemap(*mesh, pointRemap))
    {
        return false;
    }
    if (!ExpandVertexIndexedUvData(mesh))
    {
        return false;
    }
    if (!CompactIndexedUvData(mesh))
    {
        return false;
    }

    PointDataDomains pointDataDomains;
    size_t meshIndex = 0;
    if (!ValidatePointData(stage, mesh, pointDataDomains, meshIndex))
    {
        return false;
    }

    if (pointRemap.keptPointIndices.size() == mesh->points.size())
    {
        return true;
    }

    CompactPointData(mesh, pointRemap, pointDataDomains);
    CompactSkinData(stage, meshIndex, pointRemap.keptPointIndices);
    return true;
}

bool GenerateSmoothNormalsForPoints(const Mesh& mesh, const PXR_NS::VtVec3fArray& points, PXR_NS::VtVec3fArray& smoothNormals)
{
    if (points.size() != mesh.points.size())
    {
        return false;
    }

    PXR_NS::VtVec3fArray accumulatedNormals(points.size(), PXR_NS::GfVec3f(0.0f));
    std::vector<std::vector<PXR_NS::GfVec3f>> neighborPositions(points.size());
    size_t faceVertexOffset = 0;
    for (int faceVertexCount : mesh.faceVertexCounts)
    {
        if (faceVertexCount < 0 || faceVertexOffset + static_cast<size_t>(faceVertexCount) > mesh.faceVertexIndices.size())
        {
            return false;
        }
        if (faceVertexCount < 3)
        {
            faceVertexOffset += static_cast<size_t>(faceVertexCount);
            continue;
        }

        for (int faceVertexIndex = 0; faceVertexIndex < faceVertexCount; ++faceVertexIndex)
        {
            const int pointIndex = mesh.faceVertexIndices[faceVertexOffset + static_cast<size_t>(faceVertexIndex)];
            if (pointIndex < 0 || static_cast<size_t>(pointIndex) >= points.size() || !IsFinite(points[pointIndex]))
            {
                return false;
            }
        }

        const int firstPointIndex = mesh.faceVertexIndices[faceVertexOffset];
        const auto& firstPoint = points[firstPointIndex];
        PXR_NS::GfVec3f faceNormal(0.0f);
        PXR_NS::GfVec3f fallbackFaceNormal(0.0f);
        for (int triangleIndex = 1; triangleIndex + 1 < faceVertexCount; ++triangleIndex)
        {
            const int secondPointIndex = mesh.faceVertexIndices[faceVertexOffset + static_cast<size_t>(triangleIndex)];
            const int thirdPointIndex = mesh.faceVertexIndices[faceVertexOffset + static_cast<size_t>(triangleIndex + 1)];
            const auto triangleNormal = (points[secondPointIndex] - firstPoint) ^ (points[thirdPointIndex] - firstPoint);
            faceNormal += triangleNormal;
            if (fallbackFaceNormal.GetLengthSq() == 0.0f && triangleNormal.GetLengthSq() > 0.0f)
            {
                fallbackFaceNormal = triangleNormal;
            }
        }

        float faceNormalLengthSquared = faceNormal.GetLengthSq();
        if (!std::isfinite(faceNormalLengthSquared) || faceNormalLengthSquared == 0.0f)
        {
            faceNormal = fallbackFaceNormal;
            faceNormalLengthSquared = faceNormal.GetLengthSq();
        }
        if (!std::isfinite(faceNormalLengthSquared) || faceNormalLengthSquared == 0.0f)
        {
            faceVertexOffset += static_cast<size_t>(faceVertexCount);
            continue;
        }
        faceNormal /= std::sqrt(faceNormalLengthSquared);

        for (int faceVertexIndex = 0; faceVertexIndex < faceVertexCount; ++faceVertexIndex)
        {
            const size_t currentOffset = faceVertexOffset + static_cast<size_t>(faceVertexIndex);
            const size_t previousOffset = faceVertexOffset + static_cast<size_t>((faceVertexIndex + faceVertexCount - 1) % faceVertexCount);
            const size_t nextOffset = faceVertexOffset + static_cast<size_t>((faceVertexIndex + 1) % faceVertexCount);
            const int pointIndex = mesh.faceVertexIndices[currentOffset];
            const auto& point = points[pointIndex];
            const auto& previousPoint = points[mesh.faceVertexIndices[previousOffset]];
            const auto& nextPoint = points[mesh.faceVertexIndices[nextOffset]];
            const auto previousEdge = previousPoint - point;
            const auto nextEdge = nextPoint - point;
            const float previousEdgeLengthSquared = previousEdge.GetLengthSq();
            const float nextEdgeLengthSquared = nextEdge.GetLengthSq();
            if (!std::isfinite(previousEdgeLengthSquared) || !std::isfinite(nextEdgeLengthSquared) || previousEdgeLengthSquared == 0.0f ||
                nextEdgeLengthSquared == 0.0f)
            {
                continue;
            }

            const auto edgeCross = previousEdge ^ nextEdge;
            float angle = std::atan2(edgeCross.GetLength(), previousEdge * nextEdge);
            if ((edgeCross * faceNormal) > 0.0f)
            {
                angle = 2.0f * std::acos(-1.0f) - angle;
            }
            if (std::isfinite(angle))
            {
                accumulatedNormals[pointIndex] += faceNormal * angle;
                neighborPositions[pointIndex].push_back(previousPoint);
                neighborPositions[pointIndex].push_back(nextPoint);
            }
        }
        faceVertexOffset += static_cast<size_t>(faceVertexCount);
    }
    if (faceVertexOffset != mesh.faceVertexIndices.size())
    {
        return false;
    }

    std::vector<size_t> sortedPointIndices(points.size());
    std::iota(sortedPointIndices.begin(), sortedPointIndices.end(), 0);
    std::sort(
        sortedPointIndices.begin(),
        sortedPointIndices.end(),
        [&points](size_t leftIndex, size_t rightIndex)
        {
            const auto& left = points[leftIndex];
            const auto& right = points[rightIndex];
            for (size_t componentIndex = 0; componentIndex < 3; ++componentIndex)
            {
                if (left[componentIndex] != right[componentIndex])
                {
                    return left[componentIndex] < right[componentIndex];
                }
            }
            return false;
        }
    );

    smoothNormals.assign(points.size(), PXR_NS::GfVec3f(0.0f));
    for (size_t groupStart = 0; groupStart < sortedPointIndices.size();)
    {
        size_t groupEnd = groupStart + 1;
        const auto& position = points[sortedPointIndices[groupStart]];
        while (groupEnd < sortedPointIndices.size() && points[sortedPointIndices[groupEnd]] == position)
        {
            ++groupEnd;
        }

        const size_t groupSize = groupEnd - groupStart;
        std::vector<size_t> componentParents(groupSize);
        std::iota(componentParents.begin(), componentParents.end(), 0);
        const auto FindComponent = [&componentParents](size_t index)
        {
            while (componentParents[index] != index)
            {
                componentParents[index] = componentParents[componentParents[index]];
                index = componentParents[index];
            }
            return index;
        };
        for (size_t left = 0; left < groupSize; ++left)
        {
            const size_t leftPointIndex = sortedPointIndices[groupStart + left];
            for (size_t right = left + 1; right < groupSize; ++right)
            {
                const size_t rightPointIndex = sortedPointIndices[groupStart + right];
                bool sharesEdge = false;
                for (const auto& leftNeighbor : neighborPositions[leftPointIndex])
                {
                    if (std::find(neighborPositions[rightPointIndex].begin(), neighborPositions[rightPointIndex].end(), leftNeighbor) !=
                        neighborPositions[rightPointIndex].end())
                    {
                        sharesEdge = true;
                        break;
                    }
                }
                const float leftLengthSquared = accumulatedNormals[leftPointIndex].GetLengthSq();
                const float rightLengthSquared = accumulatedNormals[rightPointIndex].GetLengthSq();
                constexpr float smoothingAngleCosine = -0.9961947f; // cos(175 degrees), matching Assimp's default.
                const float compatibleNormalThreshold = smoothingAngleCosine * std::sqrt(leftLengthSquared * rightLengthSquared);
                if (sharesEdge && accumulatedNormals[leftPointIndex] * accumulatedNormals[rightPointIndex] >= compatibleNormalThreshold)
                {
                    componentParents[FindComponent(right)] = FindComponent(left);
                }
            }
        }

        std::vector<PXR_NS::GfVec3f> componentNormals(groupSize, PXR_NS::GfVec3f(0.0f));
        for (size_t groupIndex = 0; groupIndex < groupSize; ++groupIndex)
        {
            componentNormals[FindComponent(groupIndex)] += accumulatedNormals[sortedPointIndices[groupStart + groupIndex]];
        }
        for (size_t groupIndex = 0; groupIndex < groupSize; ++groupIndex)
        {
            auto normal = componentNormals[FindComponent(groupIndex)];
            const float normalLengthSquared = normal.GetLengthSq();
            if (std::isfinite(normalLengthSquared) && normalLengthSquared > 0.0f)
            {
                normal /= std::sqrt(normalLengthSquared);
            }
            smoothNormals[sortedPointIndices[groupStart + groupIndex]] = normal;
        }
        groupStart = groupEnd;
    }
    return true;
}
} // namespace

bool StageUtils::CleanupMeshes(const StagePtr& stage, std::string& detailedError)
{
    if (!stage)
    {
        detailedError = "Failed to clean meshes because the imported stage is null.";
        return false;
    }

    for (const auto& mesh : stage->meshes)
    {
        if (mesh->faceVertexCounts.empty() && mesh->faceVertexIndices.empty())
        {
            continue;
        }

        DiscardInvalidUvData(mesh);
        if (!RepairTopology(mesh))
        {
            const std::string& meshName = mesh->name.empty() ? std::string("<unnamed>") : mesh->name;
            detailedError = "Failed to repair mesh '" + meshName + "' because it has invalid topology or mesh data sizes.";
            return false;
        }
        if (!RemoveZeroAreaFaces(mesh))
        {
            const std::string& meshName = mesh->name.empty() ? std::string("<unnamed>") : mesh->name;
            detailedError = "Failed to remove zero-area faces from mesh '" + meshName + "' because its face data cannot be remapped.";
            return false;
        }
        if (!CompactMeshPointsAfterTopologyRepair(*stage, mesh))
        {
            const std::string& meshName = mesh->name.empty() ? std::string("<unnamed>") : mesh->name;
            detailedError = "Failed to compact cleaned mesh '" + meshName + "' because it has invalid point data sizes.";
            return false;
        }
    }
    return true;
}

bool StageUtils::CompactMeshPoints(const StagePtr& stage, const MeshPtr& mesh)
{
    if (!stage || !mesh)
    {
        return false;
    }
    DiscardInvalidUvData(mesh);
    if (!RepairTopology(mesh))
    {
        return false;
    }
    return CompactMeshPointsAfterTopologyRepair(*stage, mesh);
}

bool StageUtils::GenerateSmoothNormals(const MeshPtr& mesh, bool replaceExistingNormals)
{
    if (!mesh)
    {
        return false;
    }
    if ((!replaceExistingNormals && !mesh->normals.empty()) || mesh->points.empty() || mesh->faceVertexCounts.empty() ||
        mesh->faceVertexIndices.empty())
    {
        return true;
    }

    PXR_NS::VtVec3fArray smoothNormals;
    if (!GenerateSmoothNormalsForPoints(*mesh, mesh->points, smoothNormals))
    {
        return false;
    }

    NormalCacheTimesamples normalCacheTimesamples;
    normalCacheTimesamples.reserve(mesh->pointCacheTimesamples.size());
    for (const auto& pointCacheFrame : mesh->pointCacheTimesamples)
    {
        if (pointCacheFrame.empty())
        {
            return false;
        }
        PXR_NS::VtVec3fArray frameNormals;
        if (!GenerateSmoothNormalsForPoints(*mesh, pointCacheFrame, frameNormals))
        {
            return false;
        }
        normalCacheTimesamples.push_back(std::move(frameNormals));
    }

    mesh->normals = std::move(smoothNormals);
    mesh->normalCacheTimesamples = std::move(normalCacheTimesamples);
    mesh->hasFaceVaryingNormals = false;
    return true;
}

void StageUtils::OptimizeMeshes(const StagePtr& stage)
{
    struct MeshPoint
    {
        size_t index;
        std::vector<float> values;

        bool operator==(const MeshPoint& other) const
        {
            if (values.size() != other.values.size())
            {
                return false;
            }

            for (size_t i = 0; i < values.size(); i++)
            {
                if (values[i] != other.values[i])
                {
                    return false;
                }
            }

            return true;
        }

        bool operator<(const MeshPoint& other) const
        {
            for (size_t i = 0; i < values.size(); i++)
            {
                if (values[i] < other.values[i])
                {
                    return true;
                }
                else if (values[i] > other.values[i])
                {
                    return false;
                }
            }

            return false;
        }
    };

    for (size_t i = 0; i < stage->meshes.size(); i++)
    {
        auto mesh = stage->meshes[i];
        if (mesh->points.empty())
        {
            continue;
        }

        // Gather all mesh points information to sort
        std::vector<MeshPoint> meshPoints;
        for (size_t j = 0; j < mesh->points.size(); j++)
        {
            MeshPoint point;
            point.index = j;
            point.values.push_back(mesh->points[j][0]);
            point.values.push_back(mesh->points[j][1]);
            point.values.push_back(mesh->points[j][2]);
            meshPoints.push_back(point);
        }

        // Sort points to remove redudant one
        std::sort(meshPoints.begin(), meshPoints.end());

        std::vector<bool> keepPoints(mesh->points.size(), false);
        std::vector<int> referenceIndex(mesh->points.size());

        // Find and mark all redundant points
        auto meshPoint = meshPoints[0];
        size_t currentIndex = meshPoint.index;
        referenceIndex[currentIndex] = currentIndex;
        keepPoints[currentIndex] = true;
        for (size_t j = 1; j < meshPoints.size(); j++)
        {
            if (meshPoints[j] == meshPoint)
            {
                keepPoints[meshPoints[j].index] = false;
                referenceIndex[meshPoints[j].index] = currentIndex;
            }
            else
            {
                meshPoint = meshPoints[j];
                currentIndex = meshPoint.index;
                referenceIndex[currentIndex] = currentIndex;
                keepPoints[currentIndex] = true;
            }
        }
        meshPoints.clear();

        // Keep only unique points
        size_t keepPointsSize = 0;
        PXR_NS::VtArray<PXR_NS::GfVec3f> newPoints;
        std::vector<int> indexRemapping(mesh->points.size());
        for (size_t j = 0; j < mesh->points.size(); j++)
        {
            if (keepPoints[j])
            {
                newPoints.push_back(mesh->points[j]);
                indexRemapping[j] = keepPointsSize;
                keepPointsSize++;
            }
        }

        // Remapping indexes for redundant points
        for (size_t j = 0; j < mesh->points.size(); j++)
        {
            if (!keepPoints[j])
            {
                indexRemapping[j] = indexRemapping[referenceIndex[j]];
            }
        }

        PXR_NS::VtArray<int> newFaceVertexIndices;
        for (size_t j = 0; j < mesh->faceVertexIndices.size(); j++)
        {
            const int pointIndex = mesh->faceVertexIndices[j];
            if (pointIndex < 0 || static_cast<size_t>(pointIndex) >= indexRemapping.size())
            {
                continue;
            }
            const size_t index = static_cast<size_t>(pointIndex);
            newFaceVertexIndices.push_back(indexRemapping[index]);
        }
    }
}

// MergeMesh always appends fully expanded (per-face-vertex) uv values to targetMesh->uvs, so any indexed uv
// data already sitting in targetMesh from before this call must be expanded in place first, otherwise the
// raw (compact) values would be left mixed in with the expanded ones appended for srcMesh.
static void ExpandIndexedUvsInPlace(const MeshPtr& mesh)
{
    const size_t faceVertexCount = mesh->faceVertexIndices.size();
    for (size_t i = 0; i < mesh->uvs.size(); i++)
    {
        if (i >= mesh->uvIndices.size() || mesh->uvIndices[i].empty())
        {
            continue;
        }

        const bool hasFaceVaryingIndices = mesh->uvIndices[i].size() == faceVertexCount;
        const bool hasVertexIndices = mesh->uvIndices[i].size() == mesh->points.size();
        PXR_NS::VtVec2fArray expanded;
        if (hasFaceVaryingIndices)
        {
            expanded.reserve(mesh->uvIndices[i].size());
            for (int uvIndex : mesh->uvIndices[i])
            {
                const bool isValidIndex = uvIndex >= 0 && static_cast<size_t>(uvIndex) < mesh->uvs[i].size();
                expanded.push_back(isValidIndex ? mesh->uvs[i][uvIndex] : PXR_NS::GfVec2f(0.0f, 0.0f));
            }
        }
        else if (hasVertexIndices)
        {
            expanded.reserve(faceVertexCount);
            for (int pointIndex : mesh->faceVertexIndices)
            {
                bool isValidIndex = pointIndex >= 0 && static_cast<size_t>(pointIndex) < mesh->uvIndices[i].size();
                int uvIndex = isValidIndex ? mesh->uvIndices[i][pointIndex] : -1;
                isValidIndex = uvIndex >= 0 && static_cast<size_t>(uvIndex) < mesh->uvs[i].size();
                expanded.push_back(isValidIndex ? mesh->uvs[i][uvIndex] : PXR_NS::GfVec2f(0.0f, 0.0f));
            }
        }
        else
        {
            continue;
        }
        mesh->uvs[i] = std::move(expanded);
    }
    mesh->uvIndices.clear();
}

void StageUtils::MergeMesh(const MeshPtr& targetMesh, const MeshPtr& srcMesh, const PXR_NS::GfMatrix4d& srcTransform)
{
    if (srcMesh->points.empty())
    {
        return;
    }

    ExpandIndexedUvsInPlace(targetMesh);

    size_t targetFaceCount = targetMesh->faceVertexCounts.size();
    size_t targetPointsCount = targetMesh->points.size();
    const size_t targetFaceVertexCount = targetMesh->faceVertexIndices.size();
    const size_t srcFaceVertexCount = srcMesh->faceVertexIndices.size();

    if (!targetMesh->pointCacheTimesamples.empty() || !srcMesh->pointCacheTimesamples.empty())
    {
        const size_t targetFrameCount = targetMesh->pointCacheTimesamples.size();
        const size_t srcFrameCount = srcMesh->pointCacheTimesamples.size();
        const size_t frameCount = std::max(targetFrameCount, srcFrameCount);
        PointCacheTimesamples mergedPointCache;
        mergedPointCache.reserve(frameCount);
        for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            const auto& targetFrame = targetFrameCount == 0 ? targetMesh->points :
                                                              targetMesh->pointCacheTimesamples[std::min(frameIndex, targetFrameCount - 1)];
            const auto& srcFrame = srcFrameCount == 0 ? srcMesh->points : srcMesh->pointCacheTimesamples[std::min(frameIndex, srcFrameCount - 1)];
            const auto& validTargetFrame = targetFrame.size() == targetPointsCount ? targetFrame : targetMesh->points;
            const auto& validSrcFrame = srcFrame.size() == srcMesh->points.size() ? srcFrame : srcMesh->points;

            PointCacheFrame mergedFrame = validTargetFrame;
            std::transform(
                validSrcFrame.begin(),
                validSrcFrame.end(),
                std::back_inserter(mergedFrame),
                [&srcTransform](const PXR_NS::GfVec3f& point)
                {
                    return PXR_NS::GfVec3f(srcTransform.Transform(point));
                }
            );
            mergedPointCache.push_back(std::move(mergedFrame));
        }
        targetMesh->pointCacheTimesamples = std::move(mergedPointCache);
        targetMesh->normalCacheTimesamples.clear();
    }

    std::transform(
        srcMesh->points.begin(),
        srcMesh->points.end(),
        std::back_inserter(targetMesh->points),
        [&srcMesh, &srcTransform](const PXR_NS::GfVec3f& point)
        {
            return PXR_NS::GfVec3f(srcTransform.Transform(point));
        }
    );
    std::transform(
        srcMesh->faceVertexIndices.begin(),
        srcMesh->faceVertexIndices.end(),
        std::back_inserter(targetMesh->faceVertexIndices),
        [targetPointsCount](int index)
        {
            return index < 0 ? index : index + static_cast<int>(targetPointsCount);
        }
    );

    if (srcMesh->normals.empty() || (targetPointsCount != 0 && targetMesh->normals.empty()))
    {
        targetMesh->normals.clear();
        targetMesh->hasFaceVaryingNormals = false;
    }
    else if (targetFaceVertexCount == 0 && srcFaceVertexCount == 0)
    {
        const auto& invertTranspose = srcTransform.GetTranspose().GetInverse();
        for (const auto& srcNormal : srcMesh->normals)
        {
            auto normal = PXR_NS::GfVec3f(invertTranspose.TransformDir(srcNormal));
            const float normalLengthSquared = normal.GetLengthSq();
            if (std::isfinite(normalLengthSquared) && normalLengthSquared > 0.0f)
            {
                normal /= std::sqrt(normalLengthSquared);
            }
            targetMesh->normals.push_back(normal);
        }
        targetMesh->hasFaceVaryingNormals = false;
    }
    else
    {
        const bool targetHasFaceVaryingNormals = targetMesh->hasFaceVaryingNormals || targetMesh->normals.size() != targetPointsCount;
        if (targetPointsCount != 0 && !targetHasFaceVaryingNormals)
        {
            PXR_NS::VtVec3fArray expandedNormals;
            expandedNormals.reserve(targetFaceVertexCount);
            for (size_t faceVertexIndex = 0; faceVertexIndex < targetFaceVertexCount; ++faceVertexIndex)
            {
                const int pointIndex = targetMesh->faceVertexIndices[faceVertexIndex];
                expandedNormals.push_back(targetMesh->normals[pointIndex]);
            }
            targetMesh->normals = std::move(expandedNormals);
        }

        const bool srcHasFaceVaryingNormals = srcMesh->hasFaceVaryingNormals || srcMesh->normals.size() != srcMesh->points.size();
        const auto& invertTranspose = srcTransform.GetTranspose().GetInverse();
        for (size_t faceVertexIndex = 0; faceVertexIndex < srcFaceVertexCount; ++faceVertexIndex)
        {
            const int pointIndex = srcMesh->faceVertexIndices[faceVertexIndex];
            const size_t normalIndex = srcHasFaceVaryingNormals ? faceVertexIndex : static_cast<size_t>(pointIndex);
            auto normal = PXR_NS::GfVec3f(invertTranspose.TransformDir(srcMesh->normals[normalIndex]));
            const float normalLengthSquared = normal.GetLengthSq();
            if (std::isfinite(normalLengthSquared) && normalLengthSquared > 0.0f)
            {
                normal /= std::sqrt(normalLengthSquared);
            }
            targetMesh->normals.push_back(normal);
        }
        targetMesh->hasFaceVaryingNormals = true;
    }

    size_t totalSrcFaceVertices = 0;
    for (size_t faceVertexCount : srcMesh->faceVertexCounts)
    {
        totalSrcFaceVertices += faceVertexCount;
    }

    size_t totalTargetFaceVertices = 0;
    for (size_t faceVertexCount : targetMesh->faceVertexCounts)
    {
        totalTargetFaceVertices += faceVertexCount;
    }

    // Handles uv merge
    size_t numUvSet = std::max(srcMesh->uvs.size(), targetMesh->uvs.size());
    for (size_t i = targetMesh->uvs.size(); i < numUvSet; i++)
    {
        PXR_NS::VtVec2fArray uv(totalTargetFaceVertices, PXR_NS::GfVec2f(0.0f, 0.0f));
        targetMesh->uvs.push_back(uv);
    }

    for (size_t i = 0; i < numUvSet; i++)
    {
        const bool hasFaceVaryingIndices = i < srcMesh->uvIndices.size() && srcMesh->uvIndices[i].size() == totalSrcFaceVertices;
        const bool hasVertexIndices = i < srcMesh->uvIndices.size() && srcMesh->uvIndices[i].size() == srcMesh->points.size();
        if (i < srcMesh->uvs.size() && hasFaceVaryingIndices)
        {
            for (int uvIndex : srcMesh->uvIndices[i])
            {
                const bool isValidIndex = uvIndex >= 0 && static_cast<size_t>(uvIndex) < srcMesh->uvs[i].size();
                targetMesh->uvs[i].push_back(isValidIndex ? srcMesh->uvs[i][uvIndex] : PXR_NS::GfVec2f(0.0f, 0.0f));
            }
        }
        else if (i < srcMesh->uvs.size() && hasVertexIndices)
        {
            for (int pointIndex : srcMesh->faceVertexIndices)
            {
                bool isValidIndex = pointIndex >= 0 && static_cast<size_t>(pointIndex) < srcMesh->uvIndices[i].size();
                int uvIndex = isValidIndex ? srcMesh->uvIndices[i][pointIndex] : -1;
                isValidIndex = uvIndex >= 0 && static_cast<size_t>(uvIndex) < srcMesh->uvs[i].size();
                targetMesh->uvs[i].push_back(isValidIndex ? srcMesh->uvs[i][uvIndex] : PXR_NS::GfVec2f(0.0f, 0.0f));
            }
        }
        else if (i < srcMesh->uvs.size() && srcMesh->uvs[i].size() == totalSrcFaceVertices)
        {
            std::copy(srcMesh->uvs[i].begin(), srcMesh->uvs[i].end(), std::back_inserter(targetMesh->uvs[i]));
        }
        else if (i < srcMesh->uvs.size() && srcMesh->uvs[i].size() == srcMesh->points.size())
        {
            for (int pointIndex : srcMesh->faceVertexIndices)
            {
                const bool isValidIndex = pointIndex >= 0 && static_cast<size_t>(pointIndex) < srcMesh->uvs[i].size();
                targetMesh->uvs[i].push_back(isValidIndex ? srcMesh->uvs[i][pointIndex] : PXR_NS::GfVec2f(0.0f, 0.0f));
            }
        }
        else
        {
            PXR_NS::VtVec2fArray uv(totalSrcFaceVertices, PXR_NS::GfVec2f(0.0f, 0.0f));
            std::copy(uv.begin(), uv.end(), std::back_inserter(targetMesh->uvs[i]));
        }
    }
    // uvs above are always expanded to per-face-vertex values, so any pre-merge index arrays no longer apply.
    targetMesh->uvIndices.clear();
    targetMesh->hasFaceVaryingUVs = !targetMesh->uvs.empty();

    // Handles color merge
    size_t numColors = std::max(srcMesh->colors.size(), targetMesh->colors.size());
    for (size_t i = targetMesh->colors.size(); i < numColors; i++)
    {
        PXR_NS::VtVec3fArray colors(totalTargetFaceVertices, PXR_NS::GfVec3f(1.0f, 1.0f, 1.0f));
        targetMesh->colors.push_back(colors);
    }

    for (size_t i = 0; i < numColors; i++)
    {
        if (i < srcMesh->colors.size() && srcMesh->colors[i].size() == totalSrcFaceVertices)
        {
            std::copy(srcMesh->colors[i].begin(), srcMesh->colors[i].end(), std::back_inserter(targetMesh->colors[i]));
        }
        else if (i < srcMesh->colors.size() && srcMesh->colors[i].size() == srcMesh->points.size())
        {
            for (int pointIndex : srcMesh->faceVertexIndices)
            {
                const bool isValidIndex = pointIndex >= 0 && static_cast<size_t>(pointIndex) < srcMesh->colors[i].size();
                targetMesh->colors[i].push_back(isValidIndex ? srcMesh->colors[i][pointIndex] : PXR_NS::GfVec3f(1.0f, 1.0f, 1.0f));
            }
        }
        else
        {
            PXR_NS::VtVec3fArray colors(totalSrcFaceVertices, PXR_NS::GfVec3f(1.0f, 1.0f, 1.0f));
            std::copy(colors.begin(), colors.end(), std::back_inserter(targetMesh->colors[i]));
        }
    }
    targetMesh->hasFaceVaryingColors = !targetMesh->colors.empty();

    std::copy(srcMesh->faceVertexCounts.begin(), srcMesh->faceVertexCounts.end(), std::back_inserter(targetMesh->faceVertexCounts));

    if (srcMesh->meshSubsets.size() == 1 && targetMesh->meshSubsets.size() == 1 &&
        srcMesh->meshSubsets[0].materialIndex == targetMesh->meshSubsets[0].materialIndex)
    {
        MeshGeomSubset subset;
        subset.materialIndex = targetMesh->meshSubsets[0].materialIndex;
        subset.name = targetMesh->meshSubsets[0].materialIndex;
        subset.faceIndices.resize(srcMesh->meshSubsets[0].faceIndices.size() + targetMesh->meshSubsets[0].faceIndices.size());
        std::iota(subset.faceIndices.begin(), subset.faceIndices.end(), 0);
        targetMesh->meshSubsets[0] = subset;
    }
    else
    {
        std::transform(
            srcMesh->meshSubsets.begin(),
            srcMesh->meshSubsets.end(),
            std::back_inserter(targetMesh->meshSubsets),
            [targetFaceCount](MeshGeomSubset& subset) -> MeshGeomSubset
            {
                for (size_t i = 0; i < subset.faceIndices.size(); i++)
                {
                    subset.faceIndices[i] += targetFaceCount;
                }

                return subset;
            }
        );
    }
    // Baking a reflection changes handedness. Reverse the appended corners and
    // their face-varying data together so normals, UVs, and colors stay attached.
    if (srcTransform.GetDeterminant() < 0)
    {
        size_t begin = targetFaceVertexCount;
        for (int count : srcMesh->faceVertexCounts)
        {
            const size_t end = begin + count;
            auto reverseCorners = [&](auto& values)
            {
                if (values.size() == targetMesh->faceVertexIndices.size())
                {
                    std::reverse(values.begin() + begin, values.begin() + end);
                }
            };
            reverseCorners(targetMesh->faceVertexIndices);
            if (targetMesh->hasFaceVaryingNormals)
            {
                reverseCorners(targetMesh->normals);
            }
            for (auto& uv : targetMesh->uvs)
            {
                reverseCorners(uv);
            }
            for (auto& color : targetMesh->colors)
            {
                reverseCorners(color);
            }
            begin = end;
        }
    }
}

static void MergeMeshesWithSidedness(const StagePtr& stage, const std::string& mergedMeshName, bool doubleSided)
{
    if (stage->meshes.empty())
    {
        return;
    }

    const auto isClearedMesh = [](const MeshPtr& mesh)
    {
        return mesh->points.empty() && mesh->faceVertexCounts.empty() && mesh->faceVertexIndices.empty();
    };

    // Select each eligible mesh independently; unrelated animation or instancing must not
    // prevent static siblings from being merged.
    const size_t meshCount = stage->meshes.size();
    std::vector<bool> eligible(meshCount, true);
    std::vector<bool> skinned(meshCount, false);
    std::vector<size_t> instanceCounts(meshCount, 0);
    std::vector<StageNodePtr> instanceNodes(meshCount);
    for (size_t i = 0; i < meshCount; ++i)
    {
        const auto& mesh = stage->meshes[i];
        eligible[i] = mesh->doubleSided == doubleSided && !mesh->faceVertexIndices.empty() && mesh->pointCacheTimesamples.empty() &&
                      mesh->normalCacheTimesamples.empty();
    }
    for (const auto& skinMesh : stage->skinMeshes)
    {
        eligible[skinMesh->meshIndex] = false;
        skinned[skinMesh->meshIndex] = true;
    }

    StageUtils::TraverseStageTree(
        stage->rootNode,
        [&](const StageNodePtr& node)
        {
            auto& instances = node->staticMeshInstances;
            instances.erase(
                std::remove_if(
                    instances.begin(),
                    instances.end(),
                    [&](size_t index)
                    {
                        return isClearedMesh(stage->meshes[index]);
                    }
                ),
                instances.end()
            );

            auto ancestor = node;
            while (ancestor && ancestor->transformAnimationTracks.empty())
            {
                ancestor = ancestor->parent.lock();
            }
            for (size_t index : instances)
            {
                ++instanceCounts[index];
                instanceNodes[index] = node;
                if (ancestor)
                {
                    eligible[index] = false;
                }
            }
            return true;
        }
    );

    size_t mergeCount = 0;
    for (size_t i = 0; i < meshCount; ++i)
    {
        eligible[i] = eligible[i] && instanceCounts[i] == 1;
        mergeCount += eligible[i];
    }
    if (mergeCount < 2)
    {
        return;
    }

    auto mergedMesh = std::make_shared<Mesh>();
    mergedMesh->name = mergedMeshName;
    mergedMesh->doubleSided = doubleSided;
    std::vector<MeshPtr> retainedMeshes;
    std::vector<size_t> remappedIndices(meshCount);
    for (size_t i = 0; i < meshCount; ++i)
    {
        if (eligible[i])
        {
            StageUtils::MergeMesh(mergedMesh, stage->meshes[i], instanceNodes[i]->worldTransformMatrix);
        }
        else if (!isClearedMesh(stage->meshes[i]) || skinned[i])
        {
            remappedIndices[i] = retainedMeshes.size();
            retainedMeshes.push_back(stage->meshes[i]);
        }
    }

    // Preserve all non-merged instances and skin references when compacting the mesh table.
    StageUtils::TraverseStageTree(
        stage->rootNode,
        [&](const StageNodePtr& node)
        {
            auto& instances = node->staticMeshInstances;
            instances.erase(
                std::remove_if(
                    instances.begin(),
                    instances.end(),
                    [&](size_t index)
                    {
                        return eligible[index];
                    }
                ),
                instances.end()
            );
            for (auto& index : instances)
            {
                index = remappedIndices[index];
            }
            return true;
        }
    );
    for (const auto& skinMesh : stage->skinMeshes)
    {
        skinMesh->meshIndex = remappedIndices[skinMesh->meshIndex];
    }
    stage->rootNode->staticMeshInstances.push_back(retainedMeshes.size());
    retainedMeshes.push_back(mergedMesh);
    stage->meshes = std::move(retainedMeshes);
}

void StageUtils::MergeMeshes(const StagePtr& stage, const std::string& mergedMeshName)
{
    // Most scenes are entirely single-sided; avoid a second tree traversal for them.
    const bool hasDoubleSided = std::any_of(
        stage->meshes.begin(),
        stage->meshes.end(),
        [](const MeshPtr& mesh)
        {
            return mesh->doubleSided;
        }
    );
    MergeMeshesWithSidedness(stage, mergedMeshName, false);
    if (hasDoubleSided)
    {
        MergeMeshesWithSidedness(stage, mergedMeshName + "_double_sided", true);
    }
}

void StageUtils::TraverseStageTree(const StageNodePtr& stageNode, OnStageNodePreCallback callback, OnStageNodePostCallback endNodeCallback)
{
    if (!stageNode || !callback)
    {
        return;
    }

    bool cont = callback(stageNode);
    if (cont)
    {
        for (const auto& child : stageNode->children)
        {
            TraverseStageTree(child, callback, endNodeCallback);
        }
    }

    if (endNodeCallback)
    {
        endNodeCallback(stageNode);
    }
}
