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

    // Cleans all meshes after import.
    static bool CleanupMeshes(const StagePtr& stage, std::string& detailedError);

    // Removes points that no face references and remaps point-domain mesh data.
    static bool CompactMeshPoints(const StagePtr& stage, const MeshPtr& mesh);

    // Generates angle-weighted point normals when the mesh has no normals.
    static bool GenerateSmoothNormals(const MeshPtr& mesh, bool replaceExistingNormals = false);

    // Optimize mesh to merge same points.
    static void OptimizeMeshes(const StagePtr& stage);

    // Merges src to target mesh as a geomsubset.
    static void MergeMesh(const MeshPtr& targetMesh, const MeshPtr& srcMesh, const PXR_NS::GfMatrix4d& srcTransform = PXR_NS::GfMatrix4d(1.0));

    // Merges eligible meshes into separate single-sided and double-sided groups:
    // 1. No skinning meshes are there.
    // 2. No point-cache animation is present.
    // 3. No point-cloud meshes are present.
    // 4. Meshes should all be under the same transform root.
    // 5. Meshes that have multiple instances should not be merged.
    static void MergeMeshes(const StagePtr& stage, const std::string& mergedMeshName);

    // Preorder traverse, callback will be called before traversing children. And endNodeCallback
    // will be called after children tranversing. If callback return false, it will stop traversing the children.
    static void TraverseStageTree(const StageNodePtr& stageNode, OnStageNodePreCallback callback, OnStageNodePostCallback endNodeCallback = nullptr);
};
