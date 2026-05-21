// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "usd_importer.h"

#include "../../pxr_includes.h"
#include "../../utils/utils.h"
#include "usd_type_traits.h"

#include <cmath>
#include <numeric>
#include <queue>

using namespace omni::assetconverter::importer::usd;


// Helper to get value
template <typename ValueType>
static ValueType GetUSDValue(const PXR_NS::UsdAttribute& attribute, const PXR_NS::UsdTimeCode& timeCode = PXR_NS::UsdTimeCode::Default())
{
    ValueType value = ValueType();
    if (attribute)
    {
        attribute.Get(&value, timeCode);
    }

    return value;
}

// Helper to get value
template <typename ValueType>
static ValueType GetDefaultOrFirstTimeSampledValue(const PXR_NS::UsdAttribute& attribute)
{
    ValueType value = ValueType();
    if (attribute)
    {
        attribute.Get(&value);
    }

    if (value.empty())
    {
        std::vector<double> timeSamples;
        attribute.GetTimeSamples(&timeSamples);
        if (timeSamples.size() > 0)
        {
            attribute.Get(&value, PXR_NS::UsdTimeCode(timeSamples[0]));
        }
    }

    return value;
}

template <typename ValueType>
static bool GetInputValue(const PXR_NS::UsdShadeInput& input, ValueType& value, const PXR_NS::UsdTimeCode& timeCode = PXR_NS::UsdTimeCode::Default())
{
    if (!input || !input.Get(&value, timeCode))
    {
        return false;
    }

    return true;
}

template <typename ValueType>
static bool GetShaderInputValue(
    ValueType& value,
    const PXR_NS::UsdShadeShader& shader,
    const std::string& inputName,
    const PXR_NS::UsdTimeCode& timeCode = PXR_NS::UsdTimeCode::Default()
)
{
    auto input = shader.GetInput(PXR_NS::TfToken(inputName));
    if (!input || input.HasConnectedSource())
    {
        return false;
    }

    return GetInputValue<ValueType>(input, value, timeCode);
}

template <typename ScalarType>
static void ComputeFlattened(
    const PXR_NS::UsdGeomPrimvar& primvar,
    PXR_NS::VtArray<ScalarType>* value,
    const PXR_NS::UsdTimeCode& timeCode = PXR_NS::UsdTimeCode::Default()
)
{
    if (primvar)
    {
        primvar.ComputeFlattened(value, timeCode);
    }
}

template <typename ScalarType>
static void ComputeDefaultOrFirstFrameFlattened(const PXR_NS::UsdGeomPrimvar& primvar, PXR_NS::VtArray<ScalarType>* value)
{
    if (primvar)
    {
        primvar.ComputeFlattened(value);
    }

    if (value->empty())
    {
        std::vector<double> timeSamples;
        primvar.GetTimeSamples(&timeSamples);
        if (timeSamples.size() > 0)
        {
            primvar.ComputeFlattened(value, PXR_NS::UsdTimeCode(timeSamples[0]));
        }
    }
}

template <typename ScalarType>
static PXR_NS::VtArray<ScalarType> ToFaceVaryingData(
    const PXR_NS::VtArray<ScalarType>& data,
    const PXR_NS::VtArray<int>& faceVertexCounts,
    const PXR_NS::VtArray<int>& faceVertexIndices,
    const PXR_NS::TfToken& interpolationType
)
{
    if (interpolationType == PXR_NS::UsdGeomTokens->faceVarying)
    {
        // It's possible that it includes invalid data.
        if (faceVertexIndices.size() == data.size())
        {
            return data;
        }
    }
    else if (interpolationType == PXR_NS::UsdGeomTokens->vertex || interpolationType == PXR_NS::UsdGeomTokens->varying)
    {
        PXR_NS::VtArray<ScalarType> faceVaryingData(faceVertexIndices.size());
        for (size_t i = 0; i < faceVertexIndices.size(); i++)
        {
            int faceVertexIndex = faceVertexIndices[i];
            if (faceVertexIndex < data.size())
            {
                faceVaryingData[i] = data[faceVertexIndex];
            }
        }

        return faceVaryingData;
    }
    else if (interpolationType == PXR_NS::UsdGeomTokens->uniform)
    {
        PXR_NS::VtArray<ScalarType> faceVaryingData(faceVertexIndices.size());
        size_t totalFaceVertices = 0;
        for (size_t i = 0; i < faceVertexCounts.size(); i++)
        {
            if (i < data.size())
            {
                for (size_t j = totalFaceVertices; j < totalFaceVertices + faceVertexCounts[i]; j++)
                {
                    faceVaryingData[j] = data[i];
                }
            }
            totalFaceVertices += faceVertexCounts[i];
        }

        return faceVaryingData;
    }
    else if (interpolationType == PXR_NS::UsdGeomTokens->constant)
    {
        if (data.size() > 0)
        {
            PXR_NS::VtArray<ScalarType> faceVaryingData(faceVertexIndices.size());
            const auto& constant = data[0];
            for (size_t i = 0; i < faceVaryingData.size(); i++)
            {
                faceVaryingData[i] = constant;
            }

            return faceVaryingData;
        }
    }

    return {};
}

template <typename ScalarType>
static PXR_NS::VtArray<ScalarType> ToVertexData(const PXR_NS::VtArray<ScalarType>& data, size_t numVertices, const PXR_NS::TfToken& interpolationType)
{
    if (interpolationType == PXR_NS::UsdGeomTokens->faceVarying)
    {
        // Not valid
    }
    else if (interpolationType == PXR_NS::UsdGeomTokens->vertex || interpolationType == PXR_NS::UsdGeomTokens->varying)
    {
        if (data.size() == numVertices)
        {
            return data;
        }
    }
    else if (interpolationType == PXR_NS::UsdGeomTokens->uniform)
    {
        // Not valid
    }
    else if (interpolationType == PXR_NS::UsdGeomTokens->constant)
    {
        if (data.size() > 0)
        {
            PXR_NS::VtArray<ScalarType> vertexData(numVertices);
            const auto& constant = data[0];
            for (size_t i = 0; i < vertexData.size(); i++)
            {
                vertexData[i] = constant;
            }

            return vertexData;
        }
    }

    return {};
}

template <typename ValueType, typename Traits = TypeTraits<ValueType>>
static bool GetValueFromOp(PXR_NS::UsdGeomXformOp op, ValueType& value, PXR_NS::UsdTimeCode timeCode)
{
    bool success = false;
    if (op.GetPrecision() == PXR_NS::UsdGeomXformOp::PrecisionHalf)
    {
        typename Traits::HalfType halfValue;
        success = op.GetAs<typename Traits::HalfType>(&halfValue, timeCode);
        value = ValueType(halfValue);
    }
    else if (op.GetPrecision() == PXR_NS::UsdGeomXformOp::PrecisionDouble)
    {
        typename Traits::DoubleType doubleType;
        success = op.GetAs<typename Traits::DoubleType>(&doubleType, timeCode);
        value = ValueType(doubleType);
    }
    else
    {
        typename Traits::FloatType floatType;
        success = op.GetAs<typename Traits::FloatType>(&floatType, timeCode);
        value = ValueType(floatType);
    }

    return success;
}

bool IsRotationType(PXR_NS::UsdGeomXformOp::Type opType)
{
    switch (opType)
    {
        case PXR_NS::UsdGeomXformOp::Type::TypeRotateX:
        case PXR_NS::UsdGeomXformOp::Type::TypeRotateY:
        case PXR_NS::UsdGeomXformOp::Type::TypeRotateZ:
        case PXR_NS::UsdGeomXformOp::Type::TypeRotateXYZ:
        case PXR_NS::UsdGeomXformOp::Type::TypeRotateXZY:
        case PXR_NS::UsdGeomXformOp::Type::TypeRotateYXZ:
        case PXR_NS::UsdGeomXformOp::Type::TypeRotateYZX:
        case PXR_NS::UsdGeomXformOp::Type::TypeRotateZXY:
        case PXR_NS::UsdGeomXformOp::Type::TypeRotateZYX:
            return true;
        default:
            return false;
    }
}

PXR_NS::GfVec3d ConvertToXYZ(const PXR_NS::UsdGeomXformOp& op, double timeCode)
{
    PXR_NS::UsdGeomXformOp::Type opType = op.GetOpType();
    PXR_NS::GfVec3d result(0.0);

    bool isScalar = opType == PXR_NS::UsdGeomXformOp::Type::TypeRotateX || opType == PXR_NS::UsdGeomXformOp::Type::TypeRotateY ||
                    opType == PXR_NS::UsdGeomXformOp::Type::TypeRotateZ;
    if (isScalar)
    {
        uint32_t scalarDestIndex = 0;
        switch (opType)
        {
            case PXR_NS::UsdGeomXformOp::Type::TypeRotateX:
                scalarDestIndex = 0;
                break;
            case PXR_NS::UsdGeomXformOp::Type::TypeRotateY:
                scalarDestIndex = 1;
                break;
            case PXR_NS::UsdGeomXformOp::Type::TypeRotateZ:
                scalarDestIndex = 2;
                break;
        }

        double scalar;
        op.GetAs<double>(&scalar, timeCode);

        result[scalarDestIndex] = scalar;
    }
    else
    {
        std::array<uint32_t, 3> swizzle = { 0, 1, 2 };
        switch (opType)
        {
            case PXR_NS::UsdGeomXformOp::Type::TypeRotateXYZ:
                swizzle = { 0, 1, 2 };
                break;
            case PXR_NS::UsdGeomXformOp::Type::TypeRotateXZY:
                swizzle = { 0, 2, 1 };
                break;
            case PXR_NS::UsdGeomXformOp::Type::TypeRotateYXZ:
                swizzle = { 1, 0, 2 };
                break;
            case PXR_NS::UsdGeomXformOp::Type::TypeRotateYZX:
                swizzle = { 2, 0, 1 };
                break;
            case PXR_NS::UsdGeomXformOp::Type::TypeRotateZXY:
                swizzle = { 1, 2, 0 };
                break;
            case PXR_NS::UsdGeomXformOp::Type::TypeRotateZYX:
                swizzle = { 2, 1, 0 };
                break;
        }

        PXR_NS::GfVec3d value;
        GetValueFromOp<PXR_NS::GfVec3d>(op, value, timeCode);

        for (uint32_t i = 0; i < swizzle.size(); i++)
        {
            result[i] = value[swizzle[i]];
        }
    }

    return result;
}

static PXR_NS::VtArray<PXR_NS::GfVec3d> GetRotationOpTimesamples(const PXR_NS::UsdGeomXformable& xform, double startTime, double endTime)
{
    PXR_NS::VtArray<PXR_NS::GfVec3d> sampleValues;
    bool resetXformStack = false;
    auto ops = xform.GetOrderedXformOps(&resetXformStack);
    for (size_t i = 0; i < ops.size(); i++)
    {
        const auto& op = ops[i];
        if (IsRotationType(op.GetOpType()) && !op.IsInverseOp())
        {
            std::vector<double> timeSamples;
            op.GetTimeSamples(&timeSamples);
            if (op.GetNumTimeSamples() == 0)
            {
                return {};
            }

            // need filler values from start time, but after end time no additional values needed.
            endTime = std::min(endTime, timeSamples.back());
            for (double timeCode = startTime; timeCode <= endTime; timeCode += 1.0)
            {
                sampleValues.push_back(ConvertToXYZ(op, timeCode));
            }

            return sampleValues;
        }
    }

    return {};
}

template <typename ValueType>
static PXR_NS::VtArray<ValueType> GetXformOpTimesamples(
    const PXR_NS::UsdGeomXformable& xform,
    PXR_NS::UsdGeomXformOp::Type opType,
    double startTime,
    double endTime
)
{
    PXR_NS::VtArray<ValueType> sampleValues;
    bool resetXformStack = false;
    auto ops = xform.GetOrderedXformOps(&resetXformStack);
    for (size_t i = 0; i < ops.size(); i++)
    {
        const auto& op = ops[i];
        if (op.GetOpType() == opType && !op.IsInverseOp())
        {
            // Skip pivot
            if (opType == PXR_NS::UsdGeomXformOp::TypeTranslate && op.HasSuffix(PXR_NS::TfToken("pivot")))
            {
                continue;
            }

            std::vector<double> timeSamples;
            op.GetTimeSamples(&timeSamples);
            if (op.GetNumTimeSamples() == 0)
            {
                return {};
            }

            // need filler values from start time, but after end time no additional values needed.
            endTime = std::min(endTime, timeSamples.back());
            for (double timeCode = startTime; timeCode <= endTime; timeCode += 1.0)
            {
                ValueType value;
                GetValueFromOp<ValueType>(op, value, timeCode);
                sampleValues.push_back(value);
            }

            return sampleValues;
        }
    }

    return {};
}

static TransformTimesamples GetTransformTimesamples(const PXR_NS::UsdGeomXformable& xform, double startTime, double endTime)
{
    bool resetXformStack = false;
    PXR_NS::VtArray<PXR_NS::GfVec3d> translations;
    PXR_NS::VtArray<PXR_NS::GfVec3d> rotations;
    PXR_NS::VtArray<PXR_NS::GfVec3d> scales;

    // need filler values from start time, but after end time no additional values needed.
    for (double timeCode = startTime; timeCode <= endTime; timeCode += 1.0)
    {
        TranslateEulerScaleTransform trs;
        PXR_NS::GfMatrix4d localMatrix(1.0);
        xform.GetLocalTransformation(&localMatrix, &resetXformStack, timeCode);

        PXR_NS::GfMatrix4d rotMat(1.0);
        PXR_NS::GfMatrix4d scaleOrientMatUnused, perspMatUnused;
        PXR_NS::GfQuatd rotation;
        localMatrix.Factor(&scaleOrientMatUnused, &trs.s, &rotMat, &trs.t, &perspMatUnused);

        // By default decompose as XYZ order (make it an option?)
        auto angles = rotMat.ExtractRotation().Decompose(PXR_NS::GfVec3d::ZAxis(), PXR_NS::GfVec3d::YAxis(), PXR_NS::GfVec3d::XAxis());
        trs.r = { angles[2], angles[1], angles[0] };
        translations.push_back(trs.t);
        rotations.push_back(trs.r);
        scales.push_back(trs.s);
    }

    return TransformTimesamples(translations, rotations, scales);
}

static Transform GetLocalTransform(
    const PXR_NS::UsdPrim& prim,
    bool supportPivot,
    const PXR_NS::UsdTimeCode& timeCode = PXR_NS::UsdTimeCode::Default()
)
{
    PXR_NS::GfVec3d pivot(0.0);
    TranslateEulerScaleTransform trs;
    PXR_NS::UsdGeomXformable xform(prim);
    bool resetXformStack = false;
    auto ops = xform.GetOrderedXformOps(&resetXformStack);
    bool seenTranslation = false;
    bool seenRotationOrient = false;
    bool seenRotationXYZ = false;
    bool seenPivot = false;
    bool seenScaling = false;

    bool supported = true;
    for (size_t i = 0; i < ops.size(); i++)
    {
        const auto& op = ops[i];
        if (op.GetOpType() == PXR_NS::UsdGeomXformOp::TypeTranslate)
        {
            if (op.HasSuffix(PXR_NS::TfToken("pivot")))
            {
                if (op.IsInverseOp())
                {
                    if (i != ops.size() - 1)
                    {
                        supported = false;
                        break;
                    }
                }
                else if (seenRotationOrient || seenRotationXYZ || seenScaling)
                {
                    supported = false;
                    break;
                }
                else
                {
                    seenPivot = true;
                    GetValueFromOp<PXR_NS::GfVec3d>(op, pivot, timeCode);
                }
            }
            else if (seenTranslation || seenRotationOrient || seenRotationXYZ || seenPivot || seenScaling)
            {
                supported = false;
                break;
            }
            else
            {
                seenTranslation = true;
                GetValueFromOp<PXR_NS::GfVec3d>(op, trs.t, timeCode);
            }
        }
        else if (op.GetOpType() == PXR_NS::UsdGeomXformOp::TypeOrient)
        {
            if (seenRotationOrient || seenRotationXYZ || seenScaling)
            {
                supported = false;
                break;
            }
            else
            {
                seenRotationOrient = true;
                auto angles = op.GetOpTransform(timeCode).ExtractRotation().Decompose(
                    PXR_NS::GfVec3d::ZAxis(),
                    PXR_NS::GfVec3d::YAxis(),
                    PXR_NS::GfVec3d::XAxis()
                );
                trs.r = { angles[2], angles[1], angles[0] };
            }
        }
        else if (op.GetOpType() == PXR_NS::UsdGeomXformOp::TypeRotateXYZ)
        {
            if (seenRotationOrient || seenRotationXYZ)
            {
                supported = false;
                break;
            }
            else
            {
                seenRotationXYZ = true;
                auto angles = op.GetOpTransform(timeCode).ExtractRotation().Decompose(
                    PXR_NS::GfVec3d::ZAxis(),
                    PXR_NS::GfVec3d::YAxis(),
                    PXR_NS::GfVec3d::XAxis()
                );
                trs.r = { angles[2], angles[1], angles[0] };
            }
        }
        else if (op.GetOpType() == PXR_NS::UsdGeomXformOp::TypeScale)
        {
            seenScaling = true;
            GetValueFromOp<PXR_NS::GfVec3d>(op, trs.s, timeCode);
        }
        else
        {
            supported = false;
            break;
        }
    }

    Transform transform;
    bool hasPivot = seenPivot && !PXR_NS::GfIsClose(pivot, ZERO_VEC_3D, 1e-6);
    if (!supported || (!supportPivot && hasPivot))
    {
        PXR_NS::GfMatrix4d localMatrix(1.0);
        xform.GetLocalTransformation(&localMatrix, &resetXformStack, timeCode);

        PXR_NS::GfMatrix4d rotMat(1.0);
        PXR_NS::GfMatrix4d scaleOrientMatUnused, perspMatUnused;
        PXR_NS::GfQuatd rotation;
        localMatrix.Factor(&scaleOrientMatUnused, &trs.s, &rotMat, &trs.t, &perspMatUnused);

        // By default decompose as XYZ order (make it an option?)
        auto angles = rotMat.ExtractRotation().Decompose(PXR_NS::GfVec3d::ZAxis(), PXR_NS::GfVec3d::YAxis(), PXR_NS::GfVec3d::XAxis());
        trs.r = { angles[2], angles[1], angles[0] };
    }
    else
    {
        transform.SetPivot(pivot);
    }
    transform.SetTES(trs);

    return transform;
}

StagePtr UsdSdkImporter::ImportStage(const OmniFutureThreadContextPtr& context, OmniConverterStatus& status, std::string& detailedError)
{
    mThreadContext = context;

    mThreadContext->converterContext.Log("Starting to import asset with UsdImporter...");
    PXR_NS::UsdStageRefPtr usdStage = mThreadContext->converterContext.GetCachedStage();

    // Explicitly create the session layer, as usdview does.
    // Works around a bug in Sdf where anonymous identifiers are computed using the root layer's
    // identifier, which may contain characters that can be treated as string format specifiers.
    // When the Sdf bug is addressed, we can once again use UsdStage::Open(rootLayer) below.
    //
    // NOTE: This does mean that any edits in the current session layer will be lost, something
    // that DOES NOT happen when a user has saved to the SAME file-format as the current stage.
    PXR_NS::SdfLayerRefPtr sessionLayer = PXR_NS::SdfLayer::CreateAnonymous();
    if (!usdStage)
    {
        auto rootLayer = PXR_NS::SdfLayer::FindOrOpen(mThreadContext->converterContext.GetImportAssetPath());
        if (rootLayer)
        {
            usdStage = PXR_NS::UsdStage::Open(rootLayer, sessionLayer);
        }
    }
    else
    {
        // Re-use root layer and session layer of existing stage.
        // FIXME: Since importer is inside another thread, and USD only supports multiple readers and one writers
        // to the same stage, that's why it creates another stage here so there are no multiple writers
        // to the same stage (because of variants switch and material watcher in Kit).
        // And all writes in this importer will be authored to created session layer to avoid
        // influence the original stage.
        sessionLayer->TransferContent(usdStage->GetSessionLayer());
        usdStage = PXR_NS::UsdStage::Open(usdStage->GetRootLayer(), sessionLayer);
    }

    if (!usdStage)
    {
        detailedError = "Failed to open stage " + mThreadContext->converterContext.GetImportAssetPath();
        mThreadContext->converterContext.Log(detailedError);
        status = OmniConverterStatus::FILE_READ_ERROR;
        return nullptr;
    }

    auto allChildren = usdStage->GetPseudoRoot().GetAllChildren();
    size_t rootPrimCount = 0;
    for (const auto& child : allChildren)
    {
        rootPrimCount += 1;
    }

    StagePtr stage = std::make_shared<Stage>();
    auto stageAxis = PXR_NS::UsdGeomGetStageUpAxis(usdStage);
    if (stageAxis == PXR_NS::UsdGeomTokens->z)
    {
        stage->yAxis = false;
    }
    else
    {
        stage->yAxis = true;
    }

    // override up-axis with user-defined value
    if (mThreadContext->converterContext.ConvertUpY())
    {
        stage->yAxis = true;
    }
    else if (mThreadContext->converterContext.ConvertUpZ())
    {
        stage->yAxis = false;
    }

    stage->worldUnits = PXR_NS::UsdGeomGetStageMetersPerUnit(usdStage);

    PXR_NS::SdfLayerRefPtr rootLayer = usdStage->GetRootLayer();
    double fps = 24.0;
    double maxKeyFrames = 0.0;
    double startTime = 0.0;
    double endTime = 0.0;
    if (rootLayer->HasEndTimeCode())
    {
        if (rootLayer->HasStartTimeCode())
        {
            startTime = rootLayer->GetStartTimeCode();
        }
        endTime = rootLayer->GetEndTimeCode();
        maxKeyFrames = (size_t)std::ceil(endTime - startTime) + 1;
        fps = rootLayer->GetFramesPerSecond();
        if (PXR_NS::GfIsClose(fps, 0.0, 1e-6))
        {
            fps = 24.0;
        }
    }

    stage->maxKeyFrames = maxKeyFrames;
    stage->mutiplier = fps / 24.0;
    stage->startTime = startTime;
    stage->endTime = endTime;
    if (rootPrimCount > 0 && maxKeyFrames > 0)
    {
        AnimationTrack animTrack;
        animTrack.fps = fps;
        animTrack.keyFrames = maxKeyFrames;

        auto defaultPrim = allChildren.front();
        if (defaultPrim.HasVariantSets())
        {
            auto variantSets = defaultPrim.GetVariantSets();
            if (variantSets.HasVariantSet(ANIMATION_TRACK_VARIANT_SET_NAME))
            {
                auto variantSet = variantSets.GetVariantSet(ANIMATION_TRACK_VARIANT_SET_NAME);
                auto allAnimationTrackNames = variantSet.GetVariantNames();
                for (const std::string& name : allAnimationTrackNames)
                {
                    animTrack.name = name;
                    stage->animationTracks.push_back(animTrack);
                }
            }
        }
        else
        {
            animTrack.name = rootLayer->GetDisplayName();
            stage->animationTracks.push_back(animTrack);
        }
    }
    mAllAnimationTracks = stage->animationTracks;

    if (rootPrimCount > 1)
    {
        auto node = std::make_shared<StageNode>("World");
        stage->rootNode = node;
        TraverseUsdStage(usdStage->GetPseudoRoot(), stage, stage->rootNode);
    }
    else if (rootPrimCount == 1)
    {
        TraverseUsdStage(allChildren.front(), stage, nullptr);
    }

    auto customLayerData = usdStage->GetRootLayer()->GetCustomLayerData();
    auto value = customLayerData.GetValueAtPath("cameraSettings:boundCamera");
    if (value && value->CanCast<std::string>())
    {
        const std::string& cameraPrimPath = value->Get<std::string>();
        for (size_t i = 0; i < stage->cameras.size(); i++)
        {
            if (mCameraPrimPaths[i].GetString() == cameraPrimPath)
            {
                stage->defaultBoundCamera = i;
                break;
            }
        }
    }


    status = OmniConverterStatus::OK;

    return stage;
}

void UsdSdkImporter::TraverseUsdStage(const PXR_NS::UsdPrim& currentPrim, StagePtr& stage, StageNodePtr parentNode)
{
    // HACK for Omniverse to avoid exporting builtin cameras.
    if (currentPrim.GetName().GetString().find("OmniverseKit_", 0) == 0 ||
        currentPrim.GetName().GetString().find("OmniverseKitViewportCameraMesh", 0) == 0)
    {
        return;
    }

    // Don't export hidden props if it's not requested.
    PXR_NS::UsdGeomImageable imageable(currentPrim);
    if (!mThreadContext->converterContext.ExportHiddenProps() && imageable && imageable.ComputeVisibility() == PXR_NS::UsdGeomTokens->invisible)
    {
        return;
    }

    // Skips to traverse node's chidren if necessary.
    bool skipTraverse = false;

    StageNodePtr node;
    if (currentPrim.GetPrimPath() != PXR_NS::SdfPath::AbsoluteRootPath())
    {
        // Skips children traverse since material does not need to create nodes and children other than materials or
        // shaders will be skipped.
        if (currentPrim.IsA<PXR_NS::UsdShadeMaterial>() && !mThreadContext->converterContext.IgnoreMaterials())
        {
            PopulateBoundMaterial(stage, currentPrim);
            return;
        }

        const auto& localTransform = GetLocalTransform(currentPrim, mThreadContext->converterContext.PivotSupportedForOutputFormat());

        PXR_NS::GfMatrix4d worldTransformMatrix;
        if (parentNode)
        {
            worldTransformMatrix = localTransform.GetMatrix() * parentNode->worldTransformMatrix;
        }
        else
        {
            worldTransformMatrix = localTransform.GetMatrix();
        }

        TransformAnimationTracks transformAnimationTracks;
        if (!mThreadContext->converterContext.IgnoreAnimations())
        {
            transformAnimationTracks = GetTransformAnimation(currentPrim, stage, !PXR_NS::GfIsClose(localTransform.GetPivot(), ZERO_VEC_3D, 1e-6));
        }

        // Skips children traverse as children other than skeleton related will be skipped.
        if (currentPrim.IsA<PXR_NS::UsdSkelRoot>() && !mThreadContext->converterContext.PopulateMaterialsOnly())
        {
            auto rootBoneNode = ImportSkeleton(currentPrim, stage);
            if (rootBoneNode)
            {
                if (!localTransform.IsIdentity() || !transformAnimationTracks.empty())
                {
                    node = std::make_shared<StageNode>(currentPrim.GetName());
                    node->children.push_back(rootBoneNode);
                    node->localTransform = localTransform;
                    node->worldTransformMatrix = worldTransformMatrix;
                    node->transformAnimationTracks = transformAnimationTracks;
                }
                else
                {
                    node = rootBoneNode;
                }
                skipTraverse = true;
            }
        }

        if (!node)
        {
            bool createNode = true;
            if (currentPrim.IsA<PXR_NS::UsdGeomMesh>() || currentPrim.IsA<PXR_NS::UsdGeomCamera>() || currentPrim.IsA<PXR_NS::UsdGeomBasisCurves>())
            {
                createNode = !currentPrim.GetAllChildren().empty();
            }

            // Prims like mesh/camera/lights don't have transform
            // in asset converter node. If the transform is not identity or has timesamples,
            // it needs to create node to attach prims under it.
            if (!transformAnimationTracks.empty() || createNode || !stage->rootNode || !localTransform.IsIdentity())
            {
                node = std::make_shared<StageNode>(currentPrim.GetName());
                node->localTransform = localTransform;
                node->worldTransformMatrix = worldTransformMatrix;
                node->transformAnimationTracks = transformAnimationTracks;
            }
            else
            {
                node = parentNode;
                skipTraverse = true;
            }

            if (currentPrim.IsA<PXR_NS::UsdGeomMesh>() && !mThreadContext->converterContext.PopulateMaterialsOnly())
            {
                PXR_NS::SdfPath meshPath;
                if (currentPrim.IsInstanceProxy())
                {
#if PXR_MINOR_VERSION > 21 || (PXR_MINOR_VERSION == 21 && PXR_PATCH_VERSION >= 8)
                    meshPath = currentPrim.GetPrimInPrototype().GetPath();
#else
                    meshPath = currentPrim.GetPrimInMaster().GetPath();
#endif
                }
                else
                {
                    meshPath = currentPrim.GetPath();
                }

                auto iter = mDefinedMeshes.find(meshPath);
                if (iter != mDefinedMeshes.end())
                {
                    node->staticMeshInstances.push_back(iter->second);
                }
                else
                {
                    PXR_NS::UsdGeomMesh meshPrim = PXR_NS::UsdGeomMesh(currentPrim);
                    PXR_NS::TfToken purpose = meshPrim.ComputePurpose();
                    if (purpose == PXR_NS::UsdGeomTokens->default_ || purpose == PXR_NS::UsdGeomTokens->render)
                    {
                        auto mesh = PopulateMesh(stage, meshPrim);
                        stage->meshes.push_back(mesh);
                        node->staticMeshInstances.push_back(stage->meshes.size() - 1);
                        mDefinedMeshes.insert({ meshPath, stage->meshes.size() - 1 });
                    }
                }
            }
            else if (currentPrim.IsA<PXR_NS::UsdGeomCamera>() && !mThreadContext->converterContext.IgnoreCameras() &&
                     !mThreadContext->converterContext.PopulateMaterialsOnly())
            {
                auto cameraPrim = PXR_NS::UsdGeomCamera(currentPrim);
                CameraPtr camera = PopulateCamera(cameraPrim);
                stage->cameras.push_back(camera);
                node->cameras.push_back(stage->cameras.size() - 1);
                mCameraPrimPaths.push_back(currentPrim.GetPrimPath());
            }
            else if (currentPrim.IsA<PXR_NS::UsdGeomBasisCurves>() && !mThreadContext->converterContext.PopulateMaterialsOnly())
            {
                PXR_NS::UsdGeomBasisCurves curvePrim = PXR_NS::UsdGeomBasisCurves(currentPrim);
                CurvePtr curve = PopulateCurve(stage, curvePrim);
                stage->curves.push_back(curve);
                node->curveInstances.push_back(stage->curves.size() - 1);
            }
#if PXR_MINOR_VERSION > 21 || (PXR_MINOR_VERSION == 21 && PXR_PATCH_VERSION >= 11)
            else if (currentPrim.HasAPI<PXR_NS::UsdLuxLightAPI>() && !mThreadContext->converterContext.IgnoreLights())
            {
                auto lightPrim = PXR_NS::UsdLuxLightAPI(currentPrim);
#else
            else if (currentPrim.IsA<PXR_NS::UsdLuxLight>() && !mThreadContext->converterContext.IgnoreLights())
            {
                auto lightPrim = PXR_NS::UsdLuxLight(currentPrim);
#endif
                auto light = PopulateLight(lightPrim);
                stage->lights.push_back(light);
                node->lights.push_back(stage->lights.size() - 1);
            }
        }

        if (!stage->rootNode)
        {
            stage->rootNode = node;
        }
        else if (parentNode != node)
        {
            parentNode->children.push_back(node);
            node->parent = parentNode;
        }
    }
    else
    {
        node = parentNode;
    }

    if (!skipTraverse)
    {
        auto childRange = currentPrim.GetFilteredChildren(PXR_NS::UsdTraverseInstanceProxies());
        for (auto childIter = childRange.begin(), childEnd = childRange.end(); childIter != childEnd; ++childIter)
        {
            const auto& childPrim = *childIter;
            TraverseUsdStage(childPrim, stage, node);
        }
    }
}

MeshPtr UsdSdkImporter::PopulateMesh(const StagePtr& stage, const PXR_NS::UsdGeomMesh& usdMesh)
{
    MeshPtr mesh = std::make_shared<Mesh>();
    mesh->name = usdMesh.GetPrim().GetName();

    // Add triangles
    auto pointsAttr = usdMesh.GetPointsAttr();
    mesh->points = GetDefaultOrFirstTimeSampledValue<PXR_NS::VtArray<PXR_NS::GfVec3f>>(usdMesh.GetPointsAttr());
    std::vector<double> timeSamples;
    double duration = 0.0;
    if (!mThreadContext->converterContext.IgnoreAnimations() && pointsAttr.GetTimeSamples(&timeSamples) && !timeSamples.empty())
    {
        mesh->timeSampleStart = std::max(stage->startTime, timeSamples.front());
        mesh->timeSampleEnd = std::min(stage->endTime, timeSamples.back());

        for (double time = timeSamples.front(); time <= timeSamples.back(); time += 1.0)
        {
            const auto& framePoints = GetUSDValue<PXR_NS::VtArray<PXR_NS::GfVec3f>>(pointsAttr, PXR_NS::UsdTimeCode(time));
            mesh->pointCacheTimesamples.push_back(framePoints);
        }

        duration = (mesh->timeSampleEnd - mesh->timeSampleStart) + 1;
    }
    if (stage->maxKeyFrames < mesh->pointCacheTimesamples.size())
    {
        stage->maxKeyFrames = mesh->pointCacheTimesamples.size();
        if (duration > 1e-6)
        {
            // This appears to be for FBX to rescale the point cache animation
            stage->mutiplier = stage->maxKeyFrames / duration / 24.0;
        }
    }

    PXR_NS::VtArray<int> faceVertexCounts = GetDefaultOrFirstTimeSampledValue<PXR_NS::VtArray<int>>(usdMesh.GetFaceVertexCountsAttr());
    PXR_NS::VtArray<int> faceVertexIndices = GetDefaultOrFirstTimeSampledValue<PXR_NS::VtArray<int>>(usdMesh.GetFaceVertexIndicesAttr());
    // Validate data completeness to discard those faces that are invalid.
    size_t index = 0;

    // reverse the face vertex indice's order to flip the normals when orientation is left handed
    auto orientation = usdMesh.GetOrientationAttr();
    PXR_NS::TfToken orientationValue = PXR_NS::UsdGeomTokens->rightHanded;
    if (orientation)
    {
        orientation.Get(&orientationValue);
    }

    for (size_t i = 0; i < faceVertexCounts.size(); i++)
    {
        bool validFace = true;
        int count = faceVertexCounts[i];

        // Checkes boundary.
        if (index + count > faceVertexIndices.size())
        {
            break;
        }

        for (int j = 0; j < count; j++)
        {
            if (faceVertexIndices[index + j] > mesh->points.size())
            {
                validFace = false;
                break;
            }
        }

        // Skip invalid face.
        if (validFace)
        {
            mesh->faceVertexCounts.push_back(count);

            if (orientationValue != PXR_NS::UsdGeomTokens->leftHanded)
            {
                std::copy(faceVertexIndices.begin() + index, faceVertexIndices.begin() + index + count, std::back_inserter(mesh->faceVertexIndices));
            }
            else
            {
                std::reverse_copy(
                    faceVertexIndices.begin() + index,
                    faceVertexIndices.begin() + index + count,
                    std::back_inserter(mesh->faceVertexIndices)
                );
            }
        }

        index += count;
    }

    // Iterates to get the count of uv set
#if PXR_MINOR_VERSION > 21 || (PXR_MINOR_VERSION == 21 && PXR_PATCH_VERSION >= 11)
    PXR_NS::UsdGeomPrimvarsAPI primVarsAPI(usdMesh.GetPrim());
    auto primvars = primVarsAPI.GetPrimvars();
#else
    auto primvars = usdMesh.GetPrimvars();
#endif
    std::vector<PXR_NS::UsdGeomPrimvar> uvPrimVars;
    std::vector<PXR_NS::UsdGeomPrimvar> uvIndicesPrimVars;
    for (auto iter = primvars.begin(); iter != primvars.end(); ++iter)
    {
        const auto& primVar = *iter;

        // Find uv set with type and name
        std::string primVarName = primVar.GetPrimvarName().GetString();
        auto primTypeName = primVar.GetTypeName();
        if (primVarName == PXR_NS::UsdUtilsGetPrimaryUVSetName() || (primVarName.size() >= 3 && primVarName.substr(0, 3) == "st_") ||
            (primVarName.size() >= 2 && primVarName.substr(0, 2) == "st" &&
             (primVar.GetTypeName() == PXR_NS::SdfValueTypeNames->Float2Array || primVar.GetTypeName() == PXR_NS::SdfValueTypeNames->Double2Array ||
              primVar.GetTypeName() == PXR_NS::SdfValueTypeNames->TexCoord2fArray ||
              primVar.GetTypeName() == PXR_NS::SdfValueTypeNames->TexCoord2dArray)))
        {
            uvPrimVars.push_back(primVar);
        }
    }

    // Find uv set with type only.
    if (uvPrimVars.empty())
    {
        for (auto iter = primvars.begin(); iter != primvars.end(); ++iter)
        {
            const auto& primVar = *iter;
            std::string primVarName = primVar.GetPrimvarName().GetString();
            auto primTypeName = primVar.GetTypeName();
            if (primVar.GetTypeName() == PXR_NS::SdfValueTypeNames->TexCoord2fArray ||
                primVar.GetTypeName() == PXR_NS::SdfValueTypeNames->TexCoord2dArray)
            {
                uvPrimVars.push_back(primVar);
                break;
            }
        }
    }

    bool isUsingGltfExporter = mThreadContext->converterContext.GetOutputAssetType() == AssetType::GLTF ||
                               mThreadContext->converterContext.GetOutputAssetType() == AssetType::GLB;

    auto CanBeVertexInterpolated = [](PXR_NS::TfToken& usdInterpType)
    {
        return usdInterpType == PXR_NS::UsdGeomTokens->vertex || usdInterpType == PXR_NS::UsdGeomTokens->varying ||
               usdInterpType == PXR_NS::UsdGeomTokens->constant;
    };

    bool canBeVertexInterpolated = uvPrimVars.size() > 0;
    for (size_t i = 0; i < uvPrimVars.size(); i++)
    {
        auto uvInterp = uvPrimVars[i].GetInterpolation();
        if (!CanBeVertexInterpolated(uvInterp))
        {
            canBeVertexInterpolated = false;
            break;
        }
    }

    mesh->hasFaceVaryingUVs = !canBeVertexInterpolated || !isUsingGltfExporter;
    mesh->uvs.resize(uvPrimVars.size());
    mesh->uvIndices.resize(uvPrimVars.size());
    for (size_t i = 0; i < uvPrimVars.size(); i++)
    {
        const auto& primVar = uvPrimVars[i];

        const PXR_NS::UsdAttribute& indexAttr = primVar.GetIndicesAttr();
        if (indexAttr && (mThreadContext->converterContext.GetOutputAssetType() == AssetType::FBX))
        {
            mesh->uvIndices[i] = GetDefaultOrFirstTimeSampledValue<PXR_NS::VtIntArray>(primVar.GetIndicesAttr());
            mesh->uvs[i] = GetDefaultOrFirstTimeSampledValue<PXR_NS::VtArray<PXR_NS::GfVec2f>>(primVar.GetAttr());
        }
        else
        {
            // Get interpolation type
            auto usdInterpType = primVar.GetInterpolation();

            // Copy data
            if (primVar.GetTypeName() == PXR_NS::SdfValueTypeNames->TexCoord2fArray ||
                primVar.GetTypeName() == PXR_NS::SdfValueTypeNames->Float2Array)
            {
                PXR_NS::VtArray<PXR_NS::GfVec2f> uvValues;
                ComputeDefaultOrFirstFrameFlattened<PXR_NS::GfVec2f>(primVar, &uvValues);
                if (uvValues.size() > 0)
                {
                    if (mesh->hasFaceVaryingUVs)
                    {
                        mesh->uvs[i] = ToFaceVaryingData<PXR_NS::GfVec2f>(uvValues, mesh->faceVertexCounts, mesh->faceVertexIndices, usdInterpType);
                    }
                    else
                    {
                        mesh->uvs[i] = ToVertexData<PXR_NS::GfVec2f>(uvValues, mesh->points.size(), usdInterpType);
                    }
                }
            }
            else if (primVar.GetTypeName() == PXR_NS::SdfValueTypeNames->TexCoord2dArray || primVar.GetTypeName() == PXR_NS::SdfValueTypeNames->Double2Array)
            {
                PXR_NS::VtArray<PXR_NS::GfVec2d> uvValues;
                ComputeDefaultOrFirstFrameFlattened<PXR_NS::GfVec2d>(primVar, &uvValues);
                if (uvValues.size() > 0)
                {
                    if (mesh->hasFaceVaryingUVs)
                    {
                        uvValues = ToFaceVaryingData<PXR_NS::GfVec2d>(uvValues, mesh->faceVertexCounts, mesh->faceVertexIndices, usdInterpType);
                    }
                    else
                    {
                        uvValues = ToVertexData<PXR_NS::GfVec2d>(uvValues, mesh->points.size(), usdInterpType);
                    }

                    for (size_t j = 0; j < uvValues.size(); j++)
                    {
                        mesh->uvs[i].push_back(PXR_NS::GfVec2f(uvValues[j][0], uvValues[j][1]));
                    }
                }
            }
        }
    }

    // Add normal
    bool hasNormal = false;
    if (const auto& normalAttr = usdMesh.GetNormalsAttr())
    {
        auto FlipNormalHandedness = [mesh](PXR_NS::VtArray<PXR_NS::GfVec3f>& normals)
        {
            size_t normalIndex = 0;
            for (size_t i = 0; i < mesh->faceVertexCounts.size(); i++)
            {
                auto count = mesh->faceVertexCounts[i];
                if (normalIndex + count <= normals.size())
                {
                    size_t loopCount = size_t(float(count) / 2.0);
                    for (size_t offset = 0; offset < loopCount; offset++)
                    {
                        auto temp = normals[normalIndex + offset];
                        normals[normalIndex + offset] = mesh->normals[normalIndex + count - 1 - offset];
                        normals[normalIndex + count - 1 - offset] = temp;
                    }
                }
                else
                {
                    break;
                }
                normalIndex += count;
            }
        };

        auto normalArray = GetDefaultOrFirstTimeSampledValue<PXR_NS::VtArray<PXR_NS::GfVec3f>>(normalAttr);
        auto normalInterpolation = usdMesh.GetNormalsInterpolation();
        mesh->hasFaceVaryingNormals = !isUsingGltfExporter || !CanBeVertexInterpolated(normalInterpolation);
        if (mesh->hasFaceVaryingNormals)
        {
            mesh->normals = ToFaceVaryingData(normalArray, mesh->faceVertexCounts, mesh->faceVertexIndices, normalInterpolation);

            if (orientationValue == PXR_NS::UsdGeomTokens->leftHanded)
            {
                FlipNormalHandedness(mesh->normals);
            }
        }
        else
        {
            mesh->normals = ToVertexData(normalArray, mesh->points.size(), normalInterpolation);
        }

        hasNormal = mesh->normals.size() > 0;

        if (hasNormal && mesh->pointCacheTimesamples.size() > 0)
        {
            bool hasValidNormalTimeSamples = true;
            std::vector<double> normalTimeSamples;
            if (normalAttr.GetTimeSamples(&normalTimeSamples) && !normalTimeSamples.empty())
            {
                for (double time = mesh->timeSampleStart; time <= mesh->timeSampleEnd; time += 1.0)
                {
                    const auto& frameNormals = GetUSDValue<PXR_NS::VtArray<PXR_NS::GfVec3f>>(normalAttr, PXR_NS::UsdTimeCode(time));
                    if (mesh->hasFaceVaryingNormals)
                    {
                        auto frameNormalsFlattened = ToFaceVaryingData(
                            frameNormals,
                            mesh->faceVertexCounts,
                            mesh->faceVertexIndices,
                            normalInterpolation
                        );

                        mesh->normalCacheTimesamples.push_back(frameNormalsFlattened);

                        if (orientationValue == PXR_NS::UsdGeomTokens->leftHanded)
                        {
                            FlipNormalHandedness(mesh->normalCacheTimesamples.back());
                        }
                    }
                    else
                    {
                        auto frameNormalsVertex = ToVertexData(frameNormals, mesh->points.size(), normalInterpolation);
                        mesh->normalCacheTimesamples.push_back(frameNormalsVertex);
                    }
                }
            }

            if (!hasValidNormalTimeSamples)
            {
                mesh->normalCacheTimesamples.clear();
            }
        }
    }

    // Compute normal if necessary
    // Note this is a basic face / uniform normal, no smoothing is applied.
    if (!hasNormal)
    {
        mesh->normals.resize(mesh->faceVertexIndices.size());
        size_t totalVertices = 0;
        for (const auto& vertexCount : mesh->faceVertexCounts)
        {
            if (vertexCount >= 3)
            {
                const auto& p0 = mesh->points[mesh->faceVertexIndices[totalVertices]];
                const auto& p1 = mesh->points[mesh->faceVertexIndices[totalVertices + 1]];
                const auto& p2 = mesh->points[mesh->faceVertexIndices[totalVertices + 2]];
                const auto& v0 = p1 - p0;
                const auto& v1 = p2 - p0;
                const auto& normal = v0 ^ v1;
                for (size_t i = 0; i < vertexCount; i++)
                {
                    mesh->normals[totalVertices + i] = normal.GetNormalized();
                }
            }
            totalVertices += vertexCount;
        }
    }

    // Vertex color
    const auto& colorPrimvar = usdMesh.GetDisplayColorPrimvar();
    if (colorPrimvar)
    {
        PXR_NS::VtArray<PXR_NS::GfVec3f> usdColors;
        ComputeFlattened<PXR_NS::GfVec3f>(colorPrimvar, &usdColors);
        if (usdColors.size() > 0)
        {
            auto colorInterp = colorPrimvar.GetInterpolation();
            mesh->hasFaceVaryingColors = !isUsingGltfExporter || !CanBeVertexInterpolated(colorInterp);
            if (mesh->hasFaceVaryingColors)
            {
                mesh->colors.push_back(ToFaceVaryingData(usdColors, mesh->faceVertexCounts, mesh->faceVertexIndices, colorInterp));
            }
            else
            {
                mesh->colors.push_back(ToVertexData(usdColors, mesh->points.size(), colorInterp));
            }
        }
    }

    auto usdGeomSubsets = PXR_NS::UsdGeomSubset::GetAllGeomSubsets(usdMesh);
    if (!usdGeomSubsets.empty())
    {
        for (const auto& usdGeomSubset : usdGeomSubsets)
        {
            MeshGeomSubset meshSubset;
            meshSubset.faceIndices = GetDefaultOrFirstTimeSampledValue<PXR_NS::VtArray<int>>(usdGeomSubset.GetIndicesAttr());
            if (!mThreadContext->converterContext.IgnoreMaterials())
            {
                meshSubset.materialIndex = PopulateBoundMaterial(stage, usdGeomSubset.GetPrim());
            }
            mesh->meshSubsets.push_back(meshSubset);
        }
    }
    else
    {
        MeshGeomSubset subset;
        subset.faceIndices.resize(mesh->faceVertexCounts.size());
        if (!mThreadContext->converterContext.IgnoreMaterials())
        {
            subset.materialIndex = PopulateBoundMaterial(stage, usdMesh.GetPrim());
            if (subset.materialIndex != INVALID_MATERIAL_INDEX)
            {
                subset.name = stage->materials[subset.materialIndex]->name;
            }
        }

        std::iota(subset.faceIndices.begin(), subset.faceIndices.end(), 0);
        mesh->meshSubsets.push_back(subset);
    }

    return mesh;
}

// Helper function to return an attribute value, if defined, or the specified default value if not
template <class T>
inline T getAttribute(const PXR_NS::UsdAttribute& attrib, const T& def)
{
    T val = def;
    if (attrib)
    {
        attrib.Get(&val, PXR_NS::UsdTimeCode::EarliestTime());
    }
    return val;
}

CurvePtr UsdSdkImporter::PopulateCurve(const StagePtr& stage, const PXR_NS::UsdGeomBasisCurves& usdCurve)
{
    CurvePtr curve = std::make_shared<Curve>();
    curve->name = usdCurve.GetPrim().GetName();

    {
        const PXR_NS::TfToken curveType = getAttribute(usdCurve.GetTypeAttr(), PXR_NS::UsdGeomTokens->cubic);
        if (curveType == PXR_NS::UsdGeomTokens->linear)
        {
            curve->type = CurveType::Linear;
        }
        else if (curveType == PXR_NS::UsdGeomTokens->cubic)
        {
            curve->type = CurveType::Cubic;
        }
        else
        {
            mThreadContext->converterContext.Log("Unsupported curve type.");
            curve->type = CurveType::Cubic;
        }
    }

    {
        const PXR_NS::TfToken basis = getAttribute(usdCurve.GetBasisAttr(), PXR_NS::UsdGeomTokens->bspline);
        if (basis == PXR_NS::UsdGeomTokens->bezier)
        {
            curve->basis = CurveBasis::Bezier;
        }
        else if (basis == PXR_NS::UsdGeomTokens->bspline)
        {
            curve->basis = CurveBasis::Bspline;
        }
        else if (basis == PXR_NS::UsdGeomTokens->catmullRom)
        {
            curve->basis = CurveBasis::CatmullRom;
        }
        else
        {
            mThreadContext->converterContext.Log("Unsupported basis interpolation mode.");
            curve->basis = CurveBasis::Bspline;
        }
    }

    {
        const PXR_NS::TfToken wrap = getAttribute(usdCurve.GetWrapAttr(), PXR_NS::UsdGeomTokens->nonperiodic);
        if (wrap == PXR_NS::UsdGeomTokens->nonperiodic)
        {
            curve->wrap = CurveWrap::NonPeriodic;
        }
        else if (wrap == PXR_NS::UsdGeomTokens->periodic)
        {
            curve->wrap = CurveWrap::Periodic;
        }
        else if (wrap == PXR_NS::UsdGeomTokens->pinned)
        {
            curve->wrap = CurveWrap::Pinned;
        }
        else
        {
            mThreadContext->converterContext.Log("Unsupported curve wrap.");
            curve->wrap = CurveWrap::NonPeriodic;
        }
    }

    curve->vertexCounts = GetDefaultOrFirstTimeSampledValue<PXR_NS::VtIntArray>(usdCurve.GetCurveVertexCountsAttr());
    curve->points = GetDefaultOrFirstTimeSampledValue<PXR_NS::VtArray<PXR_NS::GfVec3f>>(usdCurve.GetPointsAttr());
    curve->width = GetDefaultOrFirstTimeSampledValue<PXR_NS::VtArray<float>>(usdCurve.GetWidthsAttr());

    // UVs
    PXR_NS::UsdGeomPrimvarsAPI primvarsAPI(usdCurve);
    PXR_NS::UsdGeomPrimvar texCoords = primvarsAPI.GetPrimvar(PXR_NS::TfToken("primvars:st1"));
    texCoords.ComputeFlattened(&curve->uvs);

    // Subdivision
    curve->subdivPerSegment = mCurveSubdivisionNumber;

    return curve;
}

CameraPtr UsdSdkImporter::PopulateCamera(const PXR_NS::UsdGeomCamera& usdCamera)
{
    CameraPtr camera = std::make_shared<Camera>();
    const auto& cameraParameters = usdCamera.GetCamera(PXR_NS::UsdTimeCode::Default());
    camera->name = usdCamera.GetPrim().GetName();
    camera->projectionType = cameraParameters.GetProjection();
    camera->position = cameraParameters.GetFrustum().GetPosition();
    camera->lookAt = cameraParameters.GetFrustum().ComputeLookAtPoint();
    camera->up = cameraParameters.GetFrustum().ComputeUpVector();
    camera->focusDistance = cameraParameters.GetFocusDistance();
    camera->focalLength = cameraParameters.GetFocalLength();
    camera->horizonalAperture = cameraParameters.GetHorizontalAperture();
    camera->verticallAperture = cameraParameters.GetVerticalAperture();
    camera->clippingNear = cameraParameters.GetClippingRange().GetMin();
    camera->clippingFar = cameraParameters.GetClippingRange().GetMax();

    return camera;
}

MaterialPtr UsdSdkImporter::PopulateMaterial(const StagePtr& stage, const PXR_NS::UsdShadeMaterial& usdMaterial)
{
    auto previewSurfaceShader = usdMaterial.ComputeSurfaceSource(PXR_NS::UsdShadeTokens->universalRenderContext);

    MaterialPtr material;
    PXR_NS::TfToken id;
    // The UsdPreviewSurface material may baked from the mdl material, so populate it first
    if (previewSurfaceShader && previewSurfaceShader.GetShaderId(&id) && id == PXR_NS::TfToken("UsdPreviewSurface"))
    {

        material = PopulateUsdPreviewSurfaceMaterial(stage, previewSurfaceShader);
    }

    if (material)
    {
        const std::string& materialName = usdMaterial.GetPrim().GetName();
        material->name = materialName;
    }

    return material;
}

MaterialPtr UsdSdkImporter::PopulateOmniPbrMaterial(const StagePtr& stage, const PXR_NS::UsdShadeShader& usdShader)
{
    auto material = std::make_shared<Material>();
    PXR_NS::GfVec3f colorConstant(1.0f);
    GetShaderInputValue(colorConstant, usdShader, "diffuse_color_constant");
    PXR_NS::GfVec3f diffuseColorFactor(1.0f);
    GetShaderInputValue(diffuseColorFactor, usdShader, "diffuse_tint");

    PXR_NS::SdfAssetPath texturePath;
    material->hasDiffuseColor = true;
    if (GetShaderInputValue(texturePath, usdShader, "diffuse_texture") && !texturePath.GetAssetPath().empty())
    {
        material->GetTextureReference(MaterialTextureType::DIFFUSE).imageIndex = GetOrCreateTexture(stage, texturePath);
        material->diffuseColor = diffuseColorFactor;
    }
    else
    {
        material->diffuseColor = PXR_NS::GfVec3f(
            diffuseColorFactor[0] * colorConstant[0],
            diffuseColorFactor[1] * colorConstant[1],
            diffuseColorFactor[2] * colorConstant[2]
        );
    }

    GetShaderInputValue(material->hasEmissiveColor, usdShader, "enable_emission");
    if (material->hasEmissiveColor)
    {
        GetShaderInputValue(material->emissiveColor, usdShader, "emissive_color");
        if (GetShaderInputValue(texturePath, usdShader, "emissive_color_texture"))
        {
            material->GetTextureReference(MaterialTextureType::EMISSIVE).imageIndex = GetOrCreateTexture(stage, texturePath);
        }
    }

    material->hasSpecularStrengthFactor = true;
    material->specularStrength = 0.5;
    GetShaderInputValue(material->specularStrength, usdShader, "specular_level");

    if (GetShaderInputValue(texturePath, usdShader, "normalmap_texture"))
    {
        material->GetTextureReference(MaterialTextureType::NORMAL).imageIndex = GetOrCreateTexture(stage, texturePath);
    }

    bool enableOpacityTexture = true;
    GetShaderInputValue(enableOpacityTexture, usdShader, "enable_opacity_texture");
    if (enableOpacityTexture)
    {
        if (GetShaderInputValue(texturePath, usdShader, "opacity_texture"))
        {
            material->GetTextureReference(MaterialTextureType::OPACITY).imageIndex = GetOrCreateTexture(stage, texturePath);
            int opacityMode = 0;
            GetShaderInputValue(opacityMode, usdShader, "opacity_mode");
            material->GetTextureReference(MaterialTextureType::OPACITY).outputMode = opacityMode == 1 ? TextureOutputMode::AVERAGE :
                                                                                                        TextureOutputMode::ALPHA;
        }
    }

    GetShaderInputValue(material->hasOpacity, usdShader, "enable_opacity");
    if (material->hasOpacity)
    {
        GetShaderInputValue(material->opacity, usdShader, "opacity_constant");
        GetShaderInputValue(material->opacityThreshold, usdShader, "opacity_threshold");
        if (PXR_NS::GfIsClose(material->opacityThreshold, 0.0, 1e-6))
        {
            material->opacityMode = GLTFOpacityMode::GLTF_BLEND;
        }
        else if (material->opacityThreshold > 0.0f)
        {
            material->opacityMode = GLTFOpacityMode::GLTF_MASK;
        }
        else if (material->opacity == 1.0f)
        {
            material->opacityMode = GLTFOpacityMode::GLTF_OPAQUE;
        }
        else
        {
            material->opacityMode = GLTFOpacityMode::GLTF_BLEND;
        }
    }

    auto& metallic = material->GetTextureReference(MaterialTextureType::METALLIC);
    auto& roughness = material->GetTextureReference(MaterialTextureType::ROUGHNESS);
    if (GetShaderInputValue(texturePath, usdShader, "ORM_texture"))
    {
        metallic.imageIndex = GetOrCreateTexture(stage, texturePath);
        metallic.outputMode = TextureOutputMode::R;
        roughness.imageIndex = metallic.imageIndex;
        roughness.outputMode = TextureOutputMode::G;
        material->hasMetallicFactor = true;
        material->metallicFactor = 1.0f;
        material->hasRoughnessFactor = true;
        material->roughnessFactor = 1.0f;
    }

    float metallicAmount = 0.0f;
    GetShaderInputValue(metallicAmount, usdShader, "metallic_constant");

    float roughnessAmount = 0.5f;
    GetShaderInputValue(roughnessAmount, usdShader, "metallic_constant");

    if (!metallic.IsValid())
    {
        material->hasMetallicFactor = true;
        double metallicTextureInfluence = 1.0f;
        GetShaderInputValue(metallicTextureInfluence, usdShader, "metallic_texture_influence");
        if (GetShaderInputValue(texturePath, usdShader, "metallic_texture"))
        {
            metallic.imageIndex = GetOrCreateTexture(stage, texturePath);
            metallic.outputMode = TextureOutputMode::AVERAGE;
            material->metallicFactor = metallicTextureInfluence;
            metallic.bias = PXR_NS::GfVec4f((1 - material->metallicFactor) * metallicAmount);
        }
        else
        {
            material->metallicFactor = metallicAmount * (1 - metallicTextureInfluence);
        }
    }

    if (!roughness.IsValid())
    {
        material->hasRoughnessFactor = true;
        double roughnessTextureInfluence = 1.0f;
        bool hasInfluence = GetShaderInputValue(roughnessTextureInfluence, usdShader, "reflection_roughness_texture_influence");
        if (GetShaderInputValue(texturePath, usdShader, "reflectionroughness_texture"))
        {
            roughness.imageIndex = GetOrCreateTexture(stage, texturePath);
            roughness.outputMode = TextureOutputMode::AVERAGE;
            material->roughnessFactor = roughnessTextureInfluence;
            roughness.bias = PXR_NS::GfVec4f((1 - roughnessTextureInfluence) * roughnessAmount);
        }
        else
        {
            if (hasInfluence)
            {
                material->roughnessFactor = (1 - roughnessTextureInfluence) * roughnessAmount;
            }
            else
            {
                material->roughnessFactor = 1.0;
            }
        }
    }

    if (GetShaderInputValue(texturePath, usdShader, "ao_texture"))
    {
        auto& occlusionTexture = material->GetTextureReference(MaterialTextureType::OCCLUSION);
        occlusionTexture.imageIndex = GetOrCreateTexture(stage, texturePath);
    }

    return material;
}

MaterialPtr UsdSdkImporter::PopulateOmniGlassMaterial(const StagePtr& stage, const PXR_NS::UsdShadeShader& usdShader)
{
    auto material = std::make_shared<Material>();
    if (GetShaderInputValue(material->diffuseColor, usdShader, "glass_color"))
    {
        material->hasDiffuseColor = true;
    }
    material->isOmniGlass = true;
    PXR_NS::SdfAssetPath texturePath;
    if (GetShaderInputValue(texturePath, usdShader, "glass_color_texture"))
    {
        material->GetTextureReference(MaterialTextureType::TRANSMISSION).imageIndex = GetOrCreateTexture(stage, texturePath);
    }

    if (GetShaderInputValue(texturePath, usdShader, "normal_map_texture"))
    {
        material->GetTextureReference(MaterialTextureType::NORMAL).imageIndex = GetOrCreateTexture(stage, texturePath);
    }

    if (GetShaderInputValue(texturePath, usdShader, "roughness_texture"))
    {
        auto& glossy = material->GetTextureReference(MaterialTextureType::GLOSSY);
        glossy.imageIndex = GetOrCreateTexture(stage, texturePath);
        glossy.outputMode = TextureOutputMode::AVERAGE;
        glossy.bias = PXR_NS::GfVec4f(1.0f);
        glossy.scale = PXR_NS::GfVec4f(-1.0f);
        material->hasGlossyFactor = true;
        material->glossyFactor = 1.0f;
    }

    float glossyFactor;
    if (GetShaderInputValue(glossyFactor, usdShader, "roughness_texture_influence"))
    {
        material->hasGlossyFactor = true;
        material->glossyFactor = glossyFactor;
    }

    if (GetShaderInputValue(material->specularColor, usdShader, "reflection_color"))
    {
        material->hasSpecularColor = true;
    }

    if (GetShaderInputValue(texturePath, usdShader, "reflection_color_texture"))
    {
        material->GetTextureReference(MaterialTextureType::SPECULAR).imageIndex = GetOrCreateTexture(stage, texturePath);
    }

    if (GetShaderInputValue(texturePath, usdShader, "normal_map_texture"))
    {
        material->GetTextureReference(MaterialTextureType::NORMAL).imageIndex = GetOrCreateTexture(stage, texturePath);
    }

    GetShaderInputValue(material->hasOpacity, usdShader, "enable_opacity");
    if (material->hasOpacity)
    {
        GetShaderInputValue(material->opacity, usdShader, "cutout_opacity");
        GetShaderInputValue(material->opacityThreshold, usdShader, "opacity_threshold");
        if (material->opacityThreshold > 0.0f)
        {
            material->opacityMode = GLTFOpacityMode::GLTF_MASK;
        }
        else
        {
            material->opacityMode = GLTFOpacityMode::GLTF_BLEND;
            material->hasRoughnessFactor = true;
            material->roughnessFactor = 0.0f;
            material->hasMetallicFactor = true;
            material->metallicFactor = 0.0f;
            material->hasTransmissionFactor = true;
            material->transmissionFactor = 1.0f;
        }
    }

    if (GetShaderInputValue(texturePath, usdShader, "cutout_opacity_texture"))
    {
        material->GetTextureReference(MaterialTextureType::OPACITY).imageIndex = GetOrCreateTexture(stage, texturePath);
        int opacityMode = 0;
        GetShaderInputValue(opacityMode, usdShader, "cutout_opacity_mono_source");
        material->GetTextureReference(MaterialTextureType::OPACITY).outputMode = opacityMode == 1 ? TextureOutputMode::AVERAGE :
                                                                                                    TextureOutputMode::ALPHA;
    }

    return material;
}

MaterialPtr UsdSdkImporter::PopulateUsdPreviewSurfaceMaterial(const StagePtr& stage, const PXR_NS::UsdShadeShader& usdShader)
{
    auto material = std::make_shared<Material>();
    int value;
    if (GetShaderInputValue(value, usdShader, "useSpecularWorkflow"))
    {
        material->useSpecularGlossyWorkflow = value == 0 ? false : true;
    }

    auto& diffuseTexture = material->GetTextureReference(MaterialTextureType::DIFFUSE);
    GetPreviewSurfaceTextureInputValue(stage, usdShader, "diffuseColor", diffuseTexture);

    auto& emissiveTexture = material->GetTextureReference(MaterialTextureType::EMISSIVE);
    GetPreviewSurfaceTextureInputValue(stage, usdShader, "emissiveColor", emissiveTexture);

    auto& normalTexture = material->GetTextureReference(MaterialTextureType::NORMAL);
    GetPreviewSurfaceTextureInputValue(stage, usdShader, "normal", normalTexture);

    auto& opacityTexture = material->GetTextureReference(MaterialTextureType::OPACITY);
    GetPreviewSurfaceTextureInputValue(stage, usdShader, "opacity", opacityTexture);
    if (opacityTexture.IsValid())
    {
        material->opacityMode = GLTFOpacityMode::GLTF_BLEND;
        material->opacityThreshold = 0.0f;
    }

    auto& occlusionTexture = material->GetTextureReference(MaterialTextureType::OCCLUSION);
    GetPreviewSurfaceTextureInputValue(stage, usdShader, "occlusion", occlusionTexture);

    if (material->useSpecularGlossyWorkflow)
    {
        auto& specularTexture = material->GetTextureReference(MaterialTextureType::SPECULAR);
        if (GetPreviewSurfaceTextureInputValue(stage, usdShader, "specularColor", specularTexture))
        {
            material->hasSpecularColor = true;
            material->specularColor = PXR_NS::GfVec3f(1.0f);
        }

        auto& glossyTexture = material->GetTextureReference(MaterialTextureType::GLOSSY);
        if (GetPreviewSurfaceTextureInputValue(stage, usdShader, "roughness", glossyTexture))
        {
            material->hasGlossyFactor = true;
            material->glossyFactor = 1.0f;
        }

        if (GetShaderInputValue(material->glossyFactor, usdShader, "roughness"))
        {
            material->hasGlossyFactor = true;
        }

        if (GetShaderInputValue(material->specularColor, usdShader, "specularColor"))
        {
            material->hasSpecularColor = true;
        }
    }
    else
    {
        auto& metallicTexture = material->GetTextureReference(MaterialTextureType::METALLIC);
        if (GetPreviewSurfaceTextureInputValue(stage, usdShader, "metallic", metallicTexture))
        {
            material->hasMetallicFactor = true;
            material->metallicFactor = 1.0f;
        }

        auto& roughnessTexture = material->GetTextureReference(MaterialTextureType::ROUGHNESS);
        if (GetPreviewSurfaceTextureInputValue(stage, usdShader, "roughness", roughnessTexture))
        {
            material->hasRoughnessFactor = true;
            material->roughnessFactor = 1.0f;
        }

        auto& clearcoatTexture = material->GetTextureReference(MaterialTextureType::CLEARCOAT);
        if (GetPreviewSurfaceTextureInputValue(stage, usdShader, "clearcoat", clearcoatTexture))
        {
            material->hasClearCoatFactor = true;
            material->clearCoatFactor = 1.0f;
        }

        auto& clearcoatRoughnessTexture = material->GetTextureReference(MaterialTextureType::CLEARCOAT_ROUGHNESS);
        if (GetPreviewSurfaceTextureInputValue(stage, usdShader, "clearcoatRoughness", clearcoatRoughnessTexture))
        {
            material->hasClearCoatRoughnessFactor = true;
            material->clearCoatRoughnessFactor = 1.0f;
        }

        if (GetShaderInputValue(material->metallicFactor, usdShader, "metallic"))
        {
            material->hasMetallicFactor = true;
        }

        if (GetShaderInputValue(material->roughnessFactor, usdShader, "roughness"))
        {
            material->hasRoughnessFactor = true;
        }

        if (GetShaderInputValue(material->clearCoatFactor, usdShader, "clearcoat"))
        {
            material->hasClearCoatFactor = true;
        }

        if (GetShaderInputValue(material->clearCoatRoughnessFactor, usdShader, "clearcoatRoughness"))
        {
            material->hasClearCoatRoughnessFactor = true;
        }
    }

    material->hasDiffuseColor = GetShaderInputValue(material->diffuseColor, usdShader, "diffuseColor");
    if (GetShaderInputValue(material->emissiveColor, usdShader, "emissiveColor"))
    {
        material->hasEmissiveColor = true;
    }

    GetShaderInputValue(material->opacityThreshold, usdShader, "opacityThreshold");

    float opacity;
    if (GetShaderInputValue(opacity, usdShader, "opacity"))
    {
        if (material->opacityThreshold > 0.0f)
        {
            material->hasOpacity = true;
            material->opacity = opacity;
        }
        else
        {
            material->hasTransmissionFactor = true;
            material->transmissionFactor = 1.0f - opacity;
            material->thinWalled = false;
        }
    }

    if (GetShaderInputValue(material->ior, usdShader, "ior"))
    {
        material->hasIor = true;
    }

    return material;
}

MaterialPtr UsdSdkImporter::PopulateGltfMaterial(const StagePtr& stage, const PXR_NS::UsdShadeShader& usdShader)
{
    auto material = std::make_shared<Material>();
    if (GetShaderInputValue(material->diffuseColor, usdShader, "base_color_factor"))
    {
        material->useSpecularGlossyWorkflow = false;
    }
    else if (GetShaderInputValue(material->diffuseColor, usdShader, "diffuse_factor"))
    {
        material->useSpecularGlossyWorkflow = true;
    }

    if (material->useSpecularGlossyWorkflow)
    {
        auto& diffuseTexture = material->GetTextureReference(MaterialTextureType::DIFFUSE);
        GetGltfTextureInputValue(stage, usdShader, "diffuse_texture", diffuseTexture);

        auto& specularTexture = material->GetTextureReference(MaterialTextureType::SPECULAR);
        if (GetGltfTextureInputValue(stage, usdShader, "specular_glossiness_texture", specularTexture))
        {
            auto& glossyTexture = material->GetTextureReference(MaterialTextureType::GLOSSY);
            glossyTexture = specularTexture;
            glossyTexture.outputMode = TextureOutputMode::ALPHA;
            material->hasGlossyFactor = true;
            material->glossyFactor = 1.0f;
            material->hasSpecularColor = true;
            material->specularColor = PXR_NS::GfVec3f(1.0f);
        }

        if (GetShaderInputValue(material->specularColor, usdShader, "specular_factor"))
        {
            material->hasSpecularColor = true;
        }

        if (GetShaderInputValue(material->glossyFactor, usdShader, "glossiness_factor"))
        {
            material->hasGlossyFactor = true;
        }
    }
    else
    {
        auto& diffuseTexture = material->GetTextureReference(MaterialTextureType::DIFFUSE);
        GetGltfTextureInputValue(stage, usdShader, "base_color_texture", diffuseTexture);

        auto& metallicTexture = material->GetTextureReference(MaterialTextureType::METALLIC);
        if (GetGltfTextureInputValue(stage, usdShader, "metallic_roughness_texture", metallicTexture))
        {
            metallicTexture.outputMode = TextureOutputMode::R;
            auto& roughtnessTexture = material->GetTextureReference(MaterialTextureType::ROUGHNESS);
            roughtnessTexture = metallicTexture;
            roughtnessTexture.outputMode = TextureOutputMode::G;
            material->hasMetallicFactor = true;
            material->metallicFactor = 1.0f;

            material->hasRoughnessFactor = true;
            material->roughnessFactor = 1.0f;
        }

        auto& transmissionTexture = material->GetTextureReference(MaterialTextureType::TRANSMISSION);
        if (GetGltfTextureInputValue(stage, usdShader, "transmission_texture", transmissionTexture))
        {
            material->hasTransmissionFactor = true;
            material->transmissionFactor = 1.0f;
        }

        auto& sheenTexture = material->GetTextureReference(MaterialTextureType::SHEEN);
        if (GetGltfTextureInputValue(stage, usdShader, "sheen_color_texture", sheenTexture))
        {
            material->hasSheenColor = true;
            material->sheenColor = PXR_NS::GfVec3f(1.0f);
        }

        auto& sheenRoughnessTexture = material->GetTextureReference(MaterialTextureType::SHEEN_ROUGHNESS);
        if (GetGltfTextureInputValue(stage, usdShader, "sheen_roughness_texture", sheenRoughnessTexture))
        {
            material->hasSheenRoughnessFactor = true;
            material->sheenRoughnessFactor = 1.0f;
        }

        auto& clearNormalTexture = material->GetTextureReference(MaterialTextureType::CLEARCOAT_NORMAL);
        GetGltfTextureInputValue(stage, usdShader, "clearcoat_normal_texture", clearNormalTexture);

        auto& clearCoatTexture = material->GetTextureReference(MaterialTextureType::CLEARCOAT);
        if (GetGltfTextureInputValue(stage, usdShader, "clearcoat_texture", clearCoatTexture))
        {
            material->hasClearCoatFactor = true;
            material->clearCoatFactor = 1.0f;
        }

        auto& clearRoughnessTexture = material->GetTextureReference(MaterialTextureType::CLEARCOAT_ROUGHNESS);
        if (GetGltfTextureInputValue(stage, usdShader, "clearcoat_roughness_texture", clearRoughnessTexture))
        {
            material->hasClearCoatRoughnessFactor = true;
            material->clearCoatRoughnessFactor = 1.0f;
        }

        auto& specularStrengthTexture = material->GetTextureReference(MaterialTextureType::SPECULAR_STRENGTH);
        if (GetGltfTextureInputValue(stage, usdShader, "specular_texture", specularStrengthTexture))
        {
            material->hasSpecularStrengthFactor = true;
            material->specularStrength = 1.0f;
        }

        auto& specularColorTexture = material->GetTextureReference(MaterialTextureType::SPECULAR);
        GetGltfTextureInputValue(stage, usdShader, "specular_color_texture", specularColorTexture);

        auto& iridescenceTexture = material->GetTextureReference(MaterialTextureType::IRIDESCENCE);
        GetGltfTextureInputValue(stage, usdShader, "iridescence_texture", iridescenceTexture);

        auto& iridescenceThicknessTexture = material->GetTextureReference(MaterialTextureType::IRIDESCENCE_THICKNESS);
        GetGltfTextureInputValue(stage, usdShader, "iridescence_thickness_texture", iridescenceThicknessTexture);

        auto& anisotropyTexture = material->GetTextureReference(MaterialTextureType::ANISOTROPY);
        GetGltfTextureInputValue(stage, usdShader, "anisotropy_texture", anisotropyTexture);

        if (GetShaderInputValue(material->metallicFactor, usdShader, "metallic_factor"))
        {
            material->hasMetallicFactor = true;
        }

        if (GetShaderInputValue(material->roughnessFactor, usdShader, "roughness_factor"))
        {
            material->hasRoughnessFactor = true;
        }

        if (GetShaderInputValue(material->transmissionFactor, usdShader, "transmission_factor"))
        {
            material->hasTransmissionFactor = true;
        }

        if (GetShaderInputValue(material->sheenColor, usdShader, "sheen_color_factor"))
        {
            material->hasSheenColor = true;
        }

        if (GetShaderInputValue(material->sheenRoughnessFactor, usdShader, "sheen_roughness_factor"))
        {
            material->hasSheenRoughnessFactor = true;
        }

        if (GetShaderInputValue(material->clearCoatFactor, usdShader, "clearcoat_factor"))
        {
            material->hasClearCoatFactor = true;
        }

        if (GetShaderInputValue(material->clearCoatRoughnessFactor, usdShader, "clearcoat_roughness_factor"))
        {
            material->hasClearCoatRoughnessFactor = true;
        }

        if (GetShaderInputValue(material->ior, usdShader, "ior"))
        {
            material->hasIor = true;
        }

        if (GetShaderInputValue(material->specularStrength, usdShader, "specular_factor"))
        {
            material->hasSpecularStrengthFactor = true;
        }

        if (GetShaderInputValue(material->specularColor, usdShader, "specular_color_factor"))
        {
            material->hasSpecularColor = true;
        }

        GetShaderInputValue(material->thinWalled, usdShader, "thin_walled");
        if (GetShaderInputValue(material->attenuationDistance, usdShader, "attenuation_distance"))
        {
            material->hasAttenuationDistance = true;
        }

        if (GetShaderInputValue(material->attenuationColor, usdShader, "attenuation_color"))
        {
            material->hasAttenuationColor = true;
        }

        if (GetShaderInputValue(material->iridescenceFactor, usdShader, "iridescence_factor"))
        {
            material->hasIridescenceFactor = true;
        }

        if (GetShaderInputValue(material->iridescenceIor, usdShader, "iridescence_ior"))
        {
            material->hasIridescenceIor = true;
        }

        if (GetShaderInputValue(material->iridescenceThicknessMinimum, usdShader, "iridescence_thickness_minimum"))
        {
            material->hasIridescenceThicknessMinimum = true;
        }

        if (GetShaderInputValue(material->iridescenceThicknessMaximum, usdShader, "iridescence_thickness_maximum"))
        {
            material->hasIridescenceThicknessMaximum = true;
        }

        if (GetShaderInputValue(material->anisotropyStrength, usdShader, "anisotropy_strength"))
        {
            material->hasAnisotropyStrength = true;
        }

        if (GetShaderInputValue(material->anisotropyRotation, usdShader, "anisotropy_rotation"))
        {
            material->anisotropyRotation = std::fmod(material->anisotropyRotation / (float)(M_PI * 2.0), 1.0f);
            if (material->anisotropyRotation < 0.0f)
            {
                material->anisotropyRotation += 1.0f;
            }
            material->hasAnisotropyRotation = true;
        }
    }

    float emissiveIntensity;
    if (GetShaderInputValue(emissiveIntensity, usdShader, "emissive_strength"))
    {
        if (emissiveIntensity == DEFAULT_EMISSIVE_INTENSITY)
        {
            material->hasEmissiveStrength = false;
        }
        else
        {
            material->hasEmissiveStrength = true;
            material->emissiveStrength = emissiveIntensity;
        }
    }

    if (GetShaderInputValue(material->emissiveColor, usdShader, "emissive_factor"))
    {
        material->hasEmissiveColor = true;
    }

    auto& emissiveTexture = material->GetTextureReference(MaterialTextureType::EMISSIVE);
    GetGltfTextureInputValue(stage, usdShader, "emissive_texture", emissiveTexture);

    auto& occlusionTexture = material->GetTextureReference(MaterialTextureType::OCCLUSION);
    GetGltfTextureInputValue(stage, usdShader, "occlusion_texture", occlusionTexture);

    int alphaMode;
    if (GetShaderInputValue(alphaMode, usdShader, "alpha_mode"))
    {
        material->opacityMode = (GLTFOpacityMode)alphaMode;
    }

    if (GetShaderInputValue(material->opacity, usdShader, "base_alpha"))
    {
        material->hasOpacity = true;
    }

    GetShaderInputValue(material->opacityThreshold, usdShader, "alpha_cutoff");

    return material;
}


#if PXR_MINOR_VERSION > 21 || (PXR_MINOR_VERSION == 21 && PXR_PATCH_VERSION >= 11)
LightPtr UsdSdkImporter::PopulateLight(const PXR_NS::UsdLuxLightAPI& usdLight)
#else
LightPtr UsdSdkImporter::PopulateLight(const PXR_NS::UsdLuxLight& usdLight)
#endif
{
    LightPtr light = std::make_shared<Light>();
    light->color = GetUSDValue<PXR_NS::GfVec3f>(usdLight.GetColorAttr());
    light->intensity = GetUSDValue<float>(usdLight.GetIntensityAttr());

    light->name = usdLight.GetPrim().GetName();
    if (usdLight.GetPrim().IsA<PXR_NS::UsdLuxSphereLight>())
    {
        auto usdLightPrim = PXR_NS::UsdLuxSphereLight(usdLight);
        bool isPoint = GetUSDValue<bool>(usdLightPrim.GetTreatAsPointAttr());
        if (isPoint)
        {
            light->type = LightType::POINT;
        }
        else
        {
            light->type = LightType::SPHERE;
        }
    }
    else if (usdLight.GetPrim().IsA<PXR_NS::UsdLuxRectLight>())
    {
        light->type = LightType::RECT;
    }
    else if (usdLight.GetPrim().IsA<PXR_NS::UsdLuxCylinderLight>())
    {
        light->type = LightType::SPHERE;
    }
    else if (usdLight.GetPrim().IsA<PXR_NS::UsdLuxDistantLight>())
    {
        light->type = LightType::DISTANT;
        auto usdLightPrim = PXR_NS::UsdLuxDistantLight(usdLight);
        light->innerAngle = GetUSDValue<float>(usdLightPrim.GetAngleAttr());
    }
    else
    {
        light->type = LightType::SPHERE;
    }

    return light;
}

size_t UsdSdkImporter::PopulateBoundMaterial(const StagePtr& stage, const PXR_NS::UsdPrim& prim)
{
    PXR_NS::UsdShadeMaterial usdMaterial;
    if (prim.IsA<PXR_NS::UsdShadeMaterial>())
    {
        usdMaterial = PXR_NS::UsdShadeMaterial(prim);
    }
    else
    {
        const auto& bindingAPI = PXR_NS::UsdShadeMaterialBindingAPI(prim);
        usdMaterial = bindingAPI.ComputeBoundMaterial();
    }

    size_t materialIndex = INVALID_MATERIAL_INDEX;
    if (usdMaterial)
    {
        auto iter = mDefinedMaterials.find(usdMaterial.GetPath());
        if (iter == mDefinedMaterials.end())
        {
            const auto& material = PopulateMaterial(stage, usdMaterial);
            if (material)
            {
                stage->materials.push_back(material);
                materialIndex = stage->materials.size() - 1;
                mDefinedMaterials.insert({ usdMaterial.GetPath(), materialIndex });
            }
        }
        else
        {
            materialIndex = iter->second;
        }
    }

    return materialIndex;
}

std::string UsdSdkImporter::SwitchToAnimationTrack(const PXR_NS::UsdPrim& prim, const std::string& animationTrackName)
{
    auto usdStage = prim.GetStage();
    auto allChildren = usdStage->GetPseudoRoot().GetAllChildren();
    auto defaultPrim = allChildren.front();
    auto animationTrackVariant = defaultPrim.GetVariantSet(ANIMATION_TRACK_VARIANT_SET_NAME);
    std::string originalVariantSelection = animationTrackVariant.GetVariantSelection();
    auto oldEditTarget = usdStage->GetEditTarget();
    auto tempEditTarget = usdStage->GetEditTargetForLocalLayer(usdStage->GetSessionLayer());
    usdStage->SetEditTarget(tempEditTarget);
    animationTrackVariant.SetVariantSelection(animationTrackName);
    usdStage->SetEditTarget(oldEditTarget);

    return originalVariantSelection;
}

TransformAnimationTracks UsdSdkImporter::GetTransformAnimation(const PXR_NS::UsdPrim& prim, const StagePtr& stage, bool hasPivot)
{
    auto xformPrim = PXR_NS::UsdGeomXformable(prim);

    TransformAnimationTracks allTransformTracks;
    allTransformTracks.reserve(mAllAnimationTracks.size());

    for (size_t i = 0; i < mAllAnimationTracks.size(); i++)
    {
        const auto& animationTrack = mAllAnimationTracks[i];

        std::string originalVariantSelection;
        if (mAllAnimationTracks.size() > 1)
        {
            originalVariantSelection = SwitchToAnimationTrack(prim, animationTrack.name);
        }

        std::vector<double> timeSamples;
        xformPrim.GetTimeSamples(&timeSamples);
        if (!timeSamples.empty())
        {
            double startTime = stage->startTime;
            double endTime = std::min(timeSamples.back(), stage->endTime);

            TransformTimesamples transformTimesamples;
            if (hasPivot && !mThreadContext->converterContext.PivotSupportedForOutputFormat())
            {
                // If output formats don't support pivot, convert all samples into matrix.
                transformTimesamples = GetTransformTimesamples(xformPrim, startTime, endTime);
            }
            else
            {
                auto translations = GetXformOpTimesamples<PXR_NS::GfVec3d>(xformPrim, PXR_NS::UsdGeomXformOp::TypeTranslate, startTime, endTime);
                auto scales = GetXformOpTimesamples<PXR_NS::GfVec3d>(xformPrim, PXR_NS::UsdGeomXformOp::TypeScale, startTime, endTime);
                auto orients = GetXformOpTimesamples<PXR_NS::GfQuatd>(xformPrim, PXR_NS::UsdGeomXformOp::TypeOrient, startTime, endTime);
                auto rotations = GetRotationOpTimesamples(xformPrim, startTime, endTime);
                if (orients.empty())
                {
                    transformTimesamples = TransformTimesamples(translations, rotations, scales);
                }
                else
                {
                    transformTimesamples = TransformTimesamples(translations, orients, scales);
                }

                // Check if it has transform animations
                if (transformTimesamples.Empty())
                {
                    auto transformSamples = GetXformOpTimesamples<PXR_NS::GfMatrix4d>(
                        xformPrim,
                        PXR_NS::UsdGeomXformOp::TypeTransform,
                        startTime,
                        endTime
                    );
                    for (const auto& transformSample : transformSamples)
                    {
                        auto tqs = MathUtils::GfMatrixToTQS(transformSample);
                        translations.push_back(tqs.t);
                        scales.push_back(tqs.s);
                        orients.push_back(tqs.q);
                    }
                    transformTimesamples = TransformTimesamples(translations, orients, scales);
                }
            }

            allTransformTracks.push_back(transformTimesamples);
        }

        if (mAllAnimationTracks.size() > 1)
        {
            SwitchToAnimationTrack(prim, originalVariantSelection);
        }
    }

    return allTransformTracks;
}

StageNodePtr UsdSdkImporter::ImportSkeleton(const PXR_NS::UsdPrim& prim, const StagePtr& stage)
{
    auto usdSkelRoot = PXR_NS::UsdSkelRoot(prim);
    if (!usdSkelRoot)
    {
        return nullptr;
    }

    auto populateSkeleton = [this](
                                const StagePtr& stage,
                                PXR_NS::UsdSkelCache usdSkelCache,
                                PXR_NS::UsdSkelSkeleton skeleton,
                                PXR_NS::VtArray<PXR_NS::TfToken>& jointOrder,
                                std::unordered_map<std::string, StageNodePtr>& jointBoneMap
                            )
    {
        StageNodePtr rootBone;
        PXR_NS::UsdSkelSkeletonQuery skelQuery = usdSkelCache.GetSkelQuery(skeleton);

        std::vector<int> parentJointIndices;
        jointOrder = skelQuery.GetJointOrder();
        const PXR_NS::UsdSkelTopology& topology = skelQuery.GetTopology();
        for (size_t i = 0; i < topology.GetNumJoints(); i++)
        {
            int parentIndex = topology.GetParent(i);
            parentJointIndices.push_back(parentIndex);
        }

        if (jointOrder.size() > 0)
        {
            PXR_NS::VtArray<PXR_NS::GfMatrix4d> bindTransforms;
            skelQuery.GetJointWorldBindTransforms(&bindTransforms);
            if (bindTransforms.size() == 0)
            {
                skelQuery.ComputeJointSkelTransforms(&bindTransforms, PXR_NS::UsdTimeCode::Default(), true);
            }

            PXR_NS::VtArray<PXR_NS::GfMatrix4d> restTransforms;
            skelQuery.ComputeJointLocalTransforms(&restTransforms, PXR_NS::UsdTimeCode::Default(), true);
            std::vector<StageNodePtr> indexBoneMap(jointOrder.size());
            for (size_t i = 0; i < jointOrder.size(); ++i)
            {
                StageNodePtr bone = std::make_shared<StageNode>();
                bone->isBoneNode = true;
                PXR_NS::SdfPath jointPath(jointOrder[i]);
                jointBoneMap[jointOrder[i]] = bone;
                bone->name = jointPath.GetName();
                bone->bindTransform = bindTransforms[i];
                bone->restTransform = restTransforms[i];

                int parentIndex = parentJointIndices[i];
                if (parentIndex != -1)
                {
                    auto parentBone = indexBoneMap[parentIndex];
                    parentBone->children.push_back(bone);
                    bone->parent = parentBone;
                }
                indexBoneMap[i] = bone;

                if (!rootBone && parentIndex == -1)
                {
                    rootBone = bone;
                }
            }

            if (!mThreadContext->converterContext.IgnoreAnimations() && stage->animationTracks.size() > 0)
            {
                for (size_t i = 0; i < mAllAnimationTracks.size(); i++)
                {
                    const auto& animationTrack = mAllAnimationTracks[i];

                    std::string originalVariantSelection;
                    if (mAllAnimationTracks.size() > 1)
                    {
                        originalVariantSelection = SwitchToAnimationTrack(skeleton.GetPrim(), animationTrack.name);
                        // Refresh skel cache to repopulate animation data of this track.
                        usdSkelCache.Clear();
                        skelQuery = usdSkelCache.GetSkelQuery(skeleton);
                    }

                    PXR_NS::UsdSkelAnimQuery animQuery = skelQuery.GetAnimQuery();
                    if (animQuery.GetPrim())
                    {
                        std::vector<double> timeSamples;
                        if (animQuery.GetJointTransformTimeSamples(&timeSamples))
                        {
                            if (timeSamples.size() > 0)
                            {
                                double totalTimeSample = timeSamples.back();
                                std::vector<PXR_NS::VtVec3dArray> boneTranslations(indexBoneMap.size());
                                std::vector<PXR_NS::VtVec3dArray> boneScales(indexBoneMap.size());
                                std::vector<PXR_NS::VtQuatdArray> boneOrients(indexBoneMap.size());
                                for (double timeSample = 0.0; timeSample < totalTimeSample + 1.0; timeSample += 1.0)
                                {
                                    PXR_NS::VtArray<PXR_NS::GfMatrix4d> usdJointLocalTransforms;
                                    skelQuery.ComputeJointLocalTransforms(&usdJointLocalTransforms, timeSample);
                                    PXR_NS::VtQuatfArray rotations;
                                    PXR_NS::VtVec3hArray scalings;
                                    PXR_NS::VtVec3fArray translations;
                                    PXR_NS::UsdSkelDecomposeTransforms(usdJointLocalTransforms, &translations, &rotations, &scalings);
                                    for (size_t boneIndex = 0; boneIndex < indexBoneMap.size(); boneIndex++)
                                    {
                                        boneTranslations[boneIndex].push_back(translations[boneIndex]);
                                        boneScales[boneIndex].push_back(scalings[boneIndex]);
                                        boneOrients[boneIndex].push_back(rotations[boneIndex]);
                                    }
                                }

                                for (size_t boneIndex = 0; boneIndex < indexBoneMap.size(); boneIndex++)
                                {
                                    auto& bone = indexBoneMap[boneIndex];
                                    TransformTimesamples transformTimesamples(
                                        boneTranslations[boneIndex],
                                        boneOrients[boneIndex],
                                        boneScales[boneIndex]
                                    );
                                    bone->transformAnimationTracks.push_back(transformTimesamples);
                                }
                            }
                        }
                    }

                    if (mAllAnimationTracks.size() > 1)
                    {
                        SwitchToAnimationTrack(skeleton.GetPrim(), originalVariantSelection);
                    }
                }
            }
        }

        return rootBone;
    };

    PXR_NS::UsdSkelCache usdSkelCache;
#if (PXR_MINOR_VERSION == 20 && PXR_PATCH_VERSION == 11) || PXR_MINOR_VERSION >= 21
    usdSkelCache.Populate(usdSkelRoot, PXR_NS::UsdPrimDefaultPredicate);
#else
    usdSkelCache.Populate(usdSkelRoot);
#endif

    StageNodePtr rootBone;
    std::vector<PXR_NS::UsdSkelBinding> usdSkeletonBindings;
    PXR_NS::VtArray<PXR_NS::TfToken> jointOrder;
    std::unordered_map<std::string, StageNodePtr> jointBoneMap;
#if (PXR_MINOR_VERSION == 20 && PXR_PATCH_VERSION == 11) || PXR_MINOR_VERSION >= 21
    usdSkelCache.ComputeSkelBindings(usdSkelRoot, &usdSkeletonBindings, PXR_NS::UsdPrimDefaultPredicate);
#else
    usdSkelCache.ComputeSkelBindings(usdSkelRoot, &usdSkeletonBindings);
#endif
    if (usdSkeletonBindings.size() > 0)
    {
        const auto& usdSkeletonBinding = usdSkeletonBindings[0];
        const PXR_NS::UsdSkelSkeleton& skeleton = usdSkeletonBinding.GetSkeleton();
        rootBone = populateSkeleton(stage, usdSkelCache, skeleton, jointOrder, jointBoneMap);

        for (const PXR_NS::UsdSkelSkinningQuery& skinningQuery : usdSkeletonBinding.GetSkinningTargets())
        {
            PXR_NS::UsdGeomMesh skinningMesh = PXR_NS::UsdGeomMesh(skinningQuery.GetPrim());
            if (skinningMesh)
            {
                const PXR_NS::UsdPrim& skinningPrim = skinningQuery.GetPrim();
                PXR_NS::UsdSkelBindingAPI skelBinding(skinningPrim);

                PXR_NS::GfMatrix4d geomBindingTransform(1);
                PXR_NS::UsdAttribute geomBindingAttribute = skelBinding.GetGeomBindTransformAttr();
                if (geomBindingAttribute)
                {
                    geomBindingAttribute.Get(&geomBindingTransform);
                }

                PXR_NS::UsdGeomMesh usdMesh = PXR_NS::UsdGeomMesh(skinningPrim);
                auto usdStage = skinningPrim.GetStage();
                auto mesh = PopulateMesh(stage, usdMesh);

                auto skinMesh = std::make_shared<SkinMesh>(stage->meshes.size());
                skinMesh->geomBindTransform = geomBindingTransform;
                auto jointWeightPrimvar = skinningQuery.GetJointWeightsPrimvar();
                jointWeightPrimvar.Get(&skinMesh->jointWeights);
                auto jointIndicesPrimvar = skinningQuery.GetJointIndicesPrimvar();
                jointIndicesPrimvar.Get(&skinMesh->jointInfluences);
                skinMesh->numBoneInfluencesPerVertex = skinningQuery.GetNumInfluencesPerComponent();
                skinMesh->skeletonRoot = rootBone;

                PXR_NS::VtTokenArray jointTokens;
                skinningQuery.GetJointOrder(&jointTokens);
                stage->meshes.push_back(mesh);
                stage->skinMeshes.push_back(skinMesh);
                if (jointTokens.empty())
                {
                    jointTokens = jointOrder;
                }

                for (size_t i = 0; i < jointTokens.size(); i++)
                {
                    auto iter = jointBoneMap.find(jointTokens[i]);
                    if (iter != jointBoneMap.end())
                    {
                        skinMesh->influencedBoneNodes.push_back(iter->second);
                    }
                }
            }
        }
    }
    else
    {
        PXR_NS::UsdSkelSkeleton skeleton;
        const auto& children = usdSkelRoot.GetPrim().GetAllChildren();
        for (const auto& child : children)
        {
            if (child.IsA<PXR_NS::UsdSkelSkeleton>())
            {
                skeleton = PXR_NS::UsdSkelSkeleton(child);
                break;
            }
        }

        if (skeleton)
        {
            rootBone = populateSkeleton(stage, usdSkelCache, skeleton, jointOrder, jointBoneMap);
        }
    }

    return rootBone;
}

OmniConverterMaterialProperty UsdSdkImporter::ConvertMaterialInput(const PXR_NS::UsdShadeInput& input)
{
    OmniConverterMaterialProperty property;
    property.name = input.GetFullName();
    property.displayName = input.GetAttr().GetDisplayName();
    property.groupName = input.GetAttr().GetDisplayGroup();

    auto typeName = input.GetTypeName();
    if (typeName == PXR_NS::SdfValueTypeNames->Bool)
    {
        property.valueType = OMNI_CONVERTER_VALUE_TYPE_BOOL;
        input.Get<bool>(&property.value.boolValue);
    }
    else if (typeName == PXR_NS::SdfValueTypeNames->Asset || typeName == PXR_NS::SdfValueTypeNames->String || typeName == PXR_NS::SdfValueTypeNames->Token)
    {
        if (typeName == PXR_NS::SdfValueTypeNames->Asset)
        {
            property.isTextureProperty = true;
            property.detailedType = OMNI_CONVERTER_MDL_PROPERTY_TYPE_ASSET;
            PXR_NS::SdfAssetPath assetPath;
            input.Get<PXR_NS::SdfAssetPath>(&assetPath);
            if (assetPath.GetResolvedPath().empty())
            {
                property.stringValue = assetPath.GetAssetPath();
            }
            else
            {
                property.stringValue = assetPath.GetResolvedPath();
            }
        }
        else if (typeName == PXR_NS::SdfValueTypeNames->Token)
        {
            property.detailedType = OMNI_CONVERTER_MDL_PROPERTY_TYPE_TOKEN;
        }
        property.valueType = OMNI_CONVERTER_VALUE_TYPE_STRING;
        input.Get<std::string>(&property.stringValue);
    }
    else if (typeName == PXR_NS::SdfValueTypeNames->Int)
    {
        property.valueType = OMNI_CONVERTER_VALUE_TYPE_INT32;
        input.Get<int>(&property.value.intValue);
        auto& attr = input.GetAttr();
        if (attr.HasCustomDataKey(PXR_NS::TfToken("default")))
        {
            const auto& defaultValue = attr.GetCustomDataByKey(PXR_NS::TfToken("default"));
            property.defaultValue.intValue = defaultValue.Get<int>();
        }

        if (attr.HasCustomDataKey(PXR_NS::TfToken("range:min")))
        {
            const auto& minValue = attr.GetCustomDataByKey(PXR_NS::TfToken("range:min"));
            property.minValue.intValue = minValue.Get<int>();
        }

        if (attr.HasCustomDataKey(PXR_NS::TfToken("range:max")))
        {
            const auto& maxValue = attr.GetCustomDataByKey(PXR_NS::TfToken("range:max"));
            property.maxValue.intValue = maxValue.Get<int>();
        }
    }
    else if (typeName == PXR_NS::SdfValueTypeNames->Float || typeName == PXR_NS::SdfValueTypeNames->Double)
    {
        property.valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE;
        input.Get<double>(&property.value.doubleValue);
        auto& attr = input.GetAttr();
        if (attr.HasCustomDataKey(PXR_NS::TfToken("default")))
        {
            const auto& defaultValue = attr.GetCustomDataByKey(PXR_NS::TfToken("default"));
            property.defaultValue.doubleValue = defaultValue.Get<double>();
        }

        if (attr.HasCustomDataKey(PXR_NS::TfToken("range:min")))
        {
            const auto& minValue = attr.GetCustomDataByKey(PXR_NS::TfToken("range:min"));
            property.minValue.doubleValue = minValue.Get<double>();
        }

        if (attr.HasCustomDataKey(PXR_NS::TfToken("range:max")))
        {
            const auto& maxValue = attr.GetCustomDataByKey(PXR_NS::TfToken("range:max"));
            property.maxValue.doubleValue = maxValue.Get<double>();
        }
    }
    else if (typeName == PXR_NS::SdfValueTypeNames->Float2 || typeName == PXR_NS::SdfValueTypeNames->Double2 ||
             typeName == PXR_NS::SdfValueTypeNames->Half2 || typeName == PXR_NS::SdfValueTypeNames->TexCoord2f ||
             typeName == PXR_NS::SdfValueTypeNames->TexCoord2h || typeName == PXR_NS::SdfValueTypeNames->TexCoord2d ||
             typeName == PXR_NS::SdfValueTypeNames->Int2)
    {
        if (typeName == PXR_NS::SdfValueTypeNames->TexCoord2h || typeName == PXR_NS::SdfValueTypeNames->TexCoord2f ||
            typeName == PXR_NS::SdfValueTypeNames->TexCoord2d)
        {
            property.detailedType = OMNI_CONVERTER_MDL_PROPERTY_TYPE_TEXCOORD2D;
        }
        property.valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE2;
        PXR_NS::GfVec2d value;
        input.Get<PXR_NS::GfVec2d>(&value);
        property.value.double2Value[0] = value[0];
        property.value.double2Value[1] = value[1];
    }
    else if (typeName == PXR_NS::SdfValueTypeNames->Float3 || typeName == PXR_NS::SdfValueTypeNames->Double3 ||
             typeName == PXR_NS::SdfValueTypeNames->Half3 || typeName == PXR_NS::SdfValueTypeNames->TexCoord3f ||
             typeName == PXR_NS::SdfValueTypeNames->TexCoord3h || typeName == PXR_NS::SdfValueTypeNames->TexCoord3d ||
             typeName == PXR_NS::SdfValueTypeNames->Int3 || typeName == PXR_NS::SdfValueTypeNames->Vector3h ||
             typeName == PXR_NS::SdfValueTypeNames->Vector3f || typeName == PXR_NS::SdfValueTypeNames->Vector3d ||
             typeName == PXR_NS::SdfValueTypeNames->Normal3h || typeName == PXR_NS::SdfValueTypeNames->Normal3f ||
             typeName == PXR_NS::SdfValueTypeNames->Normal3d || typeName == PXR_NS::SdfValueTypeNames->Point3h ||
             typeName == PXR_NS::SdfValueTypeNames->Point3f || typeName == PXR_NS::SdfValueTypeNames->Point3d ||
             typeName == PXR_NS::SdfValueTypeNames->Color3h || typeName == PXR_NS::SdfValueTypeNames->Color3f ||
             typeName == PXR_NS::SdfValueTypeNames->Color3d)
    {
        if (typeName == PXR_NS::SdfValueTypeNames->TexCoord3h || typeName == PXR_NS::SdfValueTypeNames->TexCoord3f ||
            typeName == PXR_NS::SdfValueTypeNames->TexCoord3d)
        {
            property.detailedType = OMNI_CONVERTER_MDL_PROPERTY_TYPE_TEXCOORD3D;
        }
        else if (typeName == PXR_NS::SdfValueTypeNames->Point3h || typeName == PXR_NS::SdfValueTypeNames->Point3f ||
                 typeName == PXR_NS::SdfValueTypeNames->Point3d)
        {
            property.detailedType = OMNI_CONVERTER_MDL_PROPERTY_TYPE_POINT3D;
        }
        else if (typeName == PXR_NS::SdfValueTypeNames->Color3h || typeName == PXR_NS::SdfValueTypeNames->Color3f ||
                 typeName == PXR_NS::SdfValueTypeNames->Color3d)
        {
            property.detailedType = OMNI_CONVERTER_MDL_PROPERTY_TYPE_COLOR3D;
        }
        else if (typeName == PXR_NS::SdfValueTypeNames->Normal3h || typeName == PXR_NS::SdfValueTypeNames->Normal3f ||
                 typeName == PXR_NS::SdfValueTypeNames->Normal3d)
        {
            property.detailedType = OMNI_CONVERTER_MDL_PROPERTY_TYPE_NORMAL3D;
        }

        property.valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE3;
        PXR_NS::GfVec3d value;
        input.Get<PXR_NS::GfVec3d>(&value);
        property.value.double3Value[0] = value[0];
        property.value.double3Value[1] = value[1];
        property.value.double3Value[2] = value[2];
    }
    else if (typeName == PXR_NS::SdfValueTypeNames->Float4 || typeName == PXR_NS::SdfValueTypeNames->Double4 ||
             typeName == PXR_NS::SdfValueTypeNames->Half4 || typeName == PXR_NS::SdfValueTypeNames->Int4 ||
             typeName == PXR_NS::SdfValueTypeNames->Color4h || typeName == PXR_NS::SdfValueTypeNames->Color4f ||
             typeName == PXR_NS::SdfValueTypeNames->Color4d || typeName == PXR_NS::SdfValueTypeNames->Quath ||
             typeName == PXR_NS::SdfValueTypeNames->Quatf || typeName == PXR_NS::SdfValueTypeNames->Quatd)
    {
        if (typeName == PXR_NS::SdfValueTypeNames->Quatd || typeName == PXR_NS::SdfValueTypeNames->Quatf ||
            typeName == PXR_NS::SdfValueTypeNames->Quath)
        {
            property.detailedType = OMNI_CONVERTER_MDL_PROPERTY_TYPE_QUATD;
        }
        else if (typeName == PXR_NS::SdfValueTypeNames->Color4h || typeName == PXR_NS::SdfValueTypeNames->Color4f ||
                 typeName == PXR_NS::SdfValueTypeNames->Color4d)
        {
            property.detailedType = OMNI_CONVERTER_MDL_PROPERTY_TYPE_COLOR4D;
        }

        property.valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE4;
        PXR_NS::GfVec4d value;
        input.Get<PXR_NS::GfVec4d>(&value);
        property.value.double4Value[0] = value[0];
        property.value.double4Value[1] = value[1];
        property.value.double4Value[2] = value[2];
        property.value.double4Value[3] = value[3];
    }
    else if (typeName == PXR_NS::SdfValueTypeNames->Matrix3d)
    {
        property.detailedType = OMNI_CONVERTER_MDL_PROPERTY_TYPE_MATRIX3D;
        property.valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE9;
        PXR_NS::GfMatrix3d value;
        input.Get<PXR_NS::GfMatrix3d>(&value);
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                property.value.double9Value[i * 3 + j] = value[i][j];
            }
        }
    }
    else if (typeName == PXR_NS::SdfValueTypeNames->Matrix4d)
    {
        property.detailedType = OMNI_CONVERTER_MDL_PROPERTY_TYPE_MATRIX4D;
        property.valueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE16;
        PXR_NS::GfMatrix4d value;
        input.Get<PXR_NS::GfMatrix4d>(&value);
        for (size_t i = 0; i < 4; i++)
        {
            for (size_t j = 0; j < 4; j++)
            {
                property.value.double16Value[i * 4 + j] = value[i][j];
            }
        }
    }
    else
    {
        property.valueType = OMNI_CONVERTER_VALUE_TYPE_UNDEFINED;
    }

    return property;
}

bool UsdSdkImporter::GetGltfTextureInputValue(
    const StagePtr& stage,
    const PXR_NS::UsdShadeShader& shader,
    const std::string& inputName,
    TextureReference& textureReference
)
{
    auto input = shader.GetInput(PXR_NS::TfToken(inputName));
    if (!input)
    {
        return false;
    }

    static auto ToTextureWrapMode = [](int wrapModeValue)
    {
        if (wrapModeValue == 33648)
        {
            return TextureWrapMode::MIRROR;
        }
        else if (wrapModeValue == 33071)
        {
            return TextureWrapMode::CLAMP;
        }

        return TextureWrapMode::REPEAT;
    };

    if (input.HasConnectedSource())
    {
        PXR_NS::UsdShadeConnectableAPI connectedSource;
        PXR_NS::TfToken name;
        PXR_NS::UsdShadeAttributeType attributeType;
        if (input.GetConnectedSource(&connectedSource, &name, &attributeType))
        {
            auto sourcePrim = connectedSource.GetPrim();
            auto textureShader = PXR_NS::UsdShadeShader(sourcePrim);
            if (textureShader) // It's source is shader
            {
            }

            return true;
        }
    }
    else
    {
        PXR_NS::SdfAssetPath texturePath;
        GetInputValue<PXR_NS::SdfAssetPath>(input, texturePath);
        textureReference.imageIndex = GetOrCreateTexture(stage, texturePath);
    }

    return textureReference.IsValid();
}

bool UsdSdkImporter::GetPreviewSurfaceTextureInputValue(
    const StagePtr& stage,
    const PXR_NS::UsdShadeShader& shader,
    const std::string& inputName,
    TextureReference& textureReference
)
{
    auto input = shader.GetInput(PXR_NS::TfToken(inputName));
    if (!input)
    {
        return false;
    }

    static auto ToTextureWrapMode = [](const std::string& wrapModeValue)
    {
        if (wrapModeValue == "mirror")
        {
            return TextureWrapMode::MIRROR;
        }
        else if (wrapModeValue == "clamp")
        {
            return TextureWrapMode::CLAMP;
        }

        return TextureWrapMode::REPEAT;
    };

    auto ToTextureOutputMode = [this](const std::string& outputName)
    {
        if (outputName == "rgb")
        {
            return TextureOutputMode::RGB;
        }
        else if (outputName == "a")
        {
            return TextureOutputMode::ALPHA;
        }
        else if (outputName == "r")
        {
            return TextureOutputMode::R;
        }
        else if (outputName == "g")
        {
            return TextureOutputMode::G;
        }
        else if (outputName == "b")
        {
            return TextureOutputMode::B;
        }
        else if (outputName == "rgba")
        {
            return TextureOutputMode::RGBA;
        }

        return TextureOutputMode::AVERAGE;
    };

    static auto checkId = [](PXR_NS::UsdShadeShader shader, const std::string& id)
    {
        PXR_NS::UsdAttribute idAttribute = shader.GetIdAttr();
        PXR_NS::TfToken value;
        if (idAttribute && idAttribute.Get(&value))
        {
            return value == PXR_NS::TfToken(id);
        }

        return false;
    };

    if (input.HasConnectedSource())
    {
        PXR_NS::UsdShadeConnectableAPI connectedSource;
        PXR_NS::TfToken name;
        PXR_NS::UsdShadeAttributeType attributeType;
        if (input.GetConnectedSource(&connectedSource, &name, &attributeType))
        {
            auto sourcePrim = connectedSource.GetPrim();
            auto textureShader = PXR_NS::UsdShadeShader(sourcePrim);
            if (textureShader && checkId(textureShader, "UsdUVTexture")) // It's source is shader
            {
                textureReference.outputMode = ToTextureOutputMode(name);

                PXR_NS::SdfAssetPath textureAssetPath;
                if (GetShaderInputValue(textureAssetPath, textureShader, "file"))
                {
                    textureReference.imageIndex = GetOrCreateTexture(stage, textureAssetPath);
                }

                GetShaderInputValue(textureReference.scale, textureShader, "scale");
                GetShaderInputValue(textureReference.bias, textureShader, "bias");

                PXR_NS::TfToken wrapS;
                if (GetShaderInputValue(wrapS, textureShader, "wrapS"))
                {
                    textureReference.wrapS = ToTextureWrapMode(wrapS.GetString());
                }

                PXR_NS::TfToken wrapT;
                if (GetShaderInputValue(wrapT, textureShader, "wrapT"))
                {
                    textureReference.wrapT = ToTextureWrapMode(wrapT.GetString());
                }

                auto uvInput = textureShader.GetInput(PXR_NS::TfToken("st"));
                if (uvInput && uvInput.HasConnectedSource())
                {
                    if (uvInput.GetConnectedSource(&connectedSource, &name, &attributeType))
                    {
                        auto uvInputShader = PXR_NS::UsdShadeShader(connectedSource.GetPrim());
                        if (uvInputShader && checkId(uvInputShader, "UsdTransform2d"))
                        {
                            PXR_NS::GfVec2f scale(1.0f, 1.0f);
                            PXR_NS::GfVec2f translation(0.0f, 0.0f);
                            float rotationAngles = 0.0f;
                            GetShaderInputValue(scale, uvInputShader, "scale");
                            if (PXR_NS::GfIsClose(scale[0], 0.0f, 1e-06))
                            {
                                scale[0] = 0.00001f;
                            }

                            if (PXR_NS::GfIsClose(scale[1], 0.0f, 1e-06))
                            {
                                scale[1] = 0.00001f;
                            }

                            GetShaderInputValue(rotationAngles, uvInputShader, "rotation");
                            GetShaderInputValue(translation, uvInputShader, "translation");
                            textureReference.transform.scale = scale;
                            textureReference.transform.translation = translation;
                            textureReference.transform.rotation = PXR_NS::GfVec3f(rotationAngles);
                        }
                    }
                }
            }

            return true;
        }
    }

    return false;
}

size_t UsdSdkImporter::GetOrCreateTexture(const StagePtr& stage, const PXR_NS::SdfAssetPath& assetPath)
{
    std::string texturePath = assetPath.GetResolvedPath();
    if (texturePath.empty())
    {
        texturePath = assetPath.GetAssetPath();
    }

    size_t imageIndex = -1;
    if (!texturePath.empty())
    {
        auto iter = mTextureIndex.find(texturePath);
        if (iter != mTextureIndex.end())
        {
            imageIndex = iter->second;
        }
        else
        {
            TextureImagePtr image;

            // It needs to use USD IO to read embedding usdz asset.
            if (mThreadContext->converterContext.GetImportAssetType() == AssetType::USDZ)
            {
                PXR_NS::ArResolver& resolver = PXR_NS::ArGetResolver();
#if PXR_MINOR_VERSION > 21 || (PXR_MINOR_VERSION == 21 && PXR_PATCH_VERSION >= 8)
                std::shared_ptr<PXR_NS::ArAsset> usdzAsset = resolver.OpenAsset(PXR_NS::ArResolvedPath(texturePath));
#else
                std::shared_ptr<PXR_NS::ArAsset> usdzAsset = resolver.OpenAsset(texturePath);
#endif
                if (usdzAsset->GetSize() > 0)
                {
                    image = std::make_shared<TextureImage>();
                    image->originalPath = assetPath.GetAssetPath();
                    image->realPath = image->originalPath;
                    image->blob = createOmniConverterBlob(new uint8_t[usdzAsset->GetSize()], usdzAsset->GetSize());
                    usdzAsset->Read(image->blob->buffer, image->blob->size, 0);
                }
            }
            else
            {
                image = std::make_shared<TextureImage>();
                image->originalPath = texturePath;
                image->realPath = texturePath;
            }

            if (image)
            {
                stage->images.push_back(image);
                imageIndex = stage->images.size() - 1;
                mTextureIndex.insert({ image->realPath, imageIndex });
            }
        }
    }

    return imageIndex;
}

