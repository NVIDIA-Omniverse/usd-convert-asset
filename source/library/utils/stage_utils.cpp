// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "stage_utils.h"

#include "../common/common.h"

#include <numeric>


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

void StageUtils::MergeMesh(const MeshPtr& targetMesh, const MeshPtr& srcMesh, const PXR_NS::GfMatrix4d& srcTransform)
{
    if (srcMesh->points.empty())
    {
        return;
    }

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
        [targetPointsCount](size_t index)
        {
            return index + targetPointsCount;
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
        if (i < srcMesh->uvs.size())
        {
            std::copy(srcMesh->uvs[i].begin(), srcMesh->uvs[i].end(), std::back_inserter(targetMesh->uvs[i]));
        }
        else
        {
            PXR_NS::VtVec2fArray uv(totalSrcFaceVertices, PXR_NS::GfVec2f(0.0f, 0.0f));
            std::copy(uv.begin(), uv.end(), std::back_inserter(targetMesh->uvs[i]));
        }
    }

    // Handles color merge
    size_t numColors = std::max(srcMesh->colors.size(), targetMesh->colors.size());
    for (size_t i = targetMesh->colors.size(); i < numColors; i++)
    {
        PXR_NS::VtVec3fArray colors(totalTargetFaceVertices, PXR_NS::GfVec3f(1.0f, 1.0f, 1.0f));
        targetMesh->colors.push_back(colors);
    }

    for (size_t i = 0; i < numColors; i++)
    {
        if (i < srcMesh->colors.size())
        {
            std::copy(srcMesh->colors[i].begin(), srcMesh->colors[i].end(), std::back_inserter(targetMesh->colors[i]));
        }
        else
        {
            PXR_NS::VtVec3fArray colors(totalSrcFaceVertices, PXR_NS::GfVec3f(1.0f, 1.0f, 1.0f));
            std::copy(colors.begin(), colors.end(), std::back_inserter(targetMesh->colors[i]));
        }
    }

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
