// SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "stage.h"

#include "utils/utils.h"


void Transform::SetPivot(const PXR_NS::GfVec3d& pivot)
{
    if (!PXR_NS::GfIsClose(pivot, mPivot, 1e-6))
    {
        mPivot = pivot;
        mMatrixIsDirty = true;
    }
}

PXR_NS::GfVec3d Transform::GetPivot() const
{
    return mPivot;
}

void Transform::SetTES(const TranslateEulerScaleTransform& tes)
{
    if (tes != mTES)
    {
        mTES = tes;
        mTESIsDirty = false;
        mTQSIsDirty = true;
        mMatrixIsDirty = true;
    }
}

TranslateEulerScaleTransform Transform::GetTES() const
{
    if (mTESIsDirty)
    {
        if (!mMatrixIsDirty)
        {
            mTES = MathUtils::GfMatrixToTRS(mMatrix);
        }
        else if (!mTQSIsDirty)
        {
            mTES.t = mTQS.t;
            mTES.s = mTQS.s;
            mTES.r = MathUtils::QuatenionToEulerAngles(mTQS.q);
        }
    }
    mTESIsDirty = false;

    return mTES;
}

void Transform::SetTQS(const TranslateQuatScaleTransform& tqs)
{
    if (tqs != mTQS)
    {
        mTQS = tqs;
        mTQSIsDirty = false;
        mMatrixIsDirty = true;
        mTESIsDirty = true;
    }
}

TranslateQuatScaleTransform Transform::GetTQS() const
{
    if (mTQSIsDirty)
    {
        if (!mMatrixIsDirty)
        {
            mTQS = MathUtils::GfMatrixToTQS(mMatrix);
        }
        else if (!mTESIsDirty)
        {
            mTQS.t = mTES.t;
            mTQS.s = mTES.s;
            mTQS.q = MathUtils::EulerAnglesToQuatenion(mTES.r);
        }
    }
    mTQSIsDirty = false;

    return mTQS;
}

void Transform::SetMatrix(const PXR_NS::GfMatrix4d& matrix)
{
    if (!PXR_NS::GfIsClose(matrix, mMatrix, 1e-6))
    {
        mMatrix = matrix;
        mMatrixIsDirty = false;
        mTESIsDirty = true;
        mTQSIsDirty = true;
    }
}

PXR_NS::GfMatrix4d Transform::GetMatrix() const
{
    if (mMatrixIsDirty)
    {
        PXR_NS::GfTransform transform;
        PXR_NS::GfQuatd rotation;
        if (!mTQSIsDirty)
        {
            rotation = mTQS.q;
        }
        else if (!mTESIsDirty)
        {
            rotation = MathUtils::EulerAnglesToQuatenion(mTES.r);
            mTQS.t = mTES.t;
            mTQS.s = mTES.s;
            mTQS.q = rotation;
            mTQSIsDirty = false;
        }
        transform.SetScale(mTQS.s);
        transform.SetRotation(mTQS.q);
        transform.SetTranslation(mTQS.t);
        transform.SetPivotPosition(mPivot);
        mMatrix = transform.GetMatrix();
    }
    mMatrixIsDirty = false;

    return mMatrix;
}

PXR_NS::GfVec3d Transform::GetTranslate() const
{
    if (!mTQSIsDirty)
    {
        return mTQS.t;
    }
    else
    {
        return GetTES().t;
    }
}

PXR_NS::GfVec3d Transform::GetScale() const
{
    if (!mTQSIsDirty)
    {
        return mTQS.s;
    }
    else
    {
        return GetTES().s;
    }
}

void Transform::SetScale(const PXR_NS::GfVec3d& scale)
{
    mTES.s = scale;
    mTQS.s = scale;
    mMatrix.SetScale(scale);
}

PXR_NS::GfVec3d Transform::GetRotationXYZ() const
{
    return GetTES().r;
}

bool Transform::IsIdentity() const
{
    if (!mMatrixIsDirty)
    {
        return mMatrix == PXR_NS::GfMatrix4d(1.0) && mPivot == ZERO_VEC_3D;
    }
    else if (!mTESIsDirty)
    {
        return mTES.t == ZERO_VEC_3D && mTES.s == ONE_VEC_3D && mTES.r == ZERO_VEC_3D && mPivot == ZERO_VEC_3D;
    }
    else
    {
        return mTQS.t == ZERO_VEC_3D && mTQS.s == ONE_VEC_3D && mTQS.q == PXR_NS::GfQuatd(1.0) && mPivot == ZERO_VEC_3D;
    }
}

bool Transform::operator==(const Transform& other) const
{
    if (!mTESIsDirty)
    {
        return mTES == other.GetTES() && mPivot == other.mPivot;
    }
    else if (!mTQSIsDirty)
    {
        return mTQS == other.GetTQS() && mPivot == other.mPivot;
    }
    else
    {
        return mMatrix == other.GetMatrix() && mPivot == other.mPivot;
    }
}

bool TransformTimesamples::Empty() const
{
    return mTranslations.empty() && mScales.empty() && mRotationXYZ.empty() && mOrients.empty();
}

const PXR_NS::VtVec3dArray& TransformTimesamples::GetRotationXYZSamples() const
{
    if (mRotationXYZ.empty() && !mOrients.empty())
    {
        for (const auto& orient : mOrients)
        {
            mRotationXYZ.push_back(MathUtils::QuatenionToEulerAngles(orient));
        }
    }

    return mRotationXYZ;
}

const PXR_NS::VtQuatdArray& TransformTimesamples::GetOrientSamples() const
{
    if (mOrients.empty() && !mRotationXYZ.empty())
    {
        for (const auto& rotationXYZ : mRotationXYZ)
        {
            mOrients.push_back(MathUtils::EulerAnglesToQuatenion(rotationXYZ));
        }
    }

    return mOrients;
}

PXR_NS::GfMatrix4d StageNode::ComputeLocalToWorldTransform(size_t animTrackIndex, size_t frameIndex)
{
    PXR_NS::GfMatrix4d worldTransform(1.0);
    auto parent = this;
    while (parent)
    {
        if (animTrackIndex < parent->transformAnimationTracks.size())
        {
            const auto& animationTrack = parent->transformAnimationTracks[animTrackIndex];
            if (frameIndex < animationTrack.Size())
            {
                const auto& translate = animationTrack.GetTranslationSamples()[frameIndex];
                const auto& scale = animationTrack.GetScaleSamples()[frameIndex];
                Transform transform;
                if (useTES)
                {
                    const auto& eulerAngles = animationTrack.GetRotationXYZSamples()[frameIndex];
                    TranslateEulerScaleTransform tes(translate, eulerAngles, scale);
                    transform.SetTES(tes);
                }
                else
                {
                    const auto& orient = animationTrack.GetOrientSamples()[frameIndex];
                    TranslateQuatScaleTransform tqs(translate, orient, scale);
                    transform.SetTQS(tqs);
                }

                worldTransform *= transform.GetMatrix();
            }
            else
            {
                worldTransform *= parent->localTransform.GetMatrix();
            }
        }
        else
        {
            worldTransform *= parent->localTransform.GetMatrix();
        }

        auto parentSharedPtr = parent->parent.lock();
        if (parentSharedPtr)
        {
            parent = parentSharedPtr.get();
        }
        else
        {
            parent = nullptr;
        }
    }

    return worldTransform;
}
