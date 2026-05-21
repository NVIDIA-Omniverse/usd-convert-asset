// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include <stdint.h>
#include <vector>

struct VertexBoneData
{
    std::vector<uint32_t> ids;
    std::vector<float> weights;
    void addBoneData(uint32_t boneId, float weight);
};
