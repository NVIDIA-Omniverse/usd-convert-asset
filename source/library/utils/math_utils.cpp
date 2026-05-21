// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "math_utils.h"


TranslateEulerScaleTransform MathUtils::GfMatrixToTRS(const PXR_NS::GfMatrix4d& matrix)
{
    PXR_NS::GfMatrix4d rotMat(1.0);
    PXR_NS::GfMatrix4d scaleOrientMatUnused, perspMatUnused;
    PXR_NS::GfVec3d scaling;
    PXR_NS::GfVec3d translation;
    matrix.Factor(&scaleOrientMatUnused, &scaling, &rotMat, &translation, &perspMatUnused);

    // By default decompose as XYZ order (make it an option?)
    auto angles = rotMat.ExtractRotation().Decompose(PXR_NS::GfVec3d::ZAxis(), PXR_NS::GfVec3d::YAxis(), PXR_NS::GfVec3d::XAxis());
    PXR_NS::GfVec3d rotation(angles[2], angles[1], angles[0]);

    TranslateEulerScaleTransform trs(translation, rotation, scaling);

    return trs;
}

TranslateQuatScaleTransform MathUtils::GfMatrixToTQS(const PXR_NS::GfMatrix4d& matrix)
{
    PXR_NS::GfMatrix4d rotMat(1.0);
    PXR_NS::GfMatrix4d scaleOrientMatUnused, perspMatUnused;
    PXR_NS::GfQuatd rotation;
    PXR_NS::GfVec3d scaling;
    PXR_NS::GfVec3d translation;
    matrix.Factor(&scaleOrientMatUnused, &scaling, &rotMat, &translation, &perspMatUnused);
    rotation = rotMat.ExtractRotationQuat();

    TranslateQuatScaleTransform tqs(translation, rotation, scaling);

    return tqs;
}

PXR_NS::GfVec3d MathUtils::QuatenionToEulerAngles(const PXR_NS::GfQuatd& quat)
{
    PXR_NS::GfRotation rot(quat);
    auto angles = rot.Decompose(PXR_NS::GfVec3d::ZAxis(), PXR_NS::GfVec3d::YAxis(), PXR_NS::GfVec3d::XAxis());
    return { angles[2], angles[1], angles[0] };
}

PXR_NS::GfQuatd MathUtils::EulerAnglesToQuatenion(const PXR_NS::GfVec3d& eulerAngles)
{
#define PI 3.14159265358979323846
    double roll = eulerAngles[0] * PI / 180.0;
    double pitch = eulerAngles[1] * PI / 180.0;
    double yaw = eulerAngles[2] * PI / 180.0;

    // Abbreviations for the various angular functions
    double cy = cos(yaw * 0.5);
    double sy = sin(yaw * 0.5);
    double cp = cos(pitch * 0.5);
    double sp = sin(pitch * 0.5);
    double cr = cos(roll * 0.5);
    double sr = sin(roll * 0.5);

    PXR_NS::GfQuatd q;
    q.SetReal(cr * cp * cy + sr * sp * sy);

    PXR_NS::GfVec3d im;
    im[0] = sr * cp * cy - cr * sp * sy;
    im[1] = cr * sp * cy + sr * cp * sy;
    im[2] = cr * cp * sy - sr * sp * cy;
    q.SetImaginary(im);

    return q;
}
