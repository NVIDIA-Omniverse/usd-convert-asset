// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../stage.h"


using OnStageNodePreCallback = std::function<bool(const StageNodePtr&)>;
using OnStageNodePostCallback = std::function<void(const StageNodePtr&)>;

class StageUtils
{
public:

    // Optimize mesh to merge same points.
    static void OptimizeMeshes(const StagePtr& stage);

    // Merges src to target mesh as a geomsubset.
    static void MergeMesh(const MeshPtr& targetMesh, const MeshPtr& srcMesh, const PXR_NS::GfMatrix4d& srcTransform = PXR_NS::GfMatrix4d(1.0));

    // Merges all meshes of a stage to single one if those meshes satisfy the following conditions:
    // 1. No skinning meshes are there.
    // 2. Meshes should all be under the same transform root.
    // 3. Meshes that have multiple instances should not be merged.
    static void MergeMeshes(const StagePtr& stage, const std::string& mergedMeshName);

    // Preorder traverse, callback will be called before traversing children. And endNodeCallback
    // will be called after children tranversing. If callback return false, it will stop traversing the children.
    static void TraverseStageTree(const StageNodePtr& stageNode, OnStageNodePreCallback callback, OnStageNodePostCallback endNodeCallback = nullptr);
};
