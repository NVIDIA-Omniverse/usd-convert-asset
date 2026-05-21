// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "common.h"

void VertexBoneData::addBoneData(uint32_t boneId, float weight)
{
    bool found = false;
    // If it's alreay added
    for (std::size_t i = 0; i < ids.size(); i++)
    {
        if (ids[i] == boneId)
        {
            weights[i] += weight;
            found = true;
            break;
        }
    }

    // New bone id
    if (!found)
    {
        ids.push_back(boneId);
        weights.push_back(weight);
    }
}
