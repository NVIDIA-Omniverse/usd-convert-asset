// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../pxr_includes.h"

#include <vector>

struct LinearSweptSphere
{
    uint32_t degree;
    std::vector<uint32_t> indices;
    std::vector<PXR_NS::GfVec3f> points;
    std::vector<float> radius;
    std::vector<PXR_NS::GfVec2f> texCoords;
};
