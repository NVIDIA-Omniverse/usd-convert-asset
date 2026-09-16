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
        if (!HasPointCount(frame))
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
            size_t index = mesh->faceVertexIndices[j];
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

    if (targetPointsCount != 0 && (srcMesh->normals.empty() || targetMesh->normals.empty()))
    {
        targetMesh->normals.clear();
    }
    else
    {
        const auto& invertTranspose = srcTransform.GetTranspose().GetInverse();
        std::transform(
            srcMesh->normals.begin(),
            srcMesh->normals.end(),
            std::back_inserter(targetMesh->normals),
            [&srcMesh, &invertTranspose](const PXR_NS::GfVec3f& normal)
            {
                return PXR_NS::GfVec3f(invertTranspose.TransformDir(normal));
            }
        );
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
}

void StageUtils::MergeMeshes(const StagePtr& stage, const std::string& mergedMeshName)
{
    if (stage->meshes.size() == 0)
    {
        return;
    }

    for (size_t i = 0; i < stage->meshes.size(); i++)
    {
        for (auto& skinMesh : stage->skinMeshes)
        {
            if (skinMesh->meshIndex == i)
            {
                return;
            }
        }
    }

    std::unordered_map<MeshPtr, StageNodePtr> meshInstanceNodes;
    bool stop = false;
    TraverseStageTree(
        stage->rootNode,
        [&stage, &stop, &meshInstanceNodes](const StageNodePtr& stageNode)
        {
            for (size_t i = 0; i < stageNode->staticMeshInstances.size(); i++)
            {
                size_t meshIndex = stageNode->staticMeshInstances[i];
                auto mesh = stage->meshes[meshIndex];
                auto iter = meshInstanceNodes.find(mesh);
                if (iter == meshInstanceNodes.end())
                {
                    meshInstanceNodes.insert({ mesh, stageNode });
                }
                else
                {
                    stop = true;
                    break;
                }
            }

            return !stop;
        }
    );

    if (stop || meshInstanceNodes.empty())
    {
        return;
    }

    // If it has any animations on top of mesh nodes, skipping the merge.
    for (const auto& meshNodePair : meshInstanceNodes)
    {
        auto parentNode = meshNodePair.second;

        while (parentNode && parentNode->transformAnimationTracks.empty())
        {
            parentNode = parentNode->parent.lock();
        }

        if (parentNode)
        {
            return;
        }
    }

    // Merge all mesh instances.
    auto mergedMesh = std::make_shared<Mesh>();
    mergedMesh->name = mergedMeshName;
    for (const auto& meshNodePair : meshInstanceNodes)
    {
        StageUtils::MergeMesh(mergedMesh, meshNodePair.first, meshNodePair.second->worldTransformMatrix);
    }
    stage->meshes = { mergedMesh };

    // Clear all instances, and put the merged mesh to the first instance node.
    for (const auto& meshNodePair : meshInstanceNodes)
    {
        meshNodePair.second->staticMeshInstances.clear();
    }

    stage->rootNode->staticMeshInstances.push_back(0);
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
