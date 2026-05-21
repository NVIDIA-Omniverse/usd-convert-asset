// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once
#include "../stage.h"

class MathUtils
{
public:

    // Assumes rotationOrder is XYZ.
    static TranslateEulerScaleTransform GfMatrixToTRS(const PXR_NS::GfMatrix4d& matrix);
    static TranslateQuatScaleTransform GfMatrixToTQS(const PXR_NS::GfMatrix4d& matrix);

    // From https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
    static PXR_NS::GfVec3d QuatenionToEulerAngles(const PXR_NS::GfQuatd& quat);
    static PXR_NS::GfQuatd EulerAnglesToQuatenion(const PXR_NS::GfVec3d& eulerAngles);
};
