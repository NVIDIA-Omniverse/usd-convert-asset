// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "usd_exporter.h"

#include "../../common/common.h"
#include "../../utils/utils.h"

#include <chrono>
#include <stack>

#define ENSURE_STATUS_OK(status)                                                                                                                     \
    {                                                                                                                                                \
        auto res = status;                                                                                                                           \
        if (res != OmniConverterStatus::OK)                                                                                                          \
        {                                                                                                                                            \
            return res;                                                                                                                              \
        }                                                                                                                                            \
    }

const static std::string MATERIAL_DIR_NAME = "materials";
const static std::string MESH_DIR_NAME = "props";
const static std::string ANIMATION_DIR_NAME = "animations";
const static std::string TEXTURE_DIR_NAME = "textures";

const static std::string SKELETON_PRIM_NAME = "Skeleton";
const static std::string MATERIAL_GROUP_PRIM_NAME = "Looks";
const static PXR_NS::SdfPath PROTOTYPE_MESH_GROUP_PATH = PXR_NS::SdfPath("/__prototype_meshes__");

#pragma warning(push)
#pragma warning(disable : 4003) // = Conversion from double to float / int to float
PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((mesh, "Mesh"))
    ((points, "points"))
    ((Points, "Points"))
    ((rangeMin, "range:min"))
    ((rangeMax, "range:max"))
    ((st, "st"))
    ((skelAnimation, "SkelAnimation"))
    ((sphereLight, "SphereLight"))
    ((distantLight, "DistantLight"))
    ((rectLight, "RectLight"))
    ((camera, "Camera"))
    ((xform, "Xform"))
    ((scope, "Scope"))
    ((skelBindingAPI, "SkelBindingAPI"))
    ((baseMonoMode, "::base::mono_mode"))
    ((sdrEnumValue, "__SDR__enum_value"))
    ((options, "options"))
    ((shader, "Shader"))
    ((gltfWrappingMode, "gltf_wrapping_mode"))
    ((gltfNormalTextureLookupValue, "gltf_normal_texture_lookup_value"))
    ((gltfTextureLookupValue, "gltf_texture_lookup_value"))
    ((gltfAlphaMode, "gltf_alpha_mode"))
    ((texCoordReader, "TexCoordReader"))
    ((usdPrimvarReaderFloat2, "UsdPrimvarReader_float2"))
    ((usdTransform2d, "UsdTransform2d"))
    ((material, "Material"))
    ((renderType, "renderType"))
    ((skelRootType, "SkelRoot"))
    ((skeletonType, "Skeleton"))
    ((geomSubsetType, "GeomSubset"))
    ((translateOp, "xformOp:translate"))
    ((scaleOp, "xformOp:scale"))
    ((orientOp, "xformOp:orient"))
    ((rotateOp, "xformOp:rotateXYZ"))
);
// clang-format on

PXR_NAMESPACE_CLOSE_SCOPE
#pragma warning(pop)

#ifdef DEBUG
const static std::string USD_FILE_EXT = ".usda";
#else
const static std::string USD_FILE_EXT = ".usd";
#endif

static std::string MakeValidIdentifier(const std::string& in)
{
// see https://openusd.org/dev/api/_usd__page__u_t_f_8.html
#if PXR_MINOR_VERSION >= 24
    if (in.empty())
    {
        return "_";
    }

    constexpr PXR_NS::TfUtf8CodePoint cp_underscore = PXR_NS::TfUtf8CodePointFromAscii('_');

    bool first_cp = true;
    std::stringstream stream;
    for (auto cp : PXR_NS::TfUtf8CodePointView{ in })
    {
        const bool cp_allowed = first_cp ? (cp == cp_underscore || PXR_NS::TfIsUtf8CodePointXidStart(cp)) : PXR_NS::TfIsUtf8CodePointXidContinue(cp);
        if (!cp_allowed)
        {
            stream << '_';
        }
        else
        {
            stream << cp;
        }

        first_cp = false;
    }
    return stream.str();
#else
    return PXR_NS::TfMakeValidIdentifier(in);
#endif
}


static std::string MakeValidUSDIdentifier(const std::string& name, const std::string& prefix)
{
    auto validName = MakeValidIdentifier(name);
    if (validName.empty() || validName[0] == '_')
    {
        validName = prefix + validName;
    }

    return validName;
}

static PXR_NS::TfToken ProjectionToToken(PXR_NS::GfCamera::Projection projection)
{
    switch (projection)
    {
        case PXR_NS::GfCamera::Perspective:
            return PXR_NS::UsdGeomTokens->perspective;
        case PXR_NS::GfCamera::Orthographic:
            return PXR_NS::UsdGeomTokens->orthographic;
        default:
            return PXR_NS::UsdGeomTokens->perspective;
    }
}

static PXR_NS::SdfPrimSpecHandle GetOrCreatePrimSpec(
    PXR_NS::SdfLayerHandle layer,
    const PXR_NS::SdfPath& path,
    PXR_NS::TfToken type = PXR_NS::TfToken(),
    bool def = true,
    const std::string& displayName = std::string()
)
{
    auto spec = layer->GetPrimAtPath(path);
    if (spec)
    {
        return spec;
    }

    spec = PXR_NS::SdfCreatePrimInLayer(layer, path);
    if (def)
    {
        spec->SetSpecifier(PXR_NS::SdfSpecifierDef);

        if (!type.IsEmpty())
        {
            spec->SetTypeName(type);
        }
    }

    if (!displayName.empty())
    {
        spec->SetField(PXR_NS::SdfFieldKeys->DisplayName, displayName.c_str());
    }

    return spec;
};

static PXR_NS::SdfAttributeSpecHandle GetOrNewSdfAttributeSpec(
    const PXR_NS::SdfPrimSpecHandle& owner,
    const std::string& name,
    const PXR_NS::SdfValueTypeName& typeName,
    PXR_NS::SdfVariability variability = PXR_NS::SdfVariabilityVarying,
    bool custom = false
)
{
    const PXR_NS::SdfPath& attributePath = owner->GetPath().AppendProperty(PXR_NS::TfToken(name));
    auto attributeSpec = owner->GetAttributeAtPath(attributePath);
    if (!attributeSpec)
    {
        attributeSpec = PXR_NS::SdfAttributeSpec::New(owner, name, typeName, variability, custom);
    }

    return attributeSpec;
}

static PXR_NS::SdfRelationshipSpecHandle NewSdfRelationshipSpec(
    const PXR_NS::SdfPrimSpecHandle& owner,
    const std::string& name,
    bool custom = true,
    PXR_NS::SdfVariability variability = PXR_NS::SdfVariabilityUniform
)
{
    const PXR_NS::SdfPath& attributePath = owner->GetPath().AppendProperty(PXR_NS::TfToken(name));
    PXR_NS::SdfRelationshipSpecHandle relationshipSpec;
    if (auto spec = owner->GetObjectAtPath(attributePath))
    {
        relationshipSpec = PXR_NS::TfDynamic_cast<PXR_NS::SdfRelationshipSpecHandle>(spec);
    }
    else
    {
        relationshipSpec = PXR_NS::SdfRelationshipSpec::New(owner, name, custom, variability);
    }

    return relationshipSpec;
}

static PXR_NS::SdfAttributeSpecHandle FindOrAddOp(
    PXR_NS::SdfPrimSpecHandle primSpec,
    PXR_NS::UsdGeomXformOp::Type xformOpType,
    bool doublePrecision,
    PXR_NS::TfToken const& opSuffix = PXR_NS::TfToken(),
    bool invert = false,
    bool addXformOpOrder = true
)
{
    auto precision = doublePrecision ? PXR_NS::UsdGeomXformOp::PrecisionDouble : PXR_NS::UsdGeomXformOp::PrecisionFloat;
    const PXR_NS::SdfValueTypeName& typeName = PXR_NS::UsdGeomXformOp::GetValueTypeName(xformOpType, precision);
    const PXR_NS::TfToken attrName = PXR_NS::UsdGeomXformOp::GetOpName(xformOpType, opSuffix);
    const PXR_NS::TfToken opOrderName = PXR_NS::UsdGeomXformOp::GetOpName(xformOpType, opSuffix, invert);

    if (addXformOpOrder)
    {
        PXR_NS::VtTokenArray xformOpOrder;
        auto opOrderPath = primSpec->GetPath().AppendProperty(PXR_NS::UsdGeomTokens->xformOpOrder);
        auto opOrderAttrSpec = GetOrNewSdfAttributeSpec(
            primSpec,
            PXR_NS::UsdGeomTokens->xformOpOrder,
            PXR_NS::SdfValueTypeNames->TokenArray,
            PXR_NS::SdfVariabilityUniform
        );

        auto path = primSpec->GetPath();
        auto vtValue = opOrderAttrSpec->GetDefaultValue();

        if (vtValue.IsHolding<PXR_NS::VtTokenArray>())
        {
            xformOpOrder = vtValue.UncheckedGet<PXR_NS::VtTokenArray>();
        }

        for (auto xformOp : xformOpOrder)
        {
            // To differentiate translate and translate:pivot
            if (xformOp == opOrderName)
            {
                auto opPath = primSpec->GetPath().AppendProperty(attrName);
                auto attributeSpec = primSpec->GetAttributeAtPath(opPath);
                if (attributeSpec)
                {
                    return attributeSpec;
                }
            }
        }

        xformOpOrder.push_back(opOrderName);
        opOrderAttrSpec->SetDefaultValue(PXR_NS::VtValue(xformOpOrder));
    }

    return GetOrNewSdfAttributeSpec(primSpec, attrName, typeName);
};

static void SetDefaultTransform(PXR_NS::SdfPrimSpecHandle primSpec, const Transform& transform, bool doublePrecision, bool useTES)
{
    if (useTES)
    {
        const auto& tes = transform.GetTES();
        auto translateOp = FindOrAddOp(primSpec, PXR_NS::UsdGeomXformOp::TypeTranslate, true);
        translateOp->SetDefaultValue(PXR_NS::VtValue(tes.t));

        if (transform.GetPivot() != ZERO_VEC_3D)
        {
            auto pivotOp = FindOrAddOp(primSpec, PXR_NS::UsdGeomXformOp::TypeTranslate, doublePrecision, PXR_NS::UsdGeomTokens->pivot);
            if (doublePrecision)
            {
                pivotOp->SetDefaultValue(PXR_NS::VtValue(transform.GetPivot()));
            }
            else
            {
                pivotOp->SetDefaultValue(PXR_NS::VtValue(PXR_NS::GfVec3f(transform.GetPivot())));
            }
        }

        auto rotationOp = FindOrAddOp(primSpec, PXR_NS::UsdGeomXformOp::TypeRotateXYZ, doublePrecision);
        if (doublePrecision)
        {
            rotationOp->SetDefaultValue(PXR_NS::VtValue(tes.r));
        }
        else
        {
            rotationOp->SetDefaultValue(PXR_NS::VtValue(PXR_NS::GfVec3f(tes.r)));
        }

        auto scalingOp = FindOrAddOp(primSpec, PXR_NS::UsdGeomXformOp::TypeScale, doublePrecision);
        if (doublePrecision)
        {
            scalingOp->SetDefaultValue(PXR_NS::VtValue(tes.s));
        }
        else
        {
            scalingOp->SetDefaultValue(PXR_NS::VtValue(PXR_NS::GfVec3f(tes.s)));
        }
    }
    else
    {
        const auto& tqs = transform.GetTQS();
        auto translateOp = FindOrAddOp(primSpec, PXR_NS::UsdGeomXformOp::TypeTranslate, true);
        translateOp->SetDefaultValue(PXR_NS::VtValue(tqs.t));

        if (transform.GetPivot() != ZERO_VEC_3D)
        {
            auto pivotOp = FindOrAddOp(primSpec, PXR_NS::UsdGeomXformOp::TypeTranslate, doublePrecision, PXR_NS::UsdGeomTokens->pivot);
            if (doublePrecision)
            {
                pivotOp->SetDefaultValue(PXR_NS::VtValue(transform.GetPivot()));
            }
            else
            {
                pivotOp->SetDefaultValue(PXR_NS::VtValue(PXR_NS::GfVec3f(transform.GetPivot())));
            }
        }

        auto rotationOp = FindOrAddOp(primSpec, PXR_NS::UsdGeomXformOp::TypeOrient, doublePrecision);
        if (doublePrecision)
        {
            rotationOp->SetDefaultValue(PXR_NS::VtValue(tqs.q));
        }
        else
        {
            rotationOp->SetDefaultValue(PXR_NS::VtValue(PXR_NS::GfQuatf(tqs.q)));
        }

        auto scalingOp = FindOrAddOp(primSpec, PXR_NS::UsdGeomXformOp::TypeScale, doublePrecision);
        if (doublePrecision)
        {
            scalingOp->SetDefaultValue(PXR_NS::VtValue(tqs.s));
        }
        else
        {
            scalingOp->SetDefaultValue(PXR_NS::VtValue(PXR_NS::GfVec3f(tqs.s)));
        }
    }

    if (transform.GetPivot() != ZERO_VEC_3D)
    {
        FindOrAddOp(primSpec, PXR_NS::UsdGeomXformOp::TypeTranslate, doublePrecision, PXR_NS::UsdGeomTokens->pivot, true);
    }
}

static void SetTranslateSample(
    PXR_NS::SdfPrimSpecHandle primSpec,
    const PXR_NS::GfVec3d& translate,
    const PXR_NS::UsdTimeCode& timeCode,
    bool doublePrecision,
    bool addXformOpOrder = true
)
{
    auto translateOp = FindOrAddOp(primSpec, PXR_NS::UsdGeomXformOp::TypeTranslate, doublePrecision, {}, false, addXformOpOrder);
    auto layer = primSpec->GetLayer();
    layer->SetTimeSample(translateOp->GetPath(), timeCode.GetValue(), PXR_NS::VtValue(translate));
}

static void SetScaleSample(
    PXR_NS::SdfPrimSpecHandle primSpec,
    const PXR_NS::GfVec3d& scale,
    const PXR_NS::UsdTimeCode& timeCode,
    bool doublePrecision,
    bool addXformOpOrder = true
)
{
    auto scaleOp = FindOrAddOp(primSpec, PXR_NS::UsdGeomXformOp::TypeScale, doublePrecision, {}, false, addXformOpOrder);
    auto layer = primSpec->GetLayer();
    if (doublePrecision)
    {
        layer->SetTimeSample(scaleOp->GetPath(), timeCode.GetValue(), PXR_NS::VtValue(scale));
    }
    else
    {
        layer->SetTimeSample(scaleOp->GetPath(), timeCode.GetValue(), PXR_NS::VtValue(PXR_NS::GfVec3f(scale)));
    }
}

static void SetRotateXYZSample(
    PXR_NS::SdfPrimSpecHandle primSpec,
    const PXR_NS::GfVec3d& rotation,
    const PXR_NS::UsdTimeCode& timeCode,
    bool doublePrecision,
    bool addXformOpOrder = true
)
{
    auto rotationOp = FindOrAddOp(primSpec, PXR_NS::UsdGeomXformOp::TypeRotateXYZ, doublePrecision, {}, false, addXformOpOrder);
    auto layer = primSpec->GetLayer();
    if (doublePrecision)
    {
        layer->SetTimeSample(rotationOp->GetPath(), timeCode.GetValue(), PXR_NS::VtValue(rotation));
    }
    else
    {
        layer->SetTimeSample(rotationOp->GetPath(), timeCode.GetValue(), PXR_NS::VtValue(PXR_NS::GfVec3f(rotation)));
    }
}

static void SetOrientSample(
    PXR_NS::SdfPrimSpecHandle primSpec,
    const PXR_NS::GfQuatd& orient,
    const PXR_NS::UsdTimeCode& timeCode,
    bool doublePrecision,
    bool addXformOpOrder = true
)
{
    auto orientOp = FindOrAddOp(primSpec, PXR_NS::UsdGeomXformOp::TypeOrient, doublePrecision, {}, false, addXformOpOrder);
    auto layer = primSpec->GetLayer();
    if (doublePrecision)
    {
        layer->SetTimeSample(orientOp->GetPath(), timeCode.GetValue(), PXR_NS::VtValue(orient));
    }
    else
    {
        layer->SetTimeSample(orientOp->GetPath(), timeCode.GetValue(), PXR_NS::VtValue(PXR_NS::GfQuatf(orient)));
    }
}

// Checks if all normal vectors in a given array are effectively zero.
static bool AllNormalsAreZero(const PXR_NS::VtArray<PXR_NS::GfVec3f>& normals)
{
    for (const auto& n : normals)
    {
        if (n.GetLengthSq() > 1e-6f)
        {
            return false; // Found a non-zero normal
        }
    }
    return true; // All normals are zero
}

// Necessary for GCC
namespace
{

// Derived from Pixar OpenUSD pxr/usd/usd/valueUtils.h (Copyright 2017 Pixar),
// licensed under the terms at https://openusd.org/license.
// Modified locally to support forcing explicit list edits.
// Helper that implements the various options for adding items to lists
// enumerated by UsdListPosition.
//
// If the list op is in explicit mode, the item will be inserted into the
// explicit list regardless of the list specified in the position enum.
//
// If the item already exists in the list, but not in the requested
// position, it will be moved to the requested position.
template <class PROXY>
void Usd_InsertListItem(
    PROXY proxy,
    const typename PROXY::value_type& item,
    bool makeExplicit = false,
    PXR_NS::UsdListPosition position = PXR_NS::UsdListPositionBackOfPrependList
)
{
    if (makeExplicit)
    {
        proxy.ClearEditsAndMakeExplicit();
    }

    typename PROXY::ListProxy list(/* unused */ PXR_NS::SdfListOpTypeExplicit);
    bool atFront = false;
    switch (position)
    {
        case PXR_NS::UsdListPositionBackOfPrependList:
            list = proxy.GetPrependedItems();
            atFront = false;
            break;
        case PXR_NS::UsdListPositionFrontOfPrependList:
            list = proxy.GetPrependedItems();
            atFront = true;
            break;
        case PXR_NS::UsdListPositionBackOfAppendList:
            list = proxy.GetAppendedItems();
            atFront = false;
            break;
        case PXR_NS::UsdListPositionFrontOfAppendList:
            list = proxy.GetAppendedItems();
            atFront = true;
            break;
    }

    // This function previously used SdfListEditorProxy::Add, which would
    // update the explicit list if the list op was in explicit mode. Clients
    // currently expect this behavior, so we need to maintain it regardless
    // of the list specified in the postiion enum.
    if (proxy.IsExplicit())
    {
        list = proxy.GetExplicitItems();
    }

    if (list.empty())
    {
        list.Insert(-1, item);
    }
    else
    {
        const size_t pos = list.Find(item);
        if (pos != size_t(-1))
        {
            const size_t targetPos = atFront ? 0 : list.size() - 1;
            if (pos == targetPos)
            {
                // Item already exists in the right position.
                return;
            }
            list.Erase(pos);
        }
        list.Insert(atFront ? 0 : -1, item);
    }
}

template <class BindingAPI>
bool ApplyBindingAPIToPrimSpec(PXR_NS::SdfPrimSpecHandle primSpec)
{
    static PXR_NS::TfType schemaType = PXR_NS::TfType::Find<BindingAPI>(); // PXR_NS::TfType::Find<PXR_NS::UsdSkelBindingAPI>();
    const PXR_NS::TfToken typeName = PXR_NS::UsdSchemaRegistry::GetSchemaTypeName(schemaType);

    auto hasItem = [](const PXR_NS::TfTokenVector& items, const PXR_NS::TfToken& item)
    {
        return std::find(items.begin(), items.end(), item) != items.end();
    };

    PXR_NS::SdfTokenListOp listOp = primSpec->GetInfo(PXR_NS::UsdTokens->apiSchemas).Get<PXR_NS::SdfTokenListOp>();
    const PXR_NS::TfTokenVector& explicitItems = listOp.GetExplicitItems();
    const PXR_NS::TfTokenVector& preItems = listOp.GetPrependedItems();
    const PXR_NS::TfTokenVector& appItems = listOp.GetAppendedItems();
    if (hasItem(explicitItems, typeName) || hasItem(preItems, typeName) || hasItem(appItems, typeName))
    {
        return true;
    }

    if (!explicitItems.empty())
    {
        if (!listOp.ReplaceOperations(PXR_NS::SdfListOpTypeExplicit, explicitItems.size(), 0, { typeName }))
        {
            return false;
        }
    }
    else
    {
        // Use ReplaceOperations to append in place.
        if (!listOp.ReplaceOperations(PXR_NS::SdfListOpTypePrepended, preItems.size(), 0, { typeName }))
        {
            return false;
        }
    }

    primSpec->SetInfo(PXR_NS::UsdTokens->apiSchemas, PXR_NS::VtValue::Take(listOp));

    return true;
}

PXR_NS::SdfAttributeSpecHandle CreateAndSetAttrSpec(
    PXR_NS::SdfPrimSpecHandle primSpec,
    const PXR_NS::TfToken& name,
    PXR_NS::SdfValueTypeName valueType,
    const PXR_NS::VtValue& value = PXR_NS::VtValue(),
    PXR_NS::SdfVariability variability = PXR_NS::SdfVariability::SdfVariabilityVarying,
    bool custom = false
)
{
    auto attrSpec = GetOrNewSdfAttributeSpec(primSpec, name, valueType, variability, custom);

    if (!value.IsEmpty())
    {
        attrSpec->SetDefaultValue(value);
    }

    return attrSpec;
};

PXR_NS::SdfAttributeSpecHandle CreateShadeSpecInput(
    PXR_NS::SdfPrimSpecHandle shaderSpec,
    const std::string& inputName,
    PXR_NS::SdfValueTypeName valueType,
    const PXR_NS::VtValue& value = PXR_NS::VtValue()
)
{
    auto inputFullName = PXR_NS::TfToken(PXR_NS::UsdShadeTokens->inputs.GetString() + inputName);
    auto shaderAttrSpec = CreateAndSetAttrSpec(shaderSpec, inputFullName, valueType, value);

    return shaderAttrSpec;
};

PXR_NS::SdfAttributeSpecHandle CreateShadeSpecOutput(
    PXR_NS::SdfPrimSpecHandle shaderSpec,
    const std::string& inputName,
    PXR_NS::SdfValueTypeName valueType,
    const PXR_NS::VtValue& value = PXR_NS::VtValue()
)
{
    auto inputFullName = PXR_NS::TfToken(PXR_NS::UsdShadeTokens->outputs.GetString() + inputName);
    auto shaderAttrSpec = CreateAndSetAttrSpec(shaderSpec, inputFullName, valueType, value);

    return shaderAttrSpec;
};

PXR_NS::SdfAttributeSpecHandle CreateShadeSpecType(
    PXR_NS::SdfPrimSpecHandle shaderSpec,
    PXR_NS::TfToken tokenType,
    PXR_NS::SdfValueTypeName valueType,
    const PXR_NS::VtValue& value = PXR_NS::VtValue()
)
{
    auto attrSpec = CreateAndSetAttrSpec(shaderSpec, tokenType, valueType, value, PXR_NS::SdfVariabilityUniform);

    return attrSpec;
};

PXR_NS::SdfAttributeSpecHandle CreateUsdShadeInput(
    PXR_NS::SdfPrimSpecHandle shaderSpec,
    const std::string& inputName,
    const PXR_NS::VtValue& value,
    PXR_NS::SdfValueTypeName valueType,
    const std::string& displayName,
    const std::string& groupName,
    const PXR_NS::VtValue& defaultValue = PXR_NS::VtValue(),
    const PXR_NS::VtValue& minValue = PXR_NS::VtValue(),
    const PXR_NS::VtValue& maxValue = PXR_NS::VtValue()
)
{
    auto inputFullName = PXR_NS::TfToken(PXR_NS::UsdShadeTokens->inputs.GetString() + inputName);
    auto shaderAttrSpec = CreateAndSetAttrSpec(shaderSpec, inputFullName, valueType, value);
    shaderAttrSpec->SetDisplayName(displayName);
    shaderAttrSpec->SetDisplayGroup(groupName);

    if (!defaultValue.IsEmpty())
    {
        shaderAttrSpec->GetLayer()
            ->SetFieldDictValueByKey(shaderAttrSpec->GetPath(), PXR_NS::SdfFieldKeys->CustomData, PXR_NS::SdfFieldKeys->Default, defaultValue);
    }

    if (!minValue.IsEmpty())
    {
        shaderAttrSpec->GetLayer()->SetFieldDictValueByKey(
            shaderAttrSpec->GetPath(),
            PXR_NS::SdfFieldKeys->CustomData,
            PXR_NS::_tokens->rangeMin,
            PXR_NS::VtValue(minValue)
        );
    }
    if (!maxValue.IsEmpty())
    {
        shaderAttrSpec->GetLayer()->SetFieldDictValueByKey(
            shaderAttrSpec->GetPath(),
            PXR_NS::SdfFieldKeys->CustomData,
            PXR_NS::_tokens->rangeMax,
            PXR_NS::VtValue(maxValue)
        );
    }

    return shaderAttrSpec;
};

bool HasAuthoredVariant(PXR_NS::SdfPrimSpecHandle defaultPrimSpec, const std::string& variantSetName, const std::string& variantName)
{
    static const PXR_NS::TfToken field = PXR_NS::SdfChildrenKeys->VariantChildren;
    const PXR_NS::SdfPath vsetPath = defaultPrimSpec->GetPath().AppendVariantSelection(variantSetName, std::string());
    std::unordered_set<std::string> namesSet;
    PXR_NS::TfTokenVector vsetNames;
    const auto& layer = defaultPrimSpec->GetLayer();
    if (layer->HasField(vsetPath, field, &vsetNames))
    {
        TF_FOR_ALL(name, vsetNames)
        {
            if (name->GetString() == variantName)
            {
                return true;
            }
        }
    }

    return false;
}

PXR_NS::SdfVariantSpecHandle AddVariant(PXR_NS::SdfPrimSpecHandle defaultPrimSpec, const std::string& variantSetName, const std::string& variantName)
{

    PXR_NS::SdfLayerHandle layer = defaultPrimSpec->GetLayer();
    auto variantPath = defaultPrimSpec->GetPath().AppendVariantSelection(variantSetName, variantName);
    bool existed = HasAuthoredVariant(defaultPrimSpec, variantSetName, variantName);
    PXR_NS::SdfVariantSpecHandle variantSpec;
    if (!existed)
    {
        PXR_NS::SdfVariantSetSpecHandle variantSetSpec;

        PXR_NS::SdfPath varSetPath = defaultPrimSpec->GetPath().AppendVariantSelection(variantSetName, std::string());
        auto spec = layer->GetObjectAtPath(varSetPath);
        if (spec)
        {
            variantSetSpec = PXR_NS::TfDynamic_cast<PXR_NS::SdfVariantSetSpecHandle>(spec);
        }
        else
        {
            variantSetSpec = PXR_NS::SdfVariantSetSpec::New(defaultPrimSpec, variantSetName);
            Usd_InsertListItem(defaultPrimSpec->GetVariantSetNameList(), variantSetName);
        }
        variantSpec = PXR_NS::SdfVariantSpec::New(variantSetSpec, variantName);
    }
    else
    {
        auto spec = layer->GetObjectAtPath(variantPath);
        variantSpec = PXR_NS::TfDynamic_cast<PXR_NS::SdfVariantSpecHandle>(spec);
    }

    return variantSpec;
}
} // namespace

OmniConverterStatus UsdExporter::Export(const StagePtr& stage, std::string& detailedError)
{
    Log("Starting to export asset with USD exporter.");

    std::string realExportRoot;
    std::string realMainUsdPath;
    std::string tempExportRoot;

    // It uses different set of defined folders for in-memory or not in-memory export.
    // For in-memory export, only textures are on the real disk. All USD content will be
    // in memory. And only UsdPreviewSurface is supported for material import.
    const std::string& outputPath = mExportContext->converterContext.GetOutputAssetPath();
    if (mExportContext->converterContext.IsInMemoryOutput())
    {
        auto inMemoryOutputLayer = PXR_NS::SdfLayer::Find(outputPath);
        if (!inMemoryOutputLayer)
        {
            Log("Failed to find in memory layer for USD export: " + outputPath);
            return OmniConverterStatus::FILE_NOT_EXISTED;
        }

        mLayerHolders[outputPath] = inMemoryOutputLayer;
        if (mExportContext->converterContext.IsCachingEnabled() && mExportContext->converterContext.GetImportAssetDigest().size() > 0)
        {
            realExportRoot = PathUtils::JoinPaths(
                mExportContext->converterContext.GetCacheFolder(),
                mExportContext->converterContext.GetImportAssetDigest()
            );
            tempExportRoot = PathUtils::JoinPaths(
                mExportContext->converterContext.GetCacheFolder(),
                mExportContext->converterContext.GetImportAssetDigest() + "_to_be_renamed"
            );
            mMainUSDExportPath = PathUtils::JoinPaths(tempExportRoot, CACHED_USD_MAIN_FILE_NAME);
            realMainUsdPath = PathUtils::JoinPaths(realExportRoot, CACHED_USD_MAIN_FILE_NAME);
        }
        else
        {
            tempExportRoot = mExportContext->converterContext.GetImportAssetDir();
            mMainUSDExportPath = outputPath;
            if (!PathUtils::CreateTempFolder(tempExportRoot))
            {
                Log("Failed to create temp folder. Using asset folder instead: " + tempExportRoot);
            }
            else
            {
                Log("Temp folder created to host all temp files: " + tempExportRoot);
            }
        }
    }
    else
    {
        tempExportRoot = mExportContext->converterContext.GetOutputAssetDir();
        mMainUSDExportPath = outputPath;
    }

    // Batch export for all.
    PXR_NS::SdfChangeBlock changeBlock;

    // Prepare export dirs and options
    mPropsExportRoot = PathUtils::JoinPaths(tempExportRoot, MESH_DIR_NAME);
    mMaterialsExportRoot = PathUtils::JoinPaths(tempExportRoot, MATERIAL_DIR_NAME);
    if (mExportContext->converterContext.SingleMesh())
    {
        mTexturesExportRoot = PathUtils::JoinPaths(mMaterialsExportRoot, TEXTURE_DIR_NAME);
    }
    else
    {
        mTexturesExportRoot = PathUtils::JoinPaths(tempExportRoot, TEXTURE_DIR_NAME);
    }
    mAnimationsExportRoot = PathUtils::JoinPaths(tempExportRoot, ANIMATION_DIR_NAME);

    uint32_t totalSteps = GetTotalExportSteps(stage);
    mExportContext->StartProgress(totalSteps);

    if (!stage->rootNode)
    {
        return OmniConverterStatus::OK;
    }
    PreprocessStage(stage);

    if (stage->meshes.size() == 0 && stage->animationTracks.size() > 0 && !mStageNodeInfos[stage->rootNode].hasSkeleton)
    {
        return ExportAnimationClip(stage, detailedError);
    }

    auto status = ExportTextures(stage, detailedError);
    if (status == OmniConverterStatus::OK)
    {
        {
            status = ExportStageTree(stage, detailedError);
            if (status == OmniConverterStatus::OK)
            {
                ExportAnimations(stage, detailedError);
            }
        }
    }

    if (mExportContext->converterContext.IsCachingEnabled() && mExportContext->converterContext.GetImportAssetDigest().size() > 0)
    {
        if (status == OmniConverterStatus::OK)
        {
            // Release all layers to avoid taking up the file descriptors to influence rename
            mLayerHolders.clear();
            if (!PathUtils::Rename(tempExportRoot, realExportRoot))
            {
                Log("ERROR: Failed to rename " + tempExportRoot + " to " + realExportRoot + ". Removing...");
                status = OmniConverterStatus::FILE_WRITE_ERROR;
            }
            else
            {
                auto outputLayer = PXR_NS::SdfLayer::Find(outputPath);
                auto cachedLayer = PXR_NS::SdfLayer::FindOrOpen(realMainUsdPath);
                auto rootPrimSpec = GetOrCreatePrimSpec(outputLayer, mCommonRootPath);
                outputLayer->SetDefaultPrim(mCommonRootPath.GetNameToken());
                Usd_InsertListItem(rootPrimSpec->GetReferenceList(), PXR_NS::SdfReference(realMainUsdPath));

                PXR_NS::UsdUtilsCopyLayerMetadata(cachedLayer, outputLayer);
                outputLayer->SetField(PXR_NS::SdfPath::AbsoluteRootPath(), PXR_NS::UsdGeomTokens->metersPerUnit, PXR_NS::VtValue(stage->worldUnits));
                if (stage->yAxis)
                {
                    outputLayer->SetField(PXR_NS::SdfPath::AbsoluteRootPath(), PXR_NS::UsdGeomTokens->upAxis, PXR_NS::UsdGeomTokens->y);
                }
                else
                {
                    outputLayer->SetField(PXR_NS::SdfPath::AbsoluteRootPath(), PXR_NS::UsdGeomTokens->upAxis, PXR_NS::UsdGeomTokens->z);
                }
            }
        }

        if (status != OmniConverterStatus::OK)
        {
            mLayerHolders.clear();
            if (!PathUtils::RemoveAll(tempExportRoot))
            {
                Log("ERROR: Failed to remove temp path " + tempExportRoot + ".");
            }
        }
    }

    return status;
}

OmniConverterStatus UsdExporter::WriteLayerTo(PXR_NS::SdfLayerRefPtr layer, const std::string& path, std::string& detailedError)
{
    if (!PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(path))
    {
        auto basePath = PathUtils::GetDirName(path);
        if (!mExportContext->converterContext.IsPathExisted(basePath) && !mExportContext->converterContext.MakeDirectories(basePath))
        {
            detailedError = "Couldn't create directory: " + basePath;
            Log(detailedError);
            return OmniConverterStatus::FILE_WRITE_ERROR;
        }
    }

    bool success = mExportContext->converterContext.WriteUsdLayer(path, layer->GetIdentifier());
    if (success)
    {
        return OmniConverterStatus::OK;
    }
    else
    {
        detailedError = "Failed to write layer: " + path;
        Log(detailedError);
        return OmniConverterStatus::FILE_WRITE_ERROR;
    }
}

PXR_NS::SdfLayerRefPtr UsdExporter::GetOrCreateLayer(const std::string& realStagePath, bool yAxis, double unit)
{
    PXR_NS::SdfLayerRefPtr targetLayer;

    auto iter = mLayerHolders.find(realStagePath);
    if (iter == mLayerHolders.end())
    {
        targetLayer = PXR_NS::SdfLayer::FindOrOpen(realStagePath);
        if (!targetLayer)
        {
            auto format = PXR_NS::SdfFileFormat::FindByExtension(realStagePath);
            targetLayer = PXR_NS::SdfLayer::New(format, realStagePath);
        }
        else
        {
            targetLayer->Clear();
        }

        if (!targetLayer)
        {
            return nullptr;
        }

        mLayerHolders[realStagePath] = targetLayer;
    }
    else
    {
        targetLayer = iter->second;
    }

    if (!targetLayer->HasField(PXR_NS::SdfPath::AbsoluteRootPath(), PXR_NS::UsdGeomTokens->metersPerUnit))
    {
        targetLayer->SetField(PXR_NS::SdfPath::AbsoluteRootPath(), PXR_NS::UsdGeomTokens->metersPerUnit, PXR_NS::VtValue(unit));
    }

    if (!targetLayer->HasField(PXR_NS::SdfPath::AbsoluteRootPath(), PXR_NS::UsdGeomTokens->upAxis))
    {
        if (yAxis)
        {
            targetLayer->SetField(PXR_NS::SdfPath::AbsoluteRootPath(), PXR_NS::UsdGeomTokens->upAxis, PXR_NS::UsdGeomTokens->y);
        }
        else
        {
            targetLayer->SetField(PXR_NS::SdfPath::AbsoluteRootPath(), PXR_NS::UsdGeomTokens->upAxis, PXR_NS::UsdGeomTokens->z);
        }
    }

    auto rootPrimSpec = GetOrCreatePrimSpec(targetLayer, mCommonRootPath, PXR_NS::_tokens->xform);

    // Sets kind for root node.
    if (!rootPrimSpec->HasField(PXR_NS::SdfFieldKeys->Kind))
    {
        rootPrimSpec->SetField(PXR_NS::SdfFieldKeys->Kind, PXR_NS::KindTokens->component);
    }

    if (!targetLayer->HasDefaultPrim())
    {
        targetLayer->SetDefaultPrim(mCommonRootPath.GetNameToken());
    }

    // Creates scope prim beforehand to make sure material prims are on the top of the prim tree.
    if (!mExportContext->converterContext.IgnoreMaterials() && mHasMaterialsToExport)
    {
        GetOrCreatePrimSpec(targetLayer, mCommonMaterialGroupPath, PXR_NS::_tokens->scope);
    }

    return targetLayer;
}

OmniConverterStatus UsdExporter::UploadTextureIfNotEmpty(const TextureImagePtr& texture, std::string& detailedError)
{
    if (!texture)
    {
        return OmniConverterStatus::OK;
    }

    if (texture->blob)
    {
        const std::string& filename = PathUtils::GetFileName(texture->originalPath, true);
        std::string targetPath = PathUtils::JoinPaths(mTexturesExportRoot, filename);
        if (PathUtils::GetExtension(targetPath).empty())
        {
            targetPath += ".png"; // Assumes png by default
        }

        // It's possible that the same texture has been included twice inside original asset.
        if (mUploadedFiles.find(targetPath) == mUploadedFiles.end())
        {
            UploadContent(targetPath, texture->blob.get());
            mUploadedFiles[targetPath] = targetPath;
        }
        mTextureUploadPath[texture] = targetPath;
        mExportContext->IncrementProgress();
    }
    else
    {
        UploadFileInternal(texture->realPath, mTexturesExportRoot, detailedError);
        mTextureUploadPath[texture] = mUploadedFiles[texture->realPath];
    }

    return OmniConverterStatus::OK;
}

OmniConverterStatus UsdExporter::UploadFileInternal(const std::string& filePath, const std::string& targetDir, std::string& detailedError)
{
    auto iter = mUploadedFiles.find(filePath);
    if (iter == mUploadedFiles.end())
    {
        const std::string& fileName = PathUtils::GetFileName(filePath, true);
        const std::string& outFilePath = PathUtils::JoinPaths(targetDir, fileName);
        mUploadedFiles[filePath] = outFilePath;
        if (!mExportContext->converterContext.CopyFile(outFilePath, filePath))
        {
            detailedError = "Failed to copy file " + filePath + " to " + outFilePath + ".";
            Log(detailedError);
            return OmniConverterStatus::CANCELLED;
        }
    }
    mExportContext->IncrementProgress();

    return OmniConverterStatus::OK;
}

void UsdExporter::PreprocessStage(const StagePtr& stage)
{
    // If the root node is skeleton root, creating a xform as root for structure needs.
    const auto& stageNodeInfo = mStageNodeInfos[stage->rootNode];
    if (!stageNodeInfo.hasProps && stage->rootNode->isBoneNode)
    {
        auto worldNode = std::make_shared<StageNode>("Root", stage->rootNode->useTES);
        worldNode->children.push_back(stage->rootNode);
        stage->rootNode = worldNode;
    }

    // Adding top root node to match Kit's requirement.
    bool createWorldAsDefaultRoot = mExportContext->converterContext.CreateWorldAsDefaultRootPrim();
    if (createWorldAsDefaultRoot)
    {
        stage->rootNode->name = "World";
    }

    PreprocessAllNodes(stage);

    // For animation track, it uses animation name as root.
    if (stage->meshes.size() == 0 && stage->animationTracks.size() > 0 && !mStageNodeInfos[stage->rootNode].hasSkeleton)
    {
        const auto& animationTrack = stage->animationTracks[0];
        const std::string& animationName = MakeValidUSDIdentifier(animationTrack.name, "animation");
        mCommonRootPath = PXR_NS::SdfPath::AbsoluteRootPath().AppendElementString(animationName);
    }
    else
    {
        mCommonRootPath = PXR_NS::SdfPath::AbsoluteRootPath().AppendElementString(MakeValidUSDIdentifier(stage->rootNode->name, "node"));
    }
    mCommonMaterialGroupPath = mCommonRootPath.AppendElementString(MATERIAL_GROUP_PRIM_NAME);

    // Checks those materials that are not referenced by any meshes if
    // it does not need to export them.
    if (!mExportContext->converterContext.KeepAllMaterials())
    {
        mShouldExportMaterial.resize(stage->materials.size(), false);
        for (const auto& mesh : stage->meshes)
        {
            for (const auto& geomSubSet : mesh->meshSubsets)
            {
                if (geomSubSet.materialIndex != INVALID_MATERIAL_INDEX)
                {
                    mHasMaterialsToExport = true;
                    mShouldExportMaterial[geomSubSet.materialIndex] = true;
                }
            }
        }
    }
    else if (stage->materials.size() > 0)
    {
        mHasMaterialsToExport = true;
        mShouldExportMaterial.resize(stage->materials.size(), true);
    }
}

PXR_NS::SdfPrimSpecHandle UsdExporter::BindOrDefineMesh(
    const StagePtr& stage,
    const MeshPtr& mesh,
    PXR_NS::SdfLayerRefPtr usdLayer,
    const PXR_NS::SdfPath& instancePath,
    bool instanceable,
    const std::string& instanceDisplayName
)
{
    // ASSIMP mesh data may contain point clouds (face vertex data is empty)
    bool isPointCloud = !mesh->points.empty() && mesh->faceVertexCounts.size() == 0;
    auto& meshInfo = mMeshPrimInfos[mesh];
    if (meshInfo.meshInstanceCount > 1)
    {
        // Define instance
        PXR_NS::SdfPath instanceMeshPrimPath;
        auto instancePrimSpec = GetOrCreatePrimSpec(usdLayer, instancePath, PXR_NS::TfToken(), true, instanceDisplayName);
        if (mExportContext->converterContext.SingleMesh())
        {
            if (meshInfo.prototypePrimPath.IsEmpty())
            {
                if (isPointCloud)
                {
                    auto usdPointCloud = ExportPointCloud(usdLayer, stage, mesh, instancePath, instanceDisplayName);
                    usdPointCloud->SetTypeName(PXR_NS::_tokens->Points);
                    meshInfo.prototypePrimPath = usdPointCloud->GetPath();
                    return usdPointCloud;
                }
                else
                {
                    auto usdMesh = ExportMeshInternal(usdLayer, stage, mesh, instancePath, instanceDisplayName);
                    usdMesh->SetTypeName(PXR_NS::_tokens->mesh);
                    meshInfo.prototypePrimPath = usdMesh->GetPath();
                    return usdMesh;
                }
            }
            else
            {
                Usd_InsertListItem(instancePrimSpec->GetReferenceList(), PXR_NS::SdfReference("", meshInfo.prototypePrimPath));
                if (isPointCloud)
                {
                    instancePrimSpec->SetTypeName(PXR_NS::_tokens->Points);
                }
                else
                {
                    // Bind materials since the original bound is out of the name space of this instance.
                    instancePrimSpec->SetTypeName(PXR_NS::_tokens->mesh);
                    if (mesh->meshSubsets.size() > 1)
                    {
                        for (size_t i = 0; i < mesh->meshSubsets.size(); i++)
                        {
                            const auto& subset = mesh->meshSubsets[i];
                            auto subsetPath = meshInfo.subsets[i].ReplacePrefix(meshInfo.meshPrimPath, instancePath);
                            auto subsetPrim = GetOrCreatePrimSpec(usdLayer, subsetPath, {}, false, instanceDisplayName);
                            BindMaterialToPrimFromSubset(stage, subset, subsetPrim, mCommonMaterialGroupPath);
                        }
                    }
                    else if (mesh->meshSubsets.size() == 1)
                    {
                        const auto& subset = mesh->meshSubsets[0];
                        BindMaterialToPrimFromSubset(stage, subset, instancePrimSpec, mCommonMaterialGroupPath);
                    }
                }
            }
        }
        else
        {
            AddExternalReference(instancePrimSpec, meshInfo.layerPath);
            instanceMeshPrimPath = meshInfo.meshPrimPath.ReplacePrefix(mCommonRootPath, instancePath);
            instancePrimSpec->SetInstanceable(instanceable);
        }

        return instancePrimSpec;
    }
    else
    {
        if (isPointCloud)
        {
            return ExportPointCloud(usdLayer, stage, mesh, instancePath, instanceDisplayName);
        }
        else
        {
            return ExportMeshInternal(usdLayer, stage, mesh, instancePath, instanceDisplayName);
        }
    }
}

OmniConverterStatus UsdExporter::ExportTextures(const StagePtr& stage, std::string& detailedError)
{
    if (mExportContext->converterContext.IgnoreMaterials())
    {
        return OmniConverterStatus::OK;
    }

    Log("Starting to export textures...");

    static auto ClearInMemoryTexture = [](TextureImagePtr& texture)
    {
        if (texture)
        {
            texture->blob = nullptr;
        }
    };

    std::unordered_set<size_t> toUploadTextureImages;
    for (size_t i = 0; i < stage->materials.size(); i++)
    {
        mExportContext->IncrementProgress();

        if (!mShouldExportMaterial[i])
        {
            continue;
        }

        const auto& material = stage->materials[i];
        if (!material->fallback)
        {
            if (material->materialPath.empty())
            {
                Log("Skips handling material " + material->name + " since its path is empty.");
            }
            else
            {
                if (!material->builtIn)
                {
                    UploadFileInternal(material->materialPath, mMaterialsExportRoot, detailedError);
                }

                for (const auto& property : material->inputProperties)
                {
                    if (mExportContext->IsExited())
                    {
                        return OmniConverterStatus::CANCELLED;
                    }

                    if (property.isTextureProperty)
                    {
                        UploadFileInternal(property.stringValue, mTexturesExportRoot, detailedError);
                    }
                }
            }
        }
        else
        {

            for (size_t j = (size_t)MaterialTextureType::START; j < (size_t)MaterialTextureType::END; j++)
            {
                const auto& textureReference = material->GetTextureReference((MaterialTextureType)j);
                if (textureReference.IsValid())
                {
                    toUploadTextureImages.insert(textureReference.imageIndex);
                }
            }
        }
    }


    for (size_t index : toUploadTextureImages)
    {
        if (mExportContext->IsExited())
        {
            return OmniConverterStatus::CANCELLED;
        }

        Log("Exporting texture " + std::to_string(index));
        TextureImagePtr texture = stage->images[index];
        UploadTextureIfNotEmpty(texture, detailedError);
        ClearInMemoryTexture(texture);
        mExportContext->IncrementProgress();
    }

    return OmniConverterStatus::OK;
}

OmniConverterStatus UsdExporter::ExportInstancedMeshes(const StagePtr& stage, std::string& detailedError)
{
    Log("Starting to export meshes...");
    std::unordered_map<std::string, size_t> uniqueMeshFileNames;
    for (size_t i = 0; i < stage->meshes.size(); i++)
    {
        mExportContext->IncrementProgress();

        if (mExportContext->IsExited())
        {
            return OmniConverterStatus::CANCELLED;
        }

        const auto& mesh = stage->meshes[i];
        // Only mesh that has at least 2 instances will be exported as separate prop layer.
        if (mMeshPrimInfos[mesh].meshInstanceCount <= 1)
        {
            continue;
        }

        NameInfo meshNameInfo = GetNameInfo(mesh->name, "mesh", uniqueMeshFileNames);

        // Layer path for separate mesh export
        std::string layerPath = PathUtils::JoinPaths(mPropsExportRoot, meshNameInfo.name + USD_FILE_EXT);
        PXR_NS::SdfLayerRefPtr usdLayer = GetOrCreateLayer(layerPath, stage->yAxis, stage->worldUnits);
        auto meshPath = mCommonRootPath.AppendElementString(meshNameInfo.name);
        ExportMeshInternal(usdLayer, stage, mesh, meshPath, meshNameInfo.displayName);
        ENSURE_STATUS_OK(WriteLayerTo(usdLayer, layerPath, detailedError));
    }

    return OmniConverterStatus::OK;
}

OmniConverterStatus UsdExporter::ExportStageTree(const StagePtr& stage, std::string& detailedError)
{
    if (mExportContext->converterContext.IgnoreAnimations() || stage->animationTracks.size() <= 1)
    {
        mPropsFilePath = mMainUSDExportPath;
    }
    else
    {
        const std::string& stageName = mExportContext->converterContext.GetImportAssetFileName();
        mPropsFilePath = PathUtils::JoinPaths(mPropsExportRoot, stageName + "_props" + USD_FILE_EXT);
    }

    auto usdLayer = GetOrCreateLayer(mPropsFilePath, stage->yAxis, stage->worldUnits);
    if (!usdLayer)
    {
        return OmniConverterStatus::FILE_READ_ERROR;
    }

    if (!mExportContext->converterContext.SingleMesh())
    {
        // Instanced meshes are those that has instances that's over 2, which will be
        // exported to separate layer if it supports to export instanced meshes to separate files.
        ENSURE_STATUS_OK(ExportInstancedMeshes(stage, detailedError));
    }

    std::unordered_map<std::string, size_t> nodeInstanceNameCount;
    ENSURE_STATUS_OK(TraverseAndExport(stage, stage->rootNode, usdLayer, PXR_NS::SdfPath::AbsoluteRootPath(), nodeInstanceNameCount));

    return WriteLayerTo(usdLayer, mPropsFilePath, detailedError);
}

OmniConverterStatus UsdExporter::ExportAnimationClip(const StagePtr& stage, std::string& detailedError)
{
    const auto& animationTrack = stage->animationTracks[0];
    auto usdLayer = GetOrCreateLayer(mExportContext->converterContext.GetOutputAssetPath(), stage->yAxis, stage->worldUnits);
    auto animation = GetOrCreatePrimSpec(usdLayer, mCommonRootPath, PXR_NS::_tokens->skelAnimation);

    StageNodePtr rootBone;
    // Finds the skeleton
    StageUtils::TraverseStageTree(
        stage->rootNode,
        [&rootBone](const StageNodePtr& stageNode)
        {
            if (!rootBone && stageNode->isBoneNode)
            {
                rootBone = stageNode;
            }

            return true;
        }
    );

    PXR_NS::VtTokenArray joints;
    size_t maxTranslationFrames = 0;
    size_t maxScalingFrames = 0;
    size_t maxOrientsFrames = 0;

    // Get stats to avoid exporting redudant frames
    StageUtils::TraverseStageTree(
        rootBone,
        [&maxTranslationFrames, &maxScalingFrames, &maxOrientsFrames](const StageNodePtr& bone)
        {
            if (!bone->isBoneNode)
            {
                return false;
            }

            if (bone->transformAnimationTracks.size() > 0)
            {
                const auto& transformSamples = bone->transformAnimationTracks[0];
                maxTranslationFrames = std::max(transformSamples.GetTranslationSamples().size(), maxTranslationFrames);
                maxScalingFrames = std::max(transformSamples.GetScaleSamples().size(), maxScalingFrames);
                maxOrientsFrames = std::max(transformSamples.GetOrientSamples().size(), maxOrientsFrames);
            }

            return true;
        }
    );

    if (maxTranslationFrames > 0 || maxScalingFrames > 0 || maxOrientsFrames > 0)
    {
        // FIXME: If any of TRS is animated, the others must have at least one frame data.
        maxTranslationFrames = std::max(maxTranslationFrames, (size_t)1);
        maxScalingFrames = std::max(maxScalingFrames, (size_t)1);
        maxOrientsFrames = std::max(maxOrientsFrames, (size_t)1);
    }

    std::vector<PXR_NS::VtVec3dArray> allBoneTranslationSamples(maxTranslationFrames);
    std::vector<PXR_NS::VtVec3dArray> allBoneScaleSamples(maxScalingFrames);
    std::vector<PXR_NS::VtQuatdArray> allBoneRotationSamples(maxOrientsFrames);

    StageUtils::TraverseStageTree(
        rootBone,
        [&](const StageNodePtr& bone)
        {
            if (!bone->isBoneNode)
            {
                return false;
            }

            joints.push_back(PXR_NS::TfToken(mStageNodeInfos[bone].jointName));

            auto boneRestTQS = MathUtils::GfMatrixToTQS(bone->restTransform);
            if (bone->useOrderForAnimation)
            {
                boneRestTQS = MathUtils::GfMatrixToTQS(bone->orderTransform);
            }
            PXR_NS::VtVec3dArray translations;
            PXR_NS::VtVec3dArray scalings;
            PXR_NS::VtQuatdArray orients;
            if (bone->transformAnimationTracks.size() > 0)
            {
                const auto& transformTimesamples = bone->transformAnimationTracks[0];
                translations = transformTimesamples.GetTranslationSamples();
                scalings = transformTimesamples.GetScaleSamples();
                orients = transformTimesamples.GetOrientSamples();
            }

            if (translations.size() > 0)
            {
                for (size_t j = 0; j < translations.size(); j++)
                {
                    allBoneTranslationSamples[j].push_back(translations[j]);
                }

                for (size_t j = translations.size(); j < maxTranslationFrames; j++)
                {
                    allBoneTranslationSamples[j].push_back(translations.back());
                }
            }
            else
            {
                for (size_t j = 0; j < maxTranslationFrames; j++)
                {
                    allBoneTranslationSamples[j].push_back(boneRestTQS.t);
                }
            }

            if (scalings.size() > 0)
            {
                for (size_t j = 0; j < scalings.size(); j++)
                {
                    allBoneScaleSamples[j].push_back(scalings[j]);
                }

                for (size_t j = scalings.size(); j < maxScalingFrames; j++)
                {
                    allBoneScaleSamples[j].push_back(scalings.back());
                }
            }
            else
            {
                for (size_t j = 0; j < maxScalingFrames; j++)
                {
                    allBoneScaleSamples[j].push_back(boneRestTQS.s);
                }
            }

            if (orients.size() > 0)
            {
                for (size_t j = 0; j < orients.size(); j++)
                {
                    allBoneRotationSamples[j].push_back(orients[j]);
                }

                for (size_t j = orients.size(); j < maxOrientsFrames; j++)
                {
                    allBoneRotationSamples[j].push_back(orients.back());
                }
            }
            else
            {
                for (size_t j = 0; j < maxOrientsFrames; j++)
                {
                    allBoneRotationSamples[j].push_back(boneRestTQS.q);
                }
            }

            return true;
        }
    );

    auto jointsAttrSpec = CreateAndSetAttrSpec(
        animation,
        PXR_NS::UsdSkelTokens->joints,
        PXR_NS::SdfValueTypeNames->TokenArray,
        PXR_NS::VtValue(joints),
        PXR_NS::SdfVariabilityUniform
    );

    auto scalesAttrSpec = GetOrNewSdfAttributeSpec(
        animation,
        PXR_NS::UsdSkelTokens->scales,
        PXR_NS::SdfValueTypeNames->Half3Array,
        PXR_NS::SdfVariabilityVarying
    );
    for (size_t i = 0; i < maxScalingFrames; i++)
    {
        PXR_NS::VtVec3hArray s;
        const auto& scaleSamples = allBoneScaleSamples[i];
        s.reserve(scaleSamples.size());
        for (const auto& scale : scaleSamples)
        {
            s.push_back(PXR_NS::GfVec3h(scale));
        }
        usdLayer->SetTimeSample(scalesAttrSpec->GetPath(), static_cast<double>(i / stage->mutiplier), PXR_NS::VtValue(s));
    }

    auto rotationsAttrSpec = GetOrNewSdfAttributeSpec(
        animation,
        PXR_NS::UsdSkelTokens->rotations,
        PXR_NS::SdfValueTypeNames->QuatfArray,
        PXR_NS::SdfVariabilityVarying
    );
    for (size_t i = 0; i < maxOrientsFrames; i++)
    {
        const auto& orientsSamples = allBoneRotationSamples[i];
        PXR_NS::VtQuatfArray q;
        q.reserve(orientsSamples.size());
        for (const auto& orient : orientsSamples)
        {
            q.push_back(PXR_NS::GfQuatf(orient));
        }
        usdLayer->SetTimeSample(rotationsAttrSpec->GetPath(), static_cast<double>(i / stage->mutiplier), PXR_NS::VtValue(q));
    }

    auto translationsAttrSpec = GetOrNewSdfAttributeSpec(
        animation,
        PXR_NS::UsdSkelTokens->translations,
        PXR_NS::SdfValueTypeNames->Float3Array,
        PXR_NS::SdfVariabilityVarying
    );
    for (size_t i = 0; i < maxTranslationFrames; i++)
    {
        PXR_NS::VtVec3fArray t;
        const auto& translationSamples = allBoneTranslationSamples[i];
        t.reserve(translationSamples.size());
        for (const auto& translation : translationSamples)
        {
            t.push_back(PXR_NS::GfVec3f(translation));
        }
        usdLayer->SetTimeSample(translationsAttrSpec->GetPath(), static_cast<double>(i / stage->mutiplier), PXR_NS::VtValue(t));
    }

    return WriteLayerTo(usdLayer, mExportContext->converterContext.GetOutputAssetPath(), detailedError);
}

OmniConverterStatus UsdExporter::TraverseAndExport(
    const StagePtr& stage,
    const StageNodePtr& stageNode,
    PXR_NS::SdfLayerRefPtr usdLayer,
    const PXR_NS::SdfPath& parentPrimPath,
    std::unordered_map<std::string, size_t>& uniqueNameCount
)
{
    // Skip empty node
    auto& stageNodeInfo = mStageNodeInfos[stageNode];
    if (!stageNodeInfo.hasProps && !stageNodeInfo.hasSkeleton)
    {
        return OmniConverterStatus::OK;
    }

    PXR_NS::SdfPath nodePrimPath;
    std::string displayName;
    if (stageNode == stage->rootNode)
    {
        nodePrimPath = mCommonRootPath;
    }
    else
    {
        auto stageNodeNameInfo = GetNameInfo(stageNode->name, "node", uniqueNameCount);
        nodePrimPath = parentPrimPath.AppendElementString(stageNodeNameInfo.name);
        displayName = stageNodeNameInfo.displayName;
    }
    stageNodeInfo.nodePrimPath = nodePrimPath;

    bool useTESOps = stageNode->useTES;
    bool useDoublePrecisionOps = mExportContext->converterContext.UseDoublePrecisionForUSDTransformOp();

    // Creates prim if the node or its children have props and this node is not skeleton node
    // since skeleton will be created as separated node below to avoid transform issue.
    if (stageNodeInfo.hasProps || !stageNode->isBoneNode)
    {
        auto xformPrimSpec = GetOrCreatePrimSpec(usdLayer, nodePrimPath, PXR_NS::_tokens->xform, true, displayName);
        SetDefaultTransform(xformPrimSpec, stageNode->localTransform, useDoublePrecisionOps, useTESOps);
    }

    std::unordered_map<std::string, size_t> uniqueInstanceNameCount;

    // Export mesh instances
    stageNodeInfo.meshInstancePaths.resize(stageNode->staticMeshInstances.size());
    for (size_t i = 0; i < stageNode->staticMeshInstances.size(); i++)
    {
        size_t meshIndex = stageNode->staticMeshInstances[i];
        auto mesh = stage->meshes[meshIndex];
        const auto& meshPrimInfo = mMeshPrimInfos[mesh];
        PXR_NS::SdfPath instancePath;
        std::string primNamePrefix = (mesh->faceVertexCounts.size() == 0) ? "point_cloud" : "mesh";
        NameInfo instanceNameInfo = GetNameInfo(mesh->name, primNamePrefix, uniqueInstanceNameCount);
        instancePath = nodePrimPath.AppendElementString(instanceNameInfo.name);
        BindOrDefineMesh(stage, mesh, usdLayer, instancePath, !mExportContext->converterContext.DisableInstancing(), instanceNameInfo.displayName);
        stageNodeInfo.meshInstancePaths[i] = instancePath;
    }

    // Export lights
    if (!mExportContext->converterContext.IgnoreLights())
    {
        PXR_NS::TfToken radiusToken = PXR_NS::UsdLuxTokens->inputsRadius;
        PXR_NS::TfToken angleToken = PXR_NS::UsdLuxTokens->inputsAngle;
        PXR_NS::TfToken widthToken = PXR_NS::UsdLuxTokens->inputsWidth;
        PXR_NS::TfToken heightToken = PXR_NS::UsdLuxTokens->inputsHeight;
        PXR_NS::TfToken intensityToken = PXR_NS::UsdLuxTokens->inputsIntensity;
        PXR_NS::TfToken colorToken = PXR_NS::UsdLuxTokens->inputsColor;

        for (const auto& lightIndex : stageNode->lights)
        {
            auto& light = stage->lights[lightIndex];
            float intensity = 0.0f;
            PXR_NS::SdfPrimSpecHandle lightPrimSpec;
            NameInfo lightNameInfo = GetNameInfo(light->name, "light", uniqueInstanceNameCount);
            auto lightPrimPath = nodePrimPath.AppendElementString(lightNameInfo.name);
            switch (light->type)
            {
                case LightType::POINT:
                {
                    lightPrimSpec = GetOrCreatePrimSpec(usdLayer, lightPrimPath, PXR_NS::_tokens->sphereLight, true, lightNameInfo.displayName);
                    auto treatAsPointAttrSpec = CreateAndSetAttrSpec(
                        lightPrimSpec,
                        PXR_NS::UsdLuxTokens->treatAsPoint,
                        PXR_NS::SdfValueTypeNames->Bool,
                        PXR_NS::VtValue(true)
                    );
                    auto radiusAttrSpec = CreateAndSetAttrSpec(lightPrimSpec, radiusToken, PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(5.0f));
                    intensity = 30000.0f;
                    break;
                }
                case LightType::SPHERE:
                {
                    lightPrimSpec = GetOrCreatePrimSpec(usdLayer, lightPrimPath, PXR_NS::_tokens->sphereLight, true, lightNameInfo.displayName);
                    auto radiusAttrSpec = CreateAndSetAttrSpec(lightPrimSpec, radiusToken, PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(1.0f));
                    intensity = 30000.0f;
                    break;
                }
                case LightType::DISTANT:
                {
                    lightPrimSpec = GetOrCreatePrimSpec(usdLayer, lightPrimPath, PXR_NS::_tokens->distantLight, true, lightNameInfo.displayName);
                    auto angleAttrSpec = CreateAndSetAttrSpec(lightPrimSpec, angleToken, PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(1.0f));
                    intensity = 3000.0f;
                    break;
                }
                case LightType::RECT:
                {
                    lightPrimSpec = GetOrCreatePrimSpec(usdLayer, lightPrimPath, PXR_NS::_tokens->rectLight, true, lightNameInfo.displayName);
                    auto widthAttrSpec = CreateAndSetAttrSpec(lightPrimSpec, widthToken, PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(100.0f));
                    auto heightAttrSpec = CreateAndSetAttrSpec(lightPrimSpec, heightToken, PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(100.0f));
                    intensity = 15000.0f;
                    break;
                }
                default:
                    break;
            }

            auto intensityAttrSpec = CreateAndSetAttrSpec(lightPrimSpec, intensityToken, PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(intensity));
            auto colorAttrSpec = CreateAndSetAttrSpec(lightPrimSpec, colorToken, PXR_NS::SdfValueTypeNames->Color3f, PXR_NS::VtValue(light->color));
        }
    }

    // Export cameras
    if (!mExportContext->converterContext.IgnoreCameras())
    {
        for (size_t cameraIndex : stageNode->cameras)
        {
            const auto& camera = stage->cameras[cameraIndex];
            const auto& cameraNameInfo = GetNameInfo(camera->name, "camera", uniqueInstanceNameCount);
            auto cameraPrimPath = nodePrimPath.AppendElementString(cameraNameInfo.name);
            stageNodeInfo.cameraPaths.push_back(cameraPrimPath);

            auto usdCamera = GetOrCreatePrimSpec(usdLayer, cameraPrimPath, PXR_NS::_tokens->camera, true, cameraNameInfo.displayName);

            PXR_NS::GfMatrix4d viewMatrix;
            viewMatrix.SetLookAt(camera->position, camera->lookAt, camera->up);
            auto camMatrix = viewMatrix.GetInverse() * stageNode->worldTransformMatrix.GetInverse();
            const auto& cameraLocalTransform = Transform(camMatrix.GetOrthonormalized());

            SetDefaultTransform(usdCamera, cameraLocalTransform, useDoublePrecisionOps, useTESOps);

            auto projectionAttrSpec = CreateAndSetAttrSpec(
                usdCamera,
                PXR_NS::UsdGeomTokens->projection,
                PXR_NS::SdfValueTypeNames->Token,
                PXR_NS::VtValue((ProjectionToToken(camera->projectionType)))
            );

            auto clippingRangeAttrSpec = CreateAndSetAttrSpec(
                usdCamera,
                PXR_NS::UsdGeomTokens->clippingRange,
                PXR_NS::SdfValueTypeNames->Float2,
                PXR_NS::VtValue(PXR_NS::GfVec2f(camera->clippingNear, camera->clippingFar))
            );

            auto horizontalApertureAttrSpec = CreateAndSetAttrSpec(
                usdCamera,
                PXR_NS::UsdGeomTokens->horizontalAperture,
                PXR_NS::SdfValueTypeNames->Float,
                PXR_NS::VtValue(camera->horizonalAperture)
            );

            auto verticalApertureAttrSpec = CreateAndSetAttrSpec(
                usdCamera,
                PXR_NS::UsdGeomTokens->verticalAperture,
                PXR_NS::SdfValueTypeNames->Float,
                PXR_NS::VtValue(camera->verticallAperture)
            );

            auto focalLengthAttrSpec = CreateAndSetAttrSpec(
                usdCamera,
                PXR_NS::UsdGeomTokens->focalLength,
                PXR_NS::SdfValueTypeNames->Float,
                PXR_NS::VtValue(camera->focalLength)
            );
        }
    }

    // Export skeleton
    if (stageNode->IsRootBone())
    {
        std::string skeletonName;
        if (!stageNodeInfo.hasProps)
        {
            skeletonName = stageNode->name;
        }
        else
        {
            skeletonName = stageNode->name + "_skel";
        }

        NameInfo skeletonNameInfo = GetNameInfo(skeletonName, "skeleton", uniqueInstanceNameCount);
        PXR_NS::SdfPath usdSkelRootPath = parentPrimPath.AppendElementString(skeletonNameInfo.name);
        GetOrCreatePrimSpec(usdLayer, usdSkelRootPath, PXR_NS::_tokens->skelRootType, true, skeletonNameInfo.displayName);
        ExportSkeletonAndSkinning(stage, stageNode, usdLayer, usdSkelRootPath);
        stageNodeInfo.nodeSkelRootPath = usdSkelRootPath;
    }

    // Save context
    for (size_t i = 0; i < stageNode->children.size(); i++)
    {
        ENSURE_STATUS_OK(TraverseAndExport(stage, stageNode->children[i], usdLayer, nodePrimPath, uniqueInstanceNameCount));
    }

    return OmniConverterStatus::OK;
}

PXR_NS::SdfPrimSpecHandle UsdExporter::ExportSkeletonAndSkinning(
    const StagePtr& stage,
    const StageNodePtr& rootBone,
    PXR_NS::SdfLayerRefPtr usdLayer,
    const PXR_NS::SdfPath& skeletonRootPath
)
{
    auto skeletonPath = skeletonRootPath.AppendElementString(SKELETON_PRIM_NAME);
    auto usdSkeletonSpec = GetOrCreatePrimSpec(usdLayer, skeletonPath, PXR_NS::_tokens->skeletonType);

    PXR_NS::VtTokenArray joints;
    PXR_NS::VtArray<PXR_NS::GfMatrix4d> bindTransforms;
    PXR_NS::VtArray<PXR_NS::GfMatrix4d> restTransforms;

    StageUtils::TraverseStageTree(
        rootBone,
        [&](const StageNodePtr& bone)
        {
            if (!bone->isBoneNode)
            {
                return false;
            }

            joints.push_back(PXR_NS::TfToken(mStageNodeInfos[bone].jointName));
            if (stage->skinMeshes.size() > 0)
            {
                bindTransforms.push_back(bone->bindTransform);
                restTransforms.push_back(bone->restTransform);
            }

            else
            {
                // if there is no skinmesh, the bindtransform should be identity
                bindTransforms.push_back(PXR_NS::GfMatrix4d().SetIdentity());
                restTransforms.push_back(PXR_NS::GfMatrix4d().SetIdentity());
            }

            return true;
        }
    );

    auto skelJointsSpec = CreateAndSetAttrSpec(
        usdSkeletonSpec,
        PXR_NS::UsdSkelTokens->joints,
        PXR_NS::SdfValueTypeNames->TokenArray,
        PXR_NS::VtValue(joints),
        PXR_NS::SdfVariabilityUniform
    );
    auto skelBindTransformsSpec = CreateAndSetAttrSpec(
        usdSkeletonSpec,
        PXR_NS::UsdSkelTokens->bindTransforms,
        PXR_NS::SdfValueTypeNames->Matrix4dArray,
        PXR_NS::VtValue(bindTransforms),
        PXR_NS::SdfVariabilityUniform
    );
    auto skelRestTransformsSpec = CreateAndSetAttrSpec(
        usdSkeletonSpec,
        PXR_NS::UsdSkelTokens->restTransforms,
        PXR_NS::SdfValueTypeNames->Matrix4dArray,
        PXR_NS::VtValue(restTransforms),
        PXR_NS::SdfVariabilityUniform
    );

    std::unordered_set<MeshPtr> meshes;
    std::unordered_map<std::string, size_t> uniqueSkinMeshNameCount;

    for (const auto& skinMesh : stage->skinMeshes)
    {
        auto mesh = stage->meshes[skinMesh->meshIndex];
        if (rootBone != skinMesh->skeletonRoot || meshes.count(mesh) != 0)
        {
            continue;
        }
        meshes.insert(mesh);

        NameInfo meshNameInfo = GetNameInfo(mesh->name, "mesh", uniqueSkinMeshNameCount);
        const auto& meshPrimPath = skeletonRootPath.AppendElementString(meshNameInfo.name);
        PXR_NS::SdfPrimSpecHandle meshPrim = BindOrDefineMesh(stage, mesh, usdLayer, meshPrimPath, false, meshNameInfo.displayName);
        if (!meshPrim)
        {
            continue;
        }

        auto jointInfluences = skinMesh->jointInfluences;
        auto jointWeights = skinMesh->jointWeights;
        PXR_NS::VtTokenArray meshJoints;
        for (const auto& bone : skinMesh->influencedBoneNodes)
        {
            meshJoints.push_back(PXR_NS::TfToken(mStageNodeInfos[bone].jointName));
        }
        size_t maxInfluences = skinMesh->numBoneInfluencesPerVertex;
        PXR_NS::UsdSkelSortInfluences(&jointInfluences, &jointWeights, maxInfluences);
        PXR_NS::UsdSkelNormalizeWeights(&jointWeights, maxInfluences);

        ApplyBindingAPIToPrimSpec<PXR_NS::UsdSkelBindingAPI>(meshPrim);

        auto relSpec = NewSdfRelationshipSpec(meshPrim, PXR_NS::UsdSkelTokens->skelSkeleton, false, PXR_NS::SdfVariabilityUniform);
        Usd_InsertListItem(relSpec->GetTargetPathList(), usdSkeletonSpec->GetPath(), true);

        auto geomBindTransformAttrSpec = GetOrNewSdfAttributeSpec(
            meshPrim,
            PXR_NS::UsdSkelTokens->primvarsSkelGeomBindTransform,
            PXR_NS::SdfValueTypeNames->Matrix4d
        );
        geomBindTransformAttrSpec->SetDefaultValue(PXR_NS::VtValue(skinMesh->geomBindTransform));

        int elementSize = static_cast<int>(maxInfluences);
        auto jointIndicesAttrSpec = GetOrNewSdfAttributeSpec(
            meshPrim,
            PXR_NS::UsdSkelTokens->primvarsSkelJointIndices,
            PXR_NS::SdfValueTypeNames->IntArray
        );
        jointIndicesAttrSpec->SetDefaultValue(PXR_NS::VtValue(jointInfluences));
        usdLayer->SetField(jointIndicesAttrSpec->GetPath(), PXR_NS::UsdGeomTokens->elementSize, elementSize);
        usdLayer->SetField(jointIndicesAttrSpec->GetPath(), PXR_NS::UsdGeomTokens->interpolation, PXR_NS::UsdGeomTokens->vertex);

        auto jointWeightsAttrSpec = GetOrNewSdfAttributeSpec(
            meshPrim,
            PXR_NS::UsdSkelTokens->primvarsSkelJointWeights,
            PXR_NS::SdfValueTypeNames->FloatArray
        );
        jointWeightsAttrSpec->SetDefaultValue(PXR_NS::VtValue(jointWeights));
        usdLayer->SetField(jointWeightsAttrSpec->GetPath(), PXR_NS::UsdGeomTokens->elementSize, elementSize);
        usdLayer->SetField(jointWeightsAttrSpec->GetPath(), PXR_NS::UsdGeomTokens->interpolation, PXR_NS::UsdGeomTokens->vertex);

        auto jointsAttrSpec = GetOrNewSdfAttributeSpec(
            meshPrim,
            PXR_NS::UsdSkelTokens->skelJoints,
            PXR_NS::SdfValueTypeNames->TokenArray,
            PXR_NS::SdfVariabilityUniform
        );
        jointsAttrSpec->SetDefaultValue(PXR_NS::VtValue(meshJoints));
    }

    return usdSkeletonSpec;
}


PXR_NS::SdfPrimSpecHandle UsdExporter::ExportPointCloud(
    PXR_NS::SdfLayerRefPtr usdLayer,
    const StagePtr& stage,
    const MeshPtr& pointCloud,
    const PXR_NS::SdfPath& pointCloudPath,
    const std::string& pointCloudDisplayName
)
{
    // Note that point cloud is a special case of mesh, where faceVertexCounts and faceVertexIndices are empty
    auto usdPointCloudSpec = usdLayer->GetPrimAtPath(pointCloudPath);
    if (usdPointCloudSpec && usdPointCloudSpec->GetTypeName() == PXR_NS::_tokens->Points)
    {
        return usdPointCloudSpec;
    }

    usdPointCloudSpec = GetOrCreatePrimSpec(usdLayer, pointCloudPath, PXR_NS::_tokens->Points, true, pointCloudDisplayName);
    if (pointCloud->points.empty())
    {
        return usdPointCloudSpec;
    }

    auto pointsAttrSpec = CreateAndSetAttrSpec(
        usdPointCloudSpec,
        PXR_NS::_tokens->points,
        PXR_NS::SdfValueTypeNames->Point3fArray,
        PXR_NS::VtValue(pointCloud->points)
    );

    // Note this value was chosen heuristically. It may need to be modified in the future.
    float pointsWidth = 0.01f;
    PXR_NS::VtFloatArray pointCloudWidths(pointCloud->points.size(), pointsWidth);

    auto widthAttrSpec = CreateAndSetAttrSpec(
        usdPointCloudSpec,
        PXR_NS::UsdGeomTokens->widths,
        PXR_NS::SdfValueTypeNames->FloatArray,
        PXR_NS::VtValue(pointCloudWidths)
    );

    // Vertex color
    if (!pointCloud->colors.empty())
    {
        auto colorAttrSpec = CreateAndSetAttrSpec(
            usdPointCloudSpec,
            PXR_NS::UsdGeomTokens->primvarsDisplayColor,
            PXR_NS::SdfValueTypeNames->Color3fArray,
            PXR_NS::VtValue(pointCloud->colors[0])
        );
        usdLayer->SetField(colorAttrSpec->GetPath(), PXR_NS::UsdGeomTokens->interpolation, PXR_NS::VtValue(PXR_NS::UsdGeomTokens->vertex));
    }

    // Normals
    if (!pointCloud->normals.empty())
    {
        bool skipNormals = AllNormalsAreZero(pointCloud->normals);
        if (!skipNormals)
        {
            auto normalsAttrSpec = CreateAndSetAttrSpec(
                usdPointCloudSpec,
                PXR_NS::UsdGeomTokens->normals,
                PXR_NS::SdfValueTypeNames->Normal3fArray,
                PXR_NS::VtValue(pointCloud->normals)
            );
            usdLayer->SetField(normalsAttrSpec->GetPath(), PXR_NS::UsdGeomTokens->interpolation, PXR_NS::VtValue(PXR_NS::UsdGeomTokens->vertex));
        }
    }

    auto& pointCloudPrimInfo = mMeshPrimInfos[pointCloud];
    {
        pointCloudPrimInfo.layerPath = usdLayer->GetIdentifier();
        pointCloudPrimInfo.meshPrimPath = pointCloudPath;
    }
    return usdPointCloudSpec;
}


PXR_NS::SdfPrimSpecHandle UsdExporter::ExportMeshInternal(
    PXR_NS::SdfLayerRefPtr usdLayer,
    const StagePtr& stage,
    const MeshPtr& mesh,
    const PXR_NS::SdfPath& meshPath,
    const std::string& meshDisplayName
)
{
    auto usdMeshSpec = usdLayer->GetPrimAtPath(meshPath);
    if (usdMeshSpec && usdMeshSpec->GetTypeName() == PXR_NS::_tokens->mesh)
    {
        return usdMeshSpec;
    }

    usdMeshSpec = GetOrCreatePrimSpec(usdLayer, meshPath, PXR_NS::_tokens->mesh, true, meshDisplayName);
    if (mesh->points.empty() || mesh->faceVertexCounts.empty() || mesh->faceVertexIndices.empty())
    {
        return usdMeshSpec;
    }
    auto pointsAttrSpec = CreateAndSetAttrSpec(
        usdMeshSpec,
        PXR_NS::_tokens->points,
        PXR_NS::SdfValueTypeNames->Point3fArray,
        PXR_NS::VtValue(mesh->points)
    );
    for (size_t i = 0; i < mesh->pointCacheTimesamples.size(); i++)
    {
        usdLayer->SetTimeSample(pointsAttrSpec->GetPath(), static_cast<double>(i / stage->mutiplier), PXR_NS::VtValue(mesh->pointCacheTimesamples[i]));
    }

    auto& meshPrimInfo = mMeshPrimInfos[mesh];
    {
        auto faceVertexCountsAttrSpec = CreateAndSetAttrSpec(
            usdMeshSpec,
            PXR_NS::UsdGeomTokens->faceVertexCounts,
            PXR_NS::SdfValueTypeNames->IntArray,
            PXR_NS::VtValue(mesh->faceVertexCounts)
        );

        auto faceVertexIndicesAttrSpec = CreateAndSetAttrSpec(
            usdMeshSpec,
            PXR_NS::UsdGeomTokens->faceVertexIndices,
            PXR_NS::SdfValueTypeNames->IntArray,
            PXR_NS::VtValue(mesh->faceVertexIndices)
        );

        PXR_NS::VtArray<PXR_NS::GfVec3f> Extent;
        PXR_NS::UsdGeomPointBased::ComputeExtent(mesh->points, &Extent);

        auto extentAttrSpec = CreateAndSetAttrSpec(
            usdMeshSpec,
            PXR_NS::UsdGeomTokens->extent,
            PXR_NS::SdfValueTypeNames->Float3Array,
            PXR_NS::VtValue(Extent)
        );

        // Vertex color
        if (!mesh->colors.empty())
        {
            // TODO : set multi colors
            auto colorAttrSpec = CreateAndSetAttrSpec(
                usdMeshSpec,
                PXR_NS::UsdGeomTokens->primvarsDisplayColor,
                PXR_NS::SdfValueTypeNames->Color3fArray,
                PXR_NS::VtValue(mesh->colors[0])
            );
            usdLayer->SetField(colorAttrSpec->GetPath(), PXR_NS::UsdGeomTokens->interpolation, PXR_NS::VtValue(PXR_NS::UsdGeomTokens->faceVarying));
        }

        // Normals
        if (!mesh->normals.empty())
        {
            auto normalsAttrSpec = CreateAndSetAttrSpec(
                usdMeshSpec,
                PXR_NS::UsdGeomTokens->normals,
                PXR_NS::SdfValueTypeNames->Normal3fArray,
                PXR_NS::VtValue(mesh->normals)
            );
            usdLayer->SetField(normalsAttrSpec->GetPath(), PXR_NS::UsdGeomTokens->interpolation, PXR_NS::VtValue(PXR_NS::UsdGeomTokens->faceVarying));
        }

        // Texture UV
        for (size_t i = 0; i < mesh->uvs.size(); i++)
        {
            PXR_NS::TfToken stName;
            if (i == 0)
            {
                stName = PXR_NS::_tokens->st;
            }
            else
            {
                stName = PXR_NS::TfToken("st_" + std::to_string(i));
            }

            auto uvAttrSpec = CreateAndSetAttrSpec(
                usdMeshSpec,
                PXR_NS::TfToken("primvars:" + stName.GetString()),
                PXR_NS::SdfValueTypeNames->TexCoord2fArray,
                PXR_NS::VtValue(mesh->uvs[i])
            );
            usdLayer->SetField(uvAttrSpec->GetPath(), PXR_NS::UsdGeomTokens->interpolation, PXR_NS::VtValue(PXR_NS::UsdGeomTokens->faceVarying));

            if (mesh->uvIndices.size() > i)
            {
                if (mesh->faceVertexIndices.size() > 0)
                {
                    if (mesh->uvIndices[i].size() == mesh->faceVertexIndices.size())
                    {
                        PXR_NS::TfToken indicesAttrName(uvAttrSpec->GetName() + ":indices");
                        auto indicesAttrSpec = CreateAndSetAttrSpec(
                            usdMeshSpec,
                            indicesAttrName,
                            PXR_NS::SdfValueTypeNames->IntArray,
                            PXR_NS::VtValue(mesh->uvIndices[i])
                        );
                    }
                }
            }
        }

        meshPrimInfo.layerPath = usdLayer->GetIdentifier();
        meshPrimInfo.meshPrimPath = meshPath;
        auto subdivisionAttrSpec = CreateAndSetAttrSpec(
            usdMeshSpec,
            PXR_NS::UsdGeomTokens->subdivisionScheme,
            PXR_NS::SdfValueTypeNames->Token,
            PXR_NS::VtValue(PXR_NS::UsdGeomTokens->none),
            PXR_NS::SdfVariabilityUniform
        );
    }

    if (mesh->meshSubsets.size() > 1)
    {
        std::unordered_map<std::string, size_t> uniqueSubsetNames;

        for (size_t i = 0; i < mesh->meshSubsets.size(); i++)
        {
            NameInfo subsetNameInfo = GetNameInfo(mesh->meshSubsets[i].name, "subset", uniqueSubsetNames);

            const auto& subset = mesh->meshSubsets[i];

            // Creates geom subset
            PXR_NS::SdfPrimSpecHandle geomSubsetSpec;
            std::string geomSubsetName = subsetNameInfo.name;
            size_t subsetIdx = 0;
            while (true)
            {
                PXR_NS::SdfPath childPath = meshPath.AppendChild(PXR_NS::TfToken(geomSubsetName));
                geomSubsetSpec = usdLayer->GetPrimAtPath(childPath);
                if (!geomSubsetSpec)
                {
                    geomSubsetSpec = GetOrCreatePrimSpec(usdLayer, childPath, PXR_NS::_tokens->geomSubsetType, true, subsetNameInfo.displayName);
                    break;
                }

                subsetIdx++;
                geomSubsetName = PXR_NS::TfStringPrintf("%s_%zu", subsetNameInfo.name.c_str(), subsetIdx);
            }

            CreateAndSetAttrSpec(
                geomSubsetSpec,
                PXR_NS::UsdGeomTokens->elementType,
                PXR_NS::SdfValueTypeNames->Token,
                PXR_NS::VtValue(PXR_NS::UsdGeomTokens->face)
            );

            CreateAndSetAttrSpec(
                geomSubsetSpec,
                PXR_NS::UsdGeomTokens->indices,
                PXR_NS::SdfValueTypeNames->IntArray,
                PXR_NS::VtValue(subset.faceIndices)
            );

            CreateAndSetAttrSpec(
                geomSubsetSpec,
                PXR_NS::UsdGeomTokens->familyName,
                PXR_NS::SdfValueTypeNames->Token,
                PXR_NS::VtValue(PXR_NS::TfToken("materialBind"))
            );

            meshPrimInfo.subsets.push_back(geomSubsetSpec->GetPath());
            BindMaterialToPrimFromSubset(stage, subset, geomSubsetSpec, mCommonMaterialGroupPath);
        }
    }
    else
    {
        const auto& subset = mesh->meshSubsets[0];
        BindMaterialToPrimFromSubset(stage, subset, usdMeshSpec, mCommonMaterialGroupPath);
    }

    return usdMeshSpec;
}

OmniConverterStatus UsdExporter::ExportAnimations(const StagePtr& stage, std::string& detailedError)
{
    auto mainLayer = GetOrCreateLayer(mMainUSDExportPath, stage->yAxis, stage->worldUnits);
    if (stage->maxKeyFrames > 0)
    {
        mainLayer->SetStartTimeCode(0);
        mainLayer->SetEndTimeCode(stage->maxKeyFrames / stage->mutiplier);
    }

    // TEMP: Enable fractional cutout settings for Kit
    // Otherwise, Kit cannot handle fractional cutout opacity correctly in RTX mode.
    if (mEnableFractionalOpacity)
    {
        auto customLayerData = mainLayer->GetCustomLayerData();
        PXR_NS::VtDictionary customData;
        customData.SetValueAtPath("rtx:raytracing:fractionalCutoutOpacity", PXR_NS::VtValue(true), " ");
        customLayerData.SetValueAtPath("renderSettings", PXR_NS::VtValue(customData));
        mainLayer->SetCustomLayerData(customLayerData);
    }

    // Add all references and set up variants for animation track selection.
    auto defaultPrimSpec = mainLayer->GetPrimAtPath(mCommonRootPath);
    bool hasMultipleTracks = stage->animationTracks.size() > 1;
    if (hasMultipleTracks)
    {
        AddExternalReference(defaultPrimSpec, mPropsFilePath);
        PXR_NS::SdfPath varSetPath = defaultPrimSpec->GetPath().AppendVariantSelection(ANIMATION_TRACK_VARIANT_SET_NAME, std::string());
        PXR_NS::SdfVariantSetSpec::New(defaultPrimSpec, ANIMATION_TRACK_VARIANT_SET_NAME);
        Usd_InsertListItem(defaultPrimSpec->GetVariantSetNameList(), ANIMATION_TRACK_VARIANT_SET_NAME);
    }

    std::vector<std::string> allAnimationTracks;
    std::vector<std::string> allSkeletalAnimationLayers;
    std::unordered_map<std::string, size_t> uniqueAnimationNames;
    for (size_t trackIndex = 0; trackIndex < stage->animationTracks.size(); trackIndex++)
    {
        mExportContext->IncrementProgress();

        const auto& animationTrack = stage->animationTracks[trackIndex];
        // TODO: kit's timeline player actually use 24 fps as default and does not fit well with other values
        // mainLayer->SetTimeCodesPerSecond(animationTrack.fps);

        const std::string& validIdentifier = StringUtils::ToLower(MakeValidUSDIdentifier(animationTrack.name, "animation"));

        PXR_NS::SdfPath rootNodeNamespacePath;
        if (hasMultipleTracks)
        {
            auto variant = AddVariant(defaultPrimSpec, ANIMATION_TRACK_VARIANT_SET_NAME, validIdentifier);
            rootNodeNamespacePath = variant->GetPath();
            allAnimationTracks.push_back(validIdentifier);
        }
        else
        {
            rootNodeNamespacePath = defaultPrimSpec->GetPath();
        }

        // Translate path to variant path if it has multiple animation tracks.
        auto ToEditNamespacePath = [&hasMultipleTracks, &rootNodeNamespacePath](const PXR_NS::SdfPath& path)
        {
            if (hasMultipleTracks)
            {
                return path.ReplacePrefix(rootNodeNamespacePath.GetPrimPath(), rootNodeNamespacePath);
            }
            else
            {
                return path;
            }
        };

        // All the following edits must be translated into path of variant namespace if it's inside variant namespace.
        const std::string& stageName = mExportContext->converterContext.GetImportAssetFileName();
        PXR_NS::SdfLayerRefPtr animationLayer = nullptr;
        StageUtils::TraverseStageTree(
            stage->rootNode,
            [&](const StageNodePtr& stageNode)
            {
                // Skips this node if it or its children are empty nodes.
                const auto& stageNodeInfo = mStageNodeInfos[stageNode];
                if (!stageNodeInfo.hasProps && !stageNodeInfo.hasSkeleton)
                {
                    return false;
                }

                const PXR_NS::SdfPath& editNamespacePrimPath = ToEditNamespacePath(stageNodeInfo.nodePrimPath);
                bool useDoublePrecisionOps = mExportContext->converterContext.UseDoublePrecisionForUSDTransformOp();
                if (!stageNode->isBoneNode && trackIndex < stageNode->transformAnimationTracks.size())
                {
                    const auto& transforms = stageNode->transformAnimationTracks[trackIndex];
                    if (transforms.Size() > 0)
                    {
                        auto xformPrim = GetOrCreatePrimSpec(mainLayer, editNamespacePrimPath, PXR_NS::TfToken(), false);
                        SetXformTransformSamples(xformPrim, transforms, useDoublePrecisionOps, stageNode->useTES, stage);
                    }
                }

                if (!mExportContext->converterContext.IgnoreCameras())
                {
                    // Camera animations will be exported to root USD.
                    for (size_t i = 0; i < stageNode->cameras.size(); i++)
                    {
                        const size_t cameraIndex = stageNode->cameras[i];
                        const auto& camera = stage->cameras[cameraIndex];
                        if (trackIndex < camera->lookAtAnimations.size() && camera->lookAtAnimations[trackIndex].size() > 0)
                        {
                            PXR_NS::VtVec3dArray translations;
                            PXR_NS::VtVec3dArray scales;
                            PXR_NS::VtVec3dArray rotationXYZs;
                            PXR_NS::VtQuatdArray orients;
                            TransformTimesamples transformTimesamples;

                            auto usdCameraSpec = GetOrCreatePrimSpec(mainLayer, stageNodeInfo.cameraPaths[i], PXR_NS::_tokens->camera, false);
                            for (size_t frameIndex = 0; frameIndex < camera->lookAtAnimations[trackIndex].size(); frameIndex++)
                            {
                                PXR_NS::GfVec3d lookat = camera->lookAt;
                                if (camera->lookAtAnimations[trackIndex].size() > 0)
                                {
                                    lookat = camera->lookAtAnimations[trackIndex][frameIndex];
                                }

                                PXR_NS::GfVec3d position = camera->position;
                                if (trackIndex < camera->positionAnimations.size() && camera->positionAnimations[trackIndex].size() > 0)
                                {
                                    position = camera->positionAnimations[trackIndex][frameIndex];
                                }

                                PXR_NS::GfVec3d up = camera->position;
                                if (trackIndex < camera->upAnimations.size() && camera->upAnimations[trackIndex].size() > 0)
                                {
                                    up = camera->upAnimations[trackIndex][frameIndex];
                                }

                                PXR_NS::GfMatrix4d viewMatrix;
                                viewMatrix.SetLookAt(position, lookat, up);
                                const PXR_NS::GfMatrix4d& parentToWorldInverse = stageNode->ComputeLocalToWorldTransform(trackIndex, frameIndex)
                                                                                     .GetInverse();

                                auto camMatrix = viewMatrix.GetInverse() * parentToWorldInverse;
                                if (stageNode->useTES)
                                {
                                    const auto& tes = MathUtils::GfMatrixToTRS(camMatrix);
                                    translations.push_back(tes.t);
                                    scales.push_back(tes.s);
                                    rotationXYZs.push_back(tes.r);
                                }
                                else
                                {
                                    const auto& tqs = MathUtils::GfMatrixToTQS(camMatrix);
                                    translations.push_back(tqs.t);
                                    scales.push_back(tqs.s);
                                    orients.push_back(tqs.q);
                                }
                            }

                            if (stageNode->useTES)
                            {
                                transformTimesamples = TransformTimesamples(translations, rotationXYZs, scales);
                            }
                            else
                            {
                                transformTimesamples = TransformTimesamples(translations, orients, scales);
                            }

                            const PXR_NS::SdfPath& editNamespacePrimPath = ToEditNamespacePath(usdCameraSpec->GetPath());
                            auto cameraPrimSpec = GetOrCreatePrimSpec(mainLayer, editNamespacePrimPath, PXR_NS::TfToken(), false);
                            SetXformTransformSamples(cameraPrimSpec, transformTimesamples, useDoublePrecisionOps, stageNode->useTES, stage);
                        }
                    }
                }

                if (stageNode->IsRootBone())
                {
                    PXR_NS::VtTokenArray joints;
                    size_t maxTranslationFrames = 0;
                    size_t maxScalingFrames = 0;
                    size_t maxOrientsFrames = 0;

                    // Get stats to avoid exporting redudant frames
                    StageUtils::TraverseStageTree(
                        stageNode,
                        [&maxTranslationFrames, &maxScalingFrames, &maxOrientsFrames, trackIndex](const StageNodePtr& bone)
                        {
                            if (!bone->isBoneNode)
                            {
                                return false;
                            }

                            if (trackIndex < bone->transformAnimationTracks.size())
                            {
                                const auto& transformSamples = bone->transformAnimationTracks[trackIndex];
                                maxTranslationFrames = std::max(transformSamples.GetTranslationSamples().size(), maxTranslationFrames);
                                maxScalingFrames = std::max(transformSamples.GetScaleSamples().size(), maxScalingFrames);
                                maxOrientsFrames = std::max(transformSamples.GetOrientSamples().size(), maxOrientsFrames);
                            }

                            return true;
                        }
                    );

                    if (maxTranslationFrames > 0 || maxScalingFrames > 0 || maxOrientsFrames > 0)
                    {
                        // FIXME: If any of TRS is animated, the others must have at least one frame data.
                        maxTranslationFrames = std::max(maxTranslationFrames, (size_t)1);
                        maxScalingFrames = std::max(maxScalingFrames, (size_t)1);
                        maxOrientsFrames = std::max(maxOrientsFrames, (size_t)1);
                    }

                    // Traverse bone tree to populate all transform data.
                    std::vector<PXR_NS::VtVec3dArray> allBoneTranslationSamples(maxTranslationFrames);
                    std::vector<PXR_NS::VtVec3dArray> allBoneScaleSamples(maxScalingFrames);
                    std::vector<PXR_NS::VtQuatdArray> allBoneRotationSamples(maxOrientsFrames);
                    StageUtils::TraverseStageTree(
                        stageNode,
                        [&](const StageNodePtr& bone)
                        {
                            if (!bone->isBoneNode)
                            {
                                return false;
                            }
                            auto name = std::string(bone->name);
                            bool condition = (name.find("R_FINGER") != std::string::npos) && (name.find("537") != std::string::npos);

                            joints.push_back(PXR_NS::TfToken(mStageNodeInfos[bone].jointName));

                            auto boneRestTQS = MathUtils::GfMatrixToTQS(bone->restTransform);
                            if (bone->useOrderForAnimation)
                            {
                                boneRestTQS = MathUtils::GfMatrixToTQS(bone->orderTransform);
                            }
                            PXR_NS::VtVec3dArray translations;
                            PXR_NS::VtVec3dArray scalings;
                            PXR_NS::VtQuatdArray orients;
                            if (trackIndex < bone->transformAnimationTracks.size())
                            {
                                const auto& transformTimesamples = bone->transformAnimationTracks[trackIndex];
                                translations = transformTimesamples.GetTranslationSamples();
                                scalings = transformTimesamples.GetScaleSamples();
                                orients = transformTimesamples.GetOrientSamples();
                            }

                            if (translations.size() > 0)
                            {
                                for (size_t j = 0; j < translations.size(); j++)
                                {
                                    allBoneTranslationSamples[j].push_back(translations[j]);
                                }

                                for (size_t j = translations.size(); j < maxTranslationFrames; j++)
                                {
                                    allBoneTranslationSamples[j].push_back(translations.back());
                                }
                            }
                            else
                            {
                                for (size_t j = 0; j < maxTranslationFrames; j++)
                                {
                                    allBoneTranslationSamples[j].push_back(boneRestTQS.t);
                                }
                            }

                            if (scalings.size() > 0)
                            {
                                for (size_t j = 0; j < scalings.size(); j++)
                                {
                                    allBoneScaleSamples[j].push_back(scalings[j]);
                                }

                                for (size_t j = scalings.size(); j < maxScalingFrames; j++)
                                {
                                    allBoneScaleSamples[j].push_back(scalings.back());
                                }
                            }
                            else
                            {
                                for (size_t j = 0; j < maxScalingFrames; j++)
                                {
                                    allBoneScaleSamples[j].push_back(boneRestTQS.s);
                                }
                            }

                            if (orients.size() > 0)
                            {
                                for (size_t j = 0; j < orients.size(); j++)
                                {
                                    allBoneRotationSamples[j].push_back(orients[j]);
                                }

                                for (size_t j = orients.size(); j < maxOrientsFrames; j++)
                                {
                                    allBoneRotationSamples[j].push_back(orients.back());
                                }
                            }
                            else
                            {
                                for (size_t j = 0; j < maxOrientsFrames; j++)
                                {
                                    allBoneRotationSamples[j].push_back(boneRestTQS.q);
                                }
                            }

                            return true;
                        }
                    );

                    // Authoring animation data into animation layer.
                    if (!animationLayer)
                    {
                        // For each animation track, it will create separate animation data file if it has multiple
                        // animation tracks.
                        if (hasMultipleTracks)
                        {
                            const std::string& animationLayerPath = PathUtils::JoinPaths(
                                mAnimationsExportRoot,
                                stageName + "_" + validIdentifier + "_anim" + USD_FILE_EXT
                            );
                            animationLayer = GetOrCreateLayer(animationLayerPath, stage->yAxis, stage->worldUnits);
                            allSkeletalAnimationLayers.push_back(animationLayerPath);
                        }
                        else
                        {
                            animationLayer = GetOrCreateLayer(mMainUSDExportPath, stage->yAxis, stage->worldUnits);
                        }
                    }

                    const auto& skeletonRootPath = stageNodeInfo.nodeSkelRootPath;
                    NameInfo animationNameInfo = GetNameInfo(animationTrack.name, "animation", uniqueAnimationNames);
                    PXR_NS::SdfPath animationPath = skeletonRootPath.AppendChild(PXR_NS::TfToken(animationNameInfo.name));
                    auto animation = GetOrCreatePrimSpec(
                        animationLayer,
                        animationPath,
                        PXR_NS::_tokens->skelAnimation,
                        true,
                        animationNameInfo.displayName
                    );

                    auto jointsAttrSpec = CreateAndSetAttrSpec(
                        animation,
                        PXR_NS::UsdSkelTokens->joints,
                        PXR_NS::SdfValueTypeNames->TokenArray,
                        PXR_NS::VtValue(joints),
                        PXR_NS::SdfVariabilityUniform
                    );

                    auto scalesAttrSpec = GetOrNewSdfAttributeSpec(
                        animation,
                        PXR_NS::UsdSkelTokens->scales,
                        PXR_NS::SdfValueTypeNames->Half3Array,
                        PXR_NS::SdfVariabilityVarying
                    );
                    for (size_t i = 0; i < maxScalingFrames; i++)
                    {
                        PXR_NS::VtVec3hArray s;
                        const auto& scaleSamples = allBoneScaleSamples[i];
                        s.reserve(scaleSamples.size());
                        for (const auto& scale : scaleSamples)
                        {
                            s.push_back(PXR_NS::GfVec3h(scale));
                        }
                        animationLayer->SetTimeSample(scalesAttrSpec->GetPath(), static_cast<double>(i / stage->mutiplier), PXR_NS::VtValue(s));
                    }

                    auto rotationsAttrSpec = GetOrNewSdfAttributeSpec(
                        animation,
                        PXR_NS::UsdSkelTokens->rotations,
                        PXR_NS::SdfValueTypeNames->QuatfArray,
                        PXR_NS::SdfVariabilityVarying
                    );
                    for (size_t i = 0; i < maxOrientsFrames; i++)
                    {
                        const auto& orientsSamples = allBoneRotationSamples[i];
                        PXR_NS::VtQuatfArray q;
                        q.reserve(orientsSamples.size());
                        for (const auto& orient : orientsSamples)
                        {
                            q.push_back(PXR_NS::GfQuatf(orient));
                        }
                        animationLayer->SetTimeSample(rotationsAttrSpec->GetPath(), static_cast<double>(i / stage->mutiplier), PXR_NS::VtValue(q));
                    }

                    auto translationsAttrSpec = GetOrNewSdfAttributeSpec(
                        animation,
                        PXR_NS::UsdSkelTokens->translations,
                        PXR_NS::SdfValueTypeNames->Float3Array,
                        PXR_NS::SdfVariabilityVarying
                    );
                    for (size_t i = 0; i < maxTranslationFrames; i++)
                    {
                        PXR_NS::VtVec3fArray t;
                        const auto& translationSamples = allBoneTranslationSamples[i];
                        t.reserve(translationSamples.size());
                        for (const auto& translation : translationSamples)
                        {
                            t.push_back(PXR_NS::GfVec3f(translation));
                        }
                        animationLayer->SetTimeSample(translationsAttrSpec->GetPath(), static_cast<double>(i / stage->mutiplier), PXR_NS::VtValue(t));
                    }

                    // Authoring variants to bind different animation source if it has multiple tracks.
                    auto skeletonPrimPath = skeletonRootPath.AppendElementString(SKELETON_PRIM_NAME);
                    skeletonPrimPath = ToEditNamespacePath(skeletonPrimPath);
                    auto skeletonPrimSpec = GetOrCreatePrimSpec(mainLayer, skeletonPrimPath, {}, false);
                    ApplyBindingAPIToPrimSpec<PXR_NS::UsdSkelBindingAPI>(skeletonPrimSpec);
                    auto relSpec = NewSdfRelationshipSpec(
                        skeletonPrimSpec,
                        PXR_NS::UsdSkelTokens->skelAnimationSource,
                        false,
                        PXR_NS::SdfVariabilityUniform
                    );
                    Usd_InsertListItem(relSpec->GetTargetPathList(), animationPath, true);

                    return false;
                }

                return true;
            }
        );

        if (animationLayer)
        {
            ENSURE_STATUS_OK(WriteLayerTo(animationLayer, animationLayer->GetIdentifier(), detailedError));
        }
    }

    if (hasMultipleTracks)
    {
        for (const std::string& layerIdentifier : allSkeletalAnimationLayers)
        {
            AddExternalReference(defaultPrimSpec, layerIdentifier);
        }

        defaultPrimSpec->SetVariantSelection(ANIMATION_TRACK_VARIANT_SET_NAME, allAnimationTracks[0]);
    }
    ENSURE_STATUS_OK(WriteLayerTo(mainLayer, mMainUSDExportPath, detailedError));

    return OmniConverterStatus::OK;
}



void UsdExporter::ExportPreviewSurfaceNode(
    const StagePtr& stage,
    PXR_NS::SdfLayerHandle usdLayer,
    PXR_NS::SdfPrimSpecHandle& usdShader,
    const PXR_NS::SdfPrimSpecHandle& materialPrim,
    const MaterialPtr& material
)
{
    const PXR_NS::TfToken UsdPreviewSurfaceID("UsdPreviewSurface");
    CreateShadeSpecType(usdShader, PXR_NS::UsdShadeTokens->infoId, PXR_NS::SdfValueTypeNames->Token, PXR_NS::VtValue(UsdPreviewSurfaceID));

    CreateShadeSpecInput(usdShader, "ior", PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(1.5f));
    CreateShadeSpecInput(usdShader, "displacement", PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(0.0f));
    CreateShadeSpecInput(usdShader, "occlusion", PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(1.0f));
    CreateShadeSpecInput(
        usdShader,
        "useSpecularWorkflow",
        PXR_NS::SdfValueTypeNames->Int,
        PXR_NS::VtValue(material->useSpecularGlossyWorkflow ? 1 : 0)
    );

    if (!ExportPreviewSurfaceTextureNode(stage, usdLayer, materialPrim, usdShader, material, MaterialTextureType::DIFFUSE, "diffuseTex"))
    {
        CreateShadeSpecInput(usdShader, "diffuseColor", PXR_NS::SdfValueTypeNames->Color3f, PXR_NS::VtValue(material->diffuseColor));
    }

    if (!ExportPreviewSurfaceTextureNode(stage, usdLayer, materialPrim, usdShader, material, MaterialTextureType::EMISSIVE, "emmissiveTex"))
    {
        CreateShadeSpecInput(usdShader, "emissiveColor", PXR_NS::SdfValueTypeNames->Color3f, PXR_NS::VtValue(material->emissiveColor));
    }

    if (!ExportPreviewSurfaceTextureNode(stage, usdLayer, materialPrim, usdShader, material, MaterialTextureType::NORMAL, "normalTex"))
    {
        CreateShadeSpecInput(usdShader, "normal", PXR_NS::SdfValueTypeNames->Normal3f, PXR_NS::VtValue(PXR_NS::GfVec3f(0.0f, 0.0f, 1.0f)));
    }

    if (!ExportPreviewSurfaceTextureNode(stage, usdLayer, materialPrim, usdShader, material, MaterialTextureType::OPACITY, "opacityTex"))
    {
        CreateShadeSpecInput(usdShader, "opacity", PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(material->opacity));
    }
    CreateShadeSpecInput(usdShader, "opacityThreshold", PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(material->opacityThreshold));

    if (material->useSpecularGlossyWorkflow)
    {
        if (!ExportPreviewSurfaceTextureNode(stage, usdLayer, materialPrim, usdShader, material, MaterialTextureType::SPECULAR, "specularTex"))
        {
            CreateShadeSpecInput(usdShader, "specularColor", PXR_NS::SdfValueTypeNames->Color3f, PXR_NS::VtValue(material->specularColor));
        }

        // UsdPreviewSurface shares glossy and roughness as the same input name.
        if (!ExportPreviewSurfaceTextureNode(stage, usdLayer, materialPrim, usdShader, material, MaterialTextureType::GLOSSY, "glossyTex"))
        {
            float roughness;
            if (material->hasRoughnessFactor)
            {
                roughness = material->glossyFactor;
            }
            else
            {
                roughness = 0.5f;
            }
            CreateShadeSpecInput(usdShader, "roughness", PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(roughness));
        }
    }
    else
    {
        if (!ExportPreviewSurfaceTextureNode(stage, usdLayer, materialPrim, usdShader, material, MaterialTextureType::METALLIC, "metallicTex"))
        {
            CreateShadeSpecInput(usdShader, "metallic", PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(material->metallicFactor));
        }

        if (!ExportPreviewSurfaceTextureNode(stage, usdLayer, materialPrim, usdShader, material, MaterialTextureType::ROUGHNESS, "roughnessTex"))
        {
            float roughness;
            if (material->hasRoughnessFactor)
            {
                roughness = material->roughnessFactor;
            }
            else
            {
                roughness = 0.5f;
            }
            CreateShadeSpecInput(usdShader, "roughness", PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(roughness));
        }

        if (!ExportPreviewSurfaceTextureNode(stage, usdLayer, materialPrim, usdShader, material, MaterialTextureType::CLEARCOAT, "clearcoatTex"))
        {
            if (material->hasClearCoatFactor)
            {
                CreateShadeSpecInput(usdShader, "clearcoat", PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(material->clearCoatFactor));
            }
        }

        if (!ExportPreviewSurfaceTextureNode(
                stage,
                usdLayer,
                materialPrim,
                usdShader,
                material,
                MaterialTextureType::CLEARCOAT_ROUGHNESS,
                "clearcoatRoughnessTex"
            ))
        {
            if (material->hasClearCoatRoughnessFactor)
            {
                CreateShadeSpecInput(
                    usdShader,
                    "clearcoatRoughness",
                    PXR_NS::SdfValueTypeNames->Float,
                    PXR_NS::VtValue(material->clearCoatRoughnessFactor)
                );
            }
        }
    }
}

bool UsdExporter::ExportPreviewSurfaceTextureNode(
    const StagePtr& stage,
    PXR_NS::SdfLayerHandle usdLayer,
    const PXR_NS::SdfPrimSpecHandle& materialPrim,
    PXR_NS::SdfPrimSpecHandle& usdShader,
    const MaterialPtr& material,
    MaterialTextureType type,
    const std::string& name
)
{
    auto textureReference = material->GetTextureReference(type);
    if (!textureReference.IsValid() || type == MaterialTextureType::CLEARCOAT_NORMAL)
    {
        return false;
    }

    TextureImagePtr image = stage->images[textureReference.imageIndex];
    const auto& assetPath = mTextureUploadPath[image];
    std::string relativePath;
    PathUtils::ComputeRelativePath(assetPath, usdLayer->GetIdentifier(), relativePath);

    std::string texSet;
    if (textureReference.uvIndex == 0)
    {
        texSet = "st";
    }
    else
    {
        texSet = "st_" + std::to_string(textureReference.uvIndex);
    }

    const PXR_NS::TfToken fileShaderToken(name.c_str());
    PXR_NS::SdfPath fileShaderPath = materialPrim->GetPath().AppendChild(fileShaderToken);

    auto fileShaderSpec = GetOrCreatePrimSpec(usdLayer, fileShaderPath, PXR_NS::_tokens->shader);

    const PXR_NS::TfToken UsdUVTextureToken("UsdUVTexture");
    CreateShadeSpecType(fileShaderSpec, PXR_NS::UsdShadeTokens->infoId, PXR_NS::SdfValueTypeNames->Token, PXR_NS::VtValue(UsdUVTextureToken));

    CreateShadeSpecInput(fileShaderSpec, "file", PXR_NS::SdfValueTypeNames->Asset, PXR_NS::VtValue(PXR_NS::SdfAssetPath(relativePath.c_str())));


    const PXR_NS::SdfPath readerPath = materialPrim->GetPath().AppendElementString(name + "_TexCoordReader");

    auto stReaderSpec = GetOrCreatePrimSpec(usdLayer, readerPath, PXR_NS::_tokens->shader);

    CreateShadeSpecType(
        stReaderSpec,
        PXR_NS::UsdShadeTokens->infoId,
        PXR_NS::SdfValueTypeNames->Token,
        PXR_NS::VtValue(PXR_NS::_tokens->usdPrimvarReaderFloat2)
    );

    CreateShadeSpecInput(stReaderSpec, "varname", PXR_NS::SdfValueTypeNames->Token, PXR_NS::VtValue(PXR_NS::_tokens->st));
    CreateShadeSpecOutput(stReaderSpec, "result", PXR_NS::SdfValueTypeNames->Float2);

    static auto wrapModeToString = [](const TextureWrapMode& wrapMode)
    {
        if (wrapMode == TextureWrapMode::REPEAT)
        {
            return "repeat";
        }
        else if (wrapMode == TextureWrapMode::CLAMP)
        {
            return "clamp";
        }
        else
        {
            return "mirror";
        }
    };
    const std::string& wrapS = wrapModeToString(textureReference.wrapS);
    const std::string& wrapT = wrapModeToString(textureReference.wrapT);
    CreateShadeSpecInput(fileShaderSpec, "wrapS", PXR_NS::SdfValueTypeNames->Token, PXR_NS::VtValue(PXR_NS::TfToken(wrapS)));
    CreateShadeSpecInput(fileShaderSpec, "wrapT", PXR_NS::SdfValueTypeNames->Token, PXR_NS::VtValue(PXR_NS::TfToken(wrapT)));

    PXR_NS::SdfAttributeSpecHandle input;
    std::string outputChannels;
    PXR_NS::SdfValueTypeName valueType;
    static const std::string color[int(MaterialTextureType::END)] = {
        "diffuseColor", "emissiveColor", "opacity",  "normal",    "specularColor",      "roughness",
        "occlusion",    "roughness",     "metallic", "clearcoat", "clearcoatRoughness", "" // No clearcoatnormal
    };

    // Glossy will be mapped to roughness for UsdPreviewSurface, it needs to invert the value.
    PXR_NS::GfVec4f scale;
    PXR_NS::GfVec4f bias;
    if (type == MaterialTextureType::GLOSSY)
    {
        scale = PXR_NS::GfVec4f(1.0f, 1.0f, 1.0f, -1.0);
        bias = PXR_NS::GfVec4f(0.0f, 0.0f, 0.0f, 1.0);
    }
    else if (type == MaterialTextureType::NORMAL)
    {
        scale = PXR_NS::GfVec4f(2.0f, 2.0f, 2.0f, 1.0f);
        bias = PXR_NS::GfVec4f(-1.0f, -1.0f, -1.0f, 0.0f);
    }
    // the diffuse color should be baked into texture scale in mask mode
    else if (type == MaterialTextureType::DIFFUSE && material->opacityMode == GLTFOpacityMode::GLTF_MASK && material->hasDiffuseColor)
    {
        scale = PXR_NS::GfVec4f(material->diffuseColor[0], material->diffuseColor[1], material->diffuseColor[2], 0.0);
        bias = textureReference.bias;
    }
    else
    {
        scale = textureReference.scale;
        bias = textureReference.bias;
    }
    CreateShadeSpecInput(fileShaderSpec, "scale", PXR_NS::SdfValueTypeNames->Float4, PXR_NS::VtValue(scale));
    CreateShadeSpecInput(fileShaderSpec, "bias", PXR_NS::SdfValueTypeNames->Float4, PXR_NS::VtValue(bias));

    std::string coloSpace;
    if (textureReference.colorSpace == TextureColorSpace::AUTO)
    {
        coloSpace = "auto";
    }
    else if (textureReference.colorSpace == TextureColorSpace::RAW)
    {
        coloSpace = "raw";
    }
    else
    {
        coloSpace = "sRGB";
    }
    CreateShadeSpecInput(fileShaderSpec, "sourceColorSpace", PXR_NS::SdfValueTypeNames->Token, PXR_NS::VtValue(PXR_NS::TfToken(coloSpace)));

    if (type == MaterialTextureType::DIFFUSE || type == MaterialTextureType::EMISSIVE || type == MaterialTextureType::SPECULAR ||
        type == MaterialTextureType::NORMAL)
    {
        outputChannels = "rgb";
        valueType = PXR_NS::SdfValueTypeNames->Float3;
    }
    else if (type == MaterialTextureType::OPACITY)
    {
        if (textureReference.outputMode == TextureOutputMode::AVERAGE)
        {
            outputChannels = "r";
        }
        else
        {
            outputChannels = "a";
        }
        valueType = PXR_NS::SdfValueTypeNames->Float;
    }
    else if (type == MaterialTextureType::METALLIC)
    {
        outputChannels = "b";
        valueType = PXR_NS::SdfValueTypeNames->Float;
        CreateShadeSpecOutput(fileShaderSpec, outputChannels, PXR_NS::SdfValueTypeNames->Float);
        input = CreateShadeSpecInput(usdShader, color[int(type)], PXR_NS::SdfValueTypeNames->Float);
    }
    else if (type == MaterialTextureType::OCCLUSION)
    {
        outputChannels = "r";
        valueType = PXR_NS::SdfValueTypeNames->Float;
    }
    else if (type == MaterialTextureType::GLOSSY)
    {
        outputChannels = "a";
        valueType = PXR_NS::SdfValueTypeNames->Float;
    }
    else if (type == MaterialTextureType::ROUGHNESS)
    {
        outputChannels = "g";
        valueType = PXR_NS::SdfValueTypeNames->Float;
    }
    else if (type == MaterialTextureType::CLEARCOAT || type == MaterialTextureType::CLEARCOAT_ROUGHNESS)
    {
        outputChannels = "r";
        valueType = PXR_NS::SdfValueTypeNames->Float;
    }

    CreateShadeSpecOutput(fileShaderSpec, outputChannels, valueType);
    if (outputChannels != "rgb")
    {
        CreateShadeSpecOutput(fileShaderSpec, "rgb", PXR_NS::SdfValueTypeNames->Float3);
    }

    input = CreateShadeSpecInput(usdShader, color[int(type)], valueType);
    auto outputChannelSpec = CreateShadeSpecOutput(fileShaderSpec, outputChannels.c_str(), PXR_NS::SdfValueTypeNames->Token);
    Usd_InsertListItem(input->GetConnectionPathList(), outputChannelSpec->GetPath(), true);

    // Export uv transform and connect it
    const PXR_NS::SdfPath uvTransformPath = materialPrim->GetPath().AppendElementString(name + "_UsdTransform2d");
    auto uvTransformSpec = GetOrCreatePrimSpec(usdLayer, uvTransformPath, PXR_NS::_tokens->shader);
    CreateShadeSpecType(
        uvTransformSpec,
        PXR_NS::UsdShadeTokens->infoId,
        PXR_NS::SdfValueTypeNames->Token,
        PXR_NS::VtValue(PXR_NS::_tokens->usdTransform2d)
    );

    auto inInput = CreateShadeSpecInput(uvTransformSpec, "in", PXR_NS::SdfValueTypeNames->Float2);
    auto resultReaderOutput = CreateShadeSpecOutput(stReaderSpec, "result", PXR_NS::SdfValueTypeNames->Token);
    Usd_InsertListItem(inInput->GetConnectionPathList(), resultReaderOutput->GetPath(), true);

    CreateShadeSpecInput(uvTransformSpec, "rotation", PXR_NS::SdfValueTypeNames->Float, PXR_NS::VtValue(textureReference.transform.rotation[2]));
    CreateShadeSpecInput(uvTransformSpec, "scale", PXR_NS::SdfValueTypeNames->Float2, PXR_NS::VtValue(textureReference.transform.scale));
    CreateShadeSpecInput(uvTransformSpec, "translation", PXR_NS::SdfValueTypeNames->Float2, PXR_NS::VtValue(textureReference.transform.translation));
    CreateShadeSpecOutput(uvTransformSpec, "result", PXR_NS::SdfValueTypeNames->Float2);

    auto texSetInput = CreateShadeSpecInput(fileShaderSpec, texSet, PXR_NS::SdfValueTypeNames->Float2);
    auto resultuvTransformOutput = CreateShadeSpecOutput(uvTransformSpec, "result", PXR_NS::SdfValueTypeNames->Token);
    Usd_InsertListItem(texSetInput->GetConnectionPathList(), resultuvTransformOutput->GetPath(), true);

    return true;
}

PXR_NS::SdfPrimSpecHandle UsdExporter::CreateMaterialPrim(
    const StagePtr& stage,
    PXR_NS::SdfLayerHandle usdLayer,
    PXR_NS::SdfPath parentPath,
    const MaterialPtr& material
)
{
    // Making sure there is no naming conflict.
    auto nameIter = mMaterialNameInfos.find(material);
    NameInfo materialNameInfo;
    if (nameIter == mMaterialNameInfos.end())
    {
        materialNameInfo = GetNameInfo(material->name, "material", mUniqueMaterialName);
        mMaterialNameInfos.insert({ material, materialNameInfo });
    }
    else
    {
        materialNameInfo = nameIter->second;
    }

    // Gets or creates material
    auto scopePrim = GetOrCreatePrimSpec(usdLayer, parentPath, PXR_NS::_tokens->scope, true, materialNameInfo.displayName);
    auto materialPrimPath = scopePrim->GetPath().AppendElementString(materialNameInfo.name);
    auto materialSpec = usdLayer->GetPrimAtPath(materialPrimPath);
    if (materialSpec && materialSpec->GetTypeName() == PXR_NS::_tokens->material)
    {
        return materialSpec;
    }

    materialSpec = GetOrCreatePrimSpec(usdLayer, materialPrimPath, PXR_NS::_tokens->material, true, materialNameInfo.displayName);

    // Create shader prim
    auto shaderPrimPath = materialPrimPath.AppendElementString(materialNameInfo.name);
    auto shaderSpec = GetOrCreatePrimSpec(usdLayer, shaderPrimPath, PXR_NS::_tokens->shader, true, materialNameInfo.displayName);

    // Export preview surface only if specified.
    if (mExportContext->converterContext.ExportPreviewSurface())
    {
        auto shaderSurfaceOutputAttrSpec = GetOrNewSdfAttributeSpec(shaderSpec, "outputs:surface", PXR_NS::SdfValueTypeNames->Token);
        auto shaderDisplacementOutputAttrSpec = GetOrNewSdfAttributeSpec(shaderSpec, "outputs:displacement", PXR_NS::SdfValueTypeNames->Token);

        auto surfaceAttrSpec = GetOrNewSdfAttributeSpec(materialSpec, "outputs:surface", PXR_NS::SdfValueTypeNames->Token);
        Usd_InsertListItem(surfaceAttrSpec->GetConnectionPathList(), shaderSurfaceOutputAttrSpec->GetPath(), true);

        auto displacementAttrSpec = GetOrNewSdfAttributeSpec(materialSpec, "outputs:displacement", PXR_NS::SdfValueTypeNames->Token);
        Usd_InsertListItem(displacementAttrSpec->GetConnectionPathList(), shaderDisplacementOutputAttrSpec->GetPath(), true);

        ExportPreviewSurfaceNode(stage, usdLayer, shaderSpec, materialSpec, material);
    }

    return materialSpec;
}


void UsdExporter::AddExternalReference(const PXR_NS::SdfPrimSpecHandle primSpec, const std::string& referencePath, const PXR_NS::SdfPath& primPath)
{
    if (referencePath.empty() && primPath.IsEmpty())
    {
        Log("Skips authoring empty external reference.");
        return;
    }

    std::string relativePath;
    PathUtils::ComputeRelativePath(referencePath, primSpec->GetLayer()->GetIdentifier(), relativePath);
    if (!primPath.IsEmpty())
    {
        Usd_InsertListItem(primSpec->GetReferenceList(), PXR_NS::SdfReference(relativePath, primPath));
    }
    else
    {
        Usd_InsertListItem(primSpec->GetReferenceList(), PXR_NS::SdfReference(relativePath));
    }
}

void UsdExporter::PreprocessAllNodes(const StagePtr& stage)
{
    bool inBoneTree = false;
    StageUtils::TraverseStageTree(
        stage->rootNode,
        [&stage, this, &inBoneTree](const StageNodePtr& stageNode)
        {
            auto& stageNodeInfo = mStageNodeInfos[stageNode];
            // some bone tree include not-bone node, and it will change the inBoneTree to false
            // so we also need to add condition 'stageNode->isBoneNode' here
            // see Maya2023\presets\hik\examples\Dummy_HIK_FightAnimation.fbx
            if (inBoneTree || stageNode->IsRootBone() || stageNode->isBoneNode)
            {
                inBoneTree = true;
                const auto& boneNodeName = MakeValidUSDIdentifier(stageNode->name, "bone");
                std::string jointName;
                if (stageNode->IsRootBone())
                {
                    jointName = PXR_NS::TfToken(boneNodeName);
                }
                else
                {
                    const auto& parentNode = stageNode->parent.lock();
                    jointName = PXR_NS::TfToken(mStageNodeInfos[parentNode].jointName + "/" + boneNodeName);
                }
                stageNodeInfo.jointName = jointName;
            }

            for (size_t i = 0; i < stageNode->staticMeshInstances.size(); i++)
            {
                size_t meshIndex = stageNode->staticMeshInstances[i];
                auto mesh = stage->meshes[meshIndex];
                mMeshPrimInfos[mesh].meshInstanceCount += 1;
            }

            bool includeMeshes = stageNode->staticMeshInstances.size() > 0;
            bool includeCurves = stageNode->curveInstances.size() > 0;
            bool hasProps = stageNode->cameras.size() > 0 || includeMeshes || includeCurves || stageNode->lights.size() > 0;
            bool hasSkeleton = stageNode->isBoneNode;
            stageNodeInfo.hasProps = hasProps;
            stageNodeInfo.hasSkeleton = hasSkeleton;

            return true;
        },
        [&stage, this, &inBoneTree](const StageNodePtr& stageNode)
        {
            if (stageNode->IsRootBone())
            {
                inBoneTree = false;
            }

            bool hasProps = false;
            bool hasSkeleton = false;
            for (const auto& child : stageNode->children)
            {
                const auto& childNodeInfo = mStageNodeInfos[child];
                hasProps = hasProps || childNodeInfo.hasProps;
                hasSkeleton = hasSkeleton || childNodeInfo.hasSkeleton;
            }

            auto& stageNodeInfo = mStageNodeInfos[stageNode];
            stageNodeInfo.hasProps = stageNodeInfo.hasProps || hasProps;
            stageNodeInfo.hasSkeleton = stageNodeInfo.hasSkeleton || hasSkeleton;
        }
    );
}

UsdExporter::NameInfo UsdExporter::GetNameInfo(
    const std::string& baseName,
    const std::string& prefix,
    std::unordered_map<std::string, size_t>& nameMap
)
{
    auto validName = MakeValidIdentifier(baseName);
    NameInfo info;
    if (validName.empty() || validName[0] == '_')
    {
        validName = prefix + validName;
        info.displayName = baseName;
    }

    // Convert to lowercase for case-insensitive comparison
    std::string lowerValidName = PXR_NS::TfStringToLower(validName);
    std::string uniqueName = validName;

    // Find any existing name that matches case-insensitively
    auto iter = std::find_if(
        nameMap.begin(),
        nameMap.end(),
        [&lowerValidName](const auto& pair)
        {
            return PXR_NS::TfStringToLower(pair.first) == lowerValidName;
        }
    );

    // If a name collision is found, append a number to the name and recheck
    while (iter != nameMap.end())
    {
        uniqueName = validName + std::to_string(iter->second);
        iter->second += 1;
        // Check for case-insensitive match with the new name
        iter = std::find_if(
            nameMap.begin(),
            nameMap.end(),
            [&uniqueName](const auto& pair)
            {
                return PXR_NS::TfStringToLower(pair.first) == PXR_NS::TfStringToLower(uniqueName);
            }
        );
    }
    nameMap.insert({ uniqueName, 0 });
    info.name = uniqueName;

    return info;
}

void UsdExporter::SetXformTransformSamples(
    PXR_NS::SdfPrimSpecHandle xformPrimSpec,
    const TransformTimesamples& transformTimesamples,
    bool useDoublePrecisionOps,
    bool useTESOps,
    const StagePtr& stage
)
{
    if (!transformTimesamples.Empty())
    {
        const auto& translationSamples = transformTimesamples.GetTranslationSamples();
        for (size_t i = 0; i < translationSamples.size(); i++)
        {
            SetTranslateSample(xformPrimSpec, translationSamples[i], PXR_NS::UsdTimeCode(i / stage->mutiplier), useDoublePrecisionOps, false);
        }

        const auto& scaleSamples = transformTimesamples.GetScaleSamples();
        for (size_t i = 0; i < scaleSamples.size(); i++)
        {
            SetScaleSample(xformPrimSpec, scaleSamples[i], PXR_NS::UsdTimeCode(i / stage->mutiplier), useDoublePrecisionOps, false);
        }

        if (useTESOps)
        {
            const auto& rotationSamples = transformTimesamples.GetRotationXYZSamples();
            for (size_t i = 0; i < rotationSamples.size(); i++)
            {
                SetRotateXYZSample(xformPrimSpec, rotationSamples[i], PXR_NS::UsdTimeCode(i / stage->mutiplier), useDoublePrecisionOps, false);
            }
        }
        else
        {
            const auto& rotationSamples = transformTimesamples.GetOrientSamples();
            for (size_t i = 0; i < rotationSamples.size(); i++)
            {
                SetOrientSample(xformPrimSpec, rotationSamples[i], PXR_NS::UsdTimeCode(i / stage->mutiplier), useDoublePrecisionOps, false);
            }
        }
    }
}

void UsdExporter::BindMaterialToPrimFromSubset(
    const StagePtr& stage,
    const MeshGeomSubset& subset,
    PXR_NS::SdfPrimSpecHandle primSpec,
    PXR_NS::SdfPath materialGroupPath
)
{
    if (mExportContext->converterContext.IgnoreMaterials())
    {
        return;
    }

    if (subset.materialIndex != INVALID_MATERIAL_INDEX && subset.materialIndex < stage->materials.size())
    {
        auto materialPrimSpec = CreateMaterialPrim(stage, primSpec->GetLayer(), materialGroupPath, stage->materials[subset.materialIndex]);

        ApplyBindingAPIToPrimSpec<PXR_NS::UsdShadeMaterialBindingAPI>(primSpec);
        auto relSpec = NewSdfRelationshipSpec(primSpec, PXR_NS::UsdShadeTokens->materialBinding, false, PXR_NS::SdfVariabilityUniform);
        Usd_InsertListItem(relSpec->GetTargetPathList(), materialPrimSpec->GetPath(), true);
    }
}

