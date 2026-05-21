// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "usd_convert_asset.h"

#include <pybind11/chrono.h>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;

struct OmniConverterFuture
{
};

namespace omni::connect::asset_converter
{
std::unordered_map<void*, py::buffer> gAllocatedBuffers;

py::object gPythonLogCallback;
py::object gPythonReadCallback;
py::object gPythonBinaryWriteCallback;
py::object gPythonLayerWriteCallback;
py::object gPythonMakeDirsCallback;
py::object gPythonPathExistsCallback;
py::object gPythonProgressCallback;
py::object gPythonMaterialLoader;
py::object gPythonFileCopyCallback;

static bool HasPythonLogCallback()
{
    return gPythonLogCallback && !gPythonLogCallback.is_none();
}

static void CallPythonLogCallback(const char* message)
{
    if (!HasPythonLogCallback())
    {
        return;
    }

    try
    {
        gPythonLogCallback(message);
    }
    catch (...)
    {
    }
}


class _Content
{
public:

    _Content(OmniConverterBlob* blob)
    {
        mContent.buffer = nullptr;
        mContent.size = 0;
        mContent.deleter = nullptr;
        if (blob)
        {
            mContent = *blob;
            blob->deleter = 0;
        }
    }

    ~_Content()
    {
        if (mContent.deleter && mContent.buffer)
        {
            mContent.deleter(mContent.buffer);
        }
    }

    char* buffer()
    {
        return (char*)mContent.buffer;
    }

    char* buffer() const
    {
        return (char*)mContent.buffer;
    }

    void assign(py::buffer buffer)
    {
        // Increase ref to hold reference
        mBuffer = buffer;

        py::buffer_info info = buffer.request();
        mContent.buffer = info.ptr;
        mContent.size = info.size;
        mContent.deleter = nullptr;
    }

    void moveTo(OmniConverterBlob* blob)
    {
        blob->buffer = mContent.buffer;
        blob->size = mContent.size;
        gAllocatedBuffers[blob->buffer] = mBuffer;
        blob->deleter = [](void* buffer)
        {
            // Release python buffer
            gAllocatedBuffers.erase(buffer);
        };
    }

    size_t getSize() const
    {
        return mContent.size;
    }

private:

    OmniConverterBlob mContent;
    py::buffer mBuffer;
};

class _MaterialProperty
{
public:

    _MaterialProperty()
    {
    }

    _MaterialProperty(OmniConverterMaterialProperty* property)
    {
        mName = omniConverterGetPropertyName(property);
        mValueType = omniConverterGetPropertyValueType(property);
        mIsTextureProperty = omniConverterIsTextureProperty(property);
        mGroupName = omniConverterGetPropertyMDLGroupName(property);
        mDisplayName = omniConverterGetPropertyMDLDisplayName(property);
        switch (mValueType)
        {
            case OMNI_CONVERTER_VALUE_TYPE_BOOL:
                mValue = py::bool_(omniConverterToBool(property));
                break;
            case OMNI_CONVERTER_VALUE_TYPE_INT32:
                mValue = py::int_(omniConverterToInt32(property));
                break;
            case OMNI_CONVERTER_VALUE_TYPE_DOUBLE:
                mValue = py::float_(omniConverterToDouble(property));
                break;
            case OMNI_CONVERTER_VALUE_TYPE_DOUBLE2:
            {
                const auto& value2 = omniConverterToDouble2(property);
                mValue = py::make_tuple(value2.value[0], value2.value[1]);
                break;
            }
            case OMNI_CONVERTER_VALUE_TYPE_DOUBLE3:
            {
                const auto& value3 = omniConverterToDouble3(property);
                mValue = py::make_tuple(value3.value[0], value3.value[1], value3.value[2]);
                break;
            }
            case OMNI_CONVERTER_VALUE_TYPE_DOUBLE4:
            {
                const auto& value4 = omniConverterToDouble4(property);
                mValue = py::make_tuple(value4.value[0], value4.value[1], value4.value[2], value4.value[3]);
                break;
            }
            case OMNI_CONVERTER_VALUE_TYPE_DOUBLE9:
            {
                const auto& value9 = omniConverterToDouble9(property);
                mValue = py::make_tuple(
                    value9.value[0],
                    value9.value[1],
                    value9.value[2],
                    value9.value[3],
                    value9.value[4],
                    value9.value[5],
                    value9.value[6],
                    value9.value[7],
                    value9.value[8]
                );
                break;
            }
            case OMNI_CONVERTER_VALUE_TYPE_DOUBLE16:
            {
                const auto& value16 = omniConverterToDouble16(property);
                mValue = py::make_tuple(
                    value16.value[0],
                    value16.value[1],
                    value16.value[2],
                    value16.value[3],
                    value16.value[4],
                    value16.value[5],
                    value16.value[6],
                    value16.value[7],
                    value16.value[8],
                    value16.value[9],
                    value16.value[10],
                    value16.value[11],
                    value16.value[12],
                    value16.value[13],
                    value16.value[14],
                    value16.value[15]
                );
                break;
            }
            case OMNI_CONVERTER_VALUE_TYPE_STRING:
                mValue = py::str(omniConverterToString(property));
                break;
            default:
                break;
        }
    }

    const std::string& GetName() const
    {
        return mName;
    }

    void SetName(const std::string& name)
    {
        mName = name;
    }

    py::object GetValue() const
    {
        return mValue;
    }

    void SetValue(py::object value)
    {
        mValue = value;
        if (py::isinstance<py::bool_>(value))
        {
            mValueType = OMNI_CONVERTER_VALUE_TYPE_BOOL;
        }
        else if (py::isinstance<py::int_>(value))
        {
            mValueType = OMNI_CONVERTER_VALUE_TYPE_INT32;
        }
        else if (py::isinstance<py::float_>(value))
        {
            mValueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE;
        }
        else if (py::isinstance<py::str>(value))
        {
            mValueType = OMNI_CONVERTER_VALUE_TYPE_STRING;
        }
        else if (py::isinstance<py::tuple>(value) || py::isinstance<py::list>(value))
        {
            size_t size = 0;
            py::tuple valueTuple = py::tuple(value);
            size = valueTuple.size();
            if (size != 1 && size != 2 && size != 3 && size != 4 && size != 9 && size != 16)
            {
                mValueType = OMNI_CONVERTER_VALUE_TYPE_UNDEFINED;
            }
            else
            {
                if (size == 1)
                {
                    mValueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE;
                }
                else if (size == 2)
                {
                    mValueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE2;
                }
                else if (size == 3)
                {
                    mValueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE3;
                }
                else if (size == 4)
                {
                    mValueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE4;
                }
                else if (size == 9)
                {
                    mValueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE9;
                }
                else if (size == 16)
                {
                    mValueType = OMNI_CONVERTER_VALUE_TYPE_DOUBLE16;
                }
            }
        }
    }

    bool IsSinglePrecision() const
    {
        return mSinglePrecision;
    }

    void SetIsSinglePrecision(bool value)
    {
        mSinglePrecision = value;
    }

    OmniConverterMaterialPropertyValueType GetValueType() const
    {
        return mValueType;
    }

    void SetValueType(OmniConverterMaterialPropertyValueType valueType)
    {
        mValueType = valueType;
    }

    bool IsTextureProperty() const
    {
        return mIsTextureProperty;
    }

    void SetIsTextureProperty(bool IsTextureProperty)
    {
        mIsTextureProperty = IsTextureProperty;
    }

    const std::string& GetGroupName() const
    {
        return mGroupName;
    }

    void SetGroupName(const std::string& groupName)
    {
        mGroupName = groupName;
    }

    const std::string& GetDisplayName() const
    {
        return mDisplayName;
    }

    void SetDisplayName(const std::string& displayName)
    {
        mDisplayName = displayName;
    }

    OmniConverterMDLPropertyType GetDetailedValueType() const
    {
        return mDetailedType;
    }

    void SetDetailedValueType(OmniConverterMDLPropertyType valueType)
    {
        mDetailedType = valueType;
    }

    py::object GetDefaultValue() const
    {
        return mDefaultValue;
    }

    void SetDefaultValue(py::object value)
    {
        mDefaultValue = value;
    }

    py::object GetMinValue() const
    {
        return mMinValue;
    }

    void SetMinValue(py::object value)
    {
        mMinValue = value;
    }

    py::object GetMaxValue() const
    {
        return mMaxValue;
    }

    void SetMaxValue(py::object value)
    {
        mMaxValue = value;
    }

    const std::string& GetColorSpace() const
    {
        return mColorSpace;
    }

    void SetColorSpace(const std::string& colorSpace)
    {
        mColorSpace = colorSpace;
    }

    void SetTextureTranslation(py::object textureTranslation)
    {
        mTextureTranslation = textureTranslation;
    }

    OmniConverterDouble2 GetTextureTranslation() const
    {
        if (py::isinstance<py::tuple>(mTextureTranslation) || py::isinstance<py::list>(mTextureTranslation))
        {
            size_t size = 0;
            py::tuple valueTuple = py::tuple(mTextureTranslation);
            size = valueTuple.size();
            if (size >= 2)
            {
                double value0 = double(py::float_(valueTuple[0]));
                double value1 = double(py::float_(valueTuple[1]));

                return { value0, value1 };
            }
        }

        return { 0.0, 0.0 };
    }

    void SetTextureScale(py::object textureScale)
    {
        mTextureScale = textureScale;
    }

    OmniConverterDouble2 GetTextureScale() const
    {
        if (py::isinstance<py::tuple>(mTextureScale) || py::isinstance<py::list>(mTextureScale))
        {
            size_t size = 0;
            py::tuple valueTuple = py::tuple(mTextureScale);
            size = valueTuple.size();
            if (size >= 2)
            {
                double value0 = double(py::float_(valueTuple[0]));
                double value1 = double(py::float_(valueTuple[1]));

                return { value0, value1 };
            }
        }

        return { 1.0, 1.0 };
    }

private:

    std::string mName;
    py::object mValue = py::none();
    bool mSinglePrecision;
    OmniConverterMaterialPropertyValueType mValueType = OMNI_CONVERTER_VALUE_TYPE_UNDEFINED;
    bool mIsTextureProperty = false;
    py::object mTextureTranslation = py::none();
    py::object mTextureScale = py::none();

    // Output only
    std::string mGroupName;
    std::string mDisplayName;
    OmniConverterMDLPropertyType mDetailedType = OMNI_CONVERTER_MDL_PROPERTY_TYPE_UNDEFINED;
    py::object mDefaultValue = py::none();
    py::object mMinValue = py::none();
    py::object mMaxValue = py::none();
    std::string mColorSpace;
};

class _MaterialLoaderOutput
{
public:

    _MaterialLoaderOutput()
    {
    }

    void AddProperty(const std::shared_ptr<_MaterialProperty>& property)
    {
        mProperties.push_back(property);
    }

    size_t Size() const
    {
        return mProperties.size();
    }

    std::shared_ptr<_MaterialProperty> GetProperty(size_t index) const
    {
        if (index >= mProperties.size())
        {
            return nullptr;
        }

        return mProperties[index];
    }

    void SetProperty(size_t index, const std::shared_ptr<_MaterialProperty>& property)
    {
        if (index >= mProperties.size())
        {
            return;
        }

        mProperties[index] = property;
    }

    const std::string& GetMaterialFilePath() const
    {
        return mMaterialFilePath;
    }

    void SetMaterialFilePath(const std::string& materialFilePath)
    {
        mMaterialFilePath = materialFilePath;
    }

    const std::string& GetMaterialSubIdentifier() const
    {
        return mMaterialSubIdentifier;
    }

    void SetMaterialSubIdentifier(const std::string& materialSubIdentifier)
    {
        mMaterialSubIdentifier = materialSubIdentifier;
    }

    bool IsBuiltInMaterial() const
    {
        return mBuiltin;
    }

    void SetBuiltIn(bool value)
    {
        mBuiltin = value;
    }

private:

    std::vector<std::shared_ptr<_MaterialProperty>> mProperties;
    std::string mMaterialFilePath;
    std::string mMaterialSubIdentifier;
    bool mBuiltin = false;
};

class _MaterialDescription
{
public:

    _MaterialDescription(OmniConverterMaterialDescription* materialDescription)
    {
        mMaterialName = omniConverterGetMaterialName(materialDescription);
        mMaterialClassId = omniConverterGetMaterialClassId(materialDescription);
        size_t numProperties = omniConverterGetNumInputProperties(materialDescription);
        for (size_t i = 0; i < numProperties; i++)
        {
            std::shared_ptr<_MaterialProperty> property = std::make_shared<_MaterialProperty>(omniConverterGetInputProperty(materialDescription, i));
            if (property->GetValueType() == OMNI_CONVERTER_VALUE_TYPE_UNDEFINED)
            {
                continue;
            }
            mProperties.push_back(property);
        }
    }

    const std::string& GetName() const
    {
        return mMaterialName;
    }

    const std::string& GetClassId() const
    {
        return mMaterialClassId;
    }

    size_t Size() const
    {
        return mProperties.size();
    }

    std::shared_ptr<_MaterialProperty> GetProperty(size_t index) const
    {
        if (index >= mProperties.size())
        {
            return nullptr;
        }

        return mProperties[index];
    }

private:

    std::string mMaterialName;
    std::string mMaterialClassId;
    std::vector<std::shared_ptr<_MaterialProperty>> mProperties;
};

static bool ToValue(py::object object, OmniConverterMDLPropertyMetadata::_Value& value)
{
    if (object.is_none())
    {
        return false;
    }

    if (py::isinstance<py::bool_>(object))
    {
        value.boolValue = bool(py::bool_(object));
    }
    else if (py::isinstance<py::int_>(object))
    {
        value.intValue = int(py::int_(object));
    }
    else if (py::isinstance<py::float_>(object))
    {
        value.doubleValue = double(py::float_(object));
    }
    else if (py::isinstance<py::tuple>(object) || py::isinstance<py::list>(object))
    {
        size_t size = 0;
        py::tuple valueTuple = py::tuple(object);
        size = valueTuple.size();
        if (size != 1 && size != 2 && size != 3 && size != 4 && size != 9 && size != 16)
        {
            return false;
        }
        else
        {
            if (size == 1)
            {
                value.doubleValue = double(py::float_(py::tuple(object)[0]));
            }
            else if (size == 2)
            {
                value.double2Value[0] = double(py::float_(py::tuple(object)[0]));
                value.double2Value[1] = double(py::float_(py::tuple(object)[1]));
            }
            else if (size == 3)
            {
                value.double3Value[0] = double(py::float_(py::tuple(object)[0]));
                value.double3Value[1] = double(py::float_(py::tuple(object)[1]));
                value.double3Value[2] = double(py::float_(py::tuple(object)[2]));
            }
            else if (size == 4)
            {
                value.double4Value[0] = double(py::float_(py::tuple(object)[0]));
                value.double4Value[1] = double(py::float_(py::tuple(object)[1]));
                value.double4Value[2] = double(py::float_(py::tuple(object)[2]));
                value.double4Value[3] = double(py::float_(py::tuple(object)[3]));
            }
            else if (size == 9)
            {
                for (size_t i = 0; i < 9; i++)
                {
                    value.double9Value[i] = double(py::float_(py::tuple(object)[i]));
                }
            }
            else if (size == 16)
            {
                for (size_t i = 0; i < 16; i++)
                {
                    value.double16Value[i] = double(py::float_(py::tuple(object)[i]));
                }
            }
        }
    }

    return true;
}
}; // namespace omni::connect::asset_converter

PYBIND11_MODULE(_assetconverter, m)
{
    using namespace pybind11::literals;
    using namespace omni::connect::asset_converter;
    m.doc() = "usd-convert-asset";
    m.attr("OMNI_CONVERTER_MAJOR_VERSION") = py::int_(11);
    m.attr("OMNI_CONVERTER_MINOR_VERSION") = py::int_(0);
    m.attr("OMNI_CONVERTER_FLAGS_IGNORE_ANIMATION") = OMNI_CONVERTER_FLAGS_IGNORE_ANIMATION;
    m.attr("OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS") = OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS;
    m.attr("OMNI_CONVERTER_FLAGS_SINGLE_MESH_FILE") = OMNI_CONVERTER_FLAGS_SINGLE_MESH_FILE;
    m.attr("OMNI_CONVERTER_FLAGS_GEN_SMOOTH_NORMALS") = OMNI_CONVERTER_FLAGS_GEN_SMOOTH_NORMALS;
    m.attr("OMNI_CONVERTER_FLAGS_IGNORE_CAMERAS") = OMNI_CONVERTER_FLAGS_IGNORE_CAMERAS;
    m.attr("OMNI_CONVERTER_FLAGS_IGNORE_LIGHTS") = OMNI_CONVERTER_FLAGS_IGNORE_LIGHTS;
    m.attr("OMNI_CONVERTER_FLAGS_SUPPORT_POINTER_INSTANCER") = OMNI_CONVERTER_FLAGS_SUPPORT_POINTER_INSTANCER;
    m.attr("OMNI_CONVERTER_FLAGS_EXPORT_AS_SHAPENET") = OMNI_CONVERTER_FLAGS_EXPORT_AS_SHAPENET;
    m.attr("OMNI_CONVERTER_FLAGS_USE_METER_PER_UNIT") = OMNI_CONVERTER_FLAGS_USE_METER_PER_UNIT;
    m.attr("OMNI_CONVERTER_FLAGS_CREATE_WORLD_AS_DEFAULT_PRIM") = OMNI_CONVERTER_FLAGS_CREATE_WORLD_AS_DEFAULT_PRIM;
    m.attr("OMNI_CONVERTER_FLAGS_EMBED_FBX_TEXTURES") = OMNI_CONVERTER_FLAGS_EMBED_FBX_TEXTURES;
    m.attr("OMNI_CONVERTER_FLAGS_EMBED_TEXTURES") = OMNI_CONVERTER_FLAGS_EMBED_TEXTURES;
    m.attr("OMNI_CONVERTER_FLAGS_FBX_CONVERT_TO_Y_UP") = OMNI_CONVERTER_FLAGS_FBX_CONVERT_TO_Y_UP;
    m.attr("OMNI_CONVERTER_FLAGS_FBX_CONVERT_TO_Z_UP") = OMNI_CONVERTER_FLAGS_FBX_CONVERT_TO_Z_UP;
    m.attr("OMNI_CONVERTER_FLAGS_KEEP_ALL_MATERIALS") = OMNI_CONVERTER_FLAGS_KEEP_ALL_MATERIALS;
    m.attr("OMNI_CONVERTER_FLAGS_MERGE_ALL_MESHES") = OMNI_CONVERTER_FLAGS_MERGE_ALL_MESHES;
    m.attr("OMNI_CONVERTER_FLAGS_USE_DOUBLE_PRECISION_FOR_USD_TRANSFORM_OP") = OMNI_CONVERTER_FLAGS_USE_DOUBLE_PRECISION_FOR_USD_TRANSFORM_OP;
    m.attr("OMNI_CONVERTER_FLAGS_IGNORE_PIVOTS") = OMNI_CONVERTER_FLAGS_IGNORE_PIVOTS;
    m.attr("OMNI_CONVERTER_FLAGS_DISABLE_INSTANCING") = OMNI_CONVERTER_FLAGS_DISABLE_INSTANCING;
    m.attr("OMNI_CONVERTER_FLAGS_EXPORT_HIDDEN_PROPS") = OMNI_CONVERTER_FLAGS_EXPORT_HIDDEN_PROPS;
    m.attr("OMNI_CONVERTER_FLAGS_FBX_BAKING_SCALES_INTO_MESH") = OMNI_CONVERTER_FLAGS_FBX_BAKING_SCALES_INTO_MESH;
    m.attr("OMNI_CONVERTER_FLAGS_IGNORE_FLIP_ROTATION") = OMNI_CONVERTER_FLAGS_IGNORE_FLIP_ROTATION;
    m.attr("OMNI_CONVERTER_FLAGS_FBX_IGNORE_UNBOUND_BONES") = OMNI_CONVERTER_FLAGS_FBX_IGNORE_UNBOUND_BONES;
    m.attr("OMNI_CONVERTER_FLAGS_EXPORT_EMBEDDED_GLTF") = OMNI_CONVERTER_FLAGS_EXPORT_EMBEDDED_GLTF;
    m.attr("OMNI_CONVERTER_FLAGS_STAGE_UP_Y") = OMNI_CONVERTER_FLAGS_STAGE_UP_Y;
    m.attr("OMNI_CONVERTER_FLAGS_STAGE_UP_Z") = OMNI_CONVERTER_FLAGS_STAGE_UP_Z;

    py::enum_<OmniConverterMDLPropertyType>(m, "OmniConverterMDLPropertyType")
        .value("POINT3D", OMNI_CONVERTER_MDL_PROPERTY_TYPE_POINT3D)
        .value("COLOR3D", OMNI_CONVERTER_MDL_PROPERTY_TYPE_COLOR3D)
        .value("COLOR4D", OMNI_CONVERTER_MDL_PROPERTY_TYPE_COLOR4D)
        .value("NORMAL3D", OMNI_CONVERTER_MDL_PROPERTY_TYPE_NORMAL3D)
        .value("VECTOR3D", OMNI_CONVERTER_MDL_PROPERTY_TYPE_VECTOR3D)
        .value("QUATD", OMNI_CONVERTER_MDL_PROPERTY_TYPE_QUATD)
        .value("MATRIX2D", OMNI_CONVERTER_MDL_PROPERTY_TYPE_MATRIX2D)
        .value("MATRIX3D", OMNI_CONVERTER_MDL_PROPERTY_TYPE_MATRIX3D)
        .value("MATRIX4D", OMNI_CONVERTER_MDL_PROPERTY_TYPE_MATRIX4D)
        .value("TEXCOORD2D", OMNI_CONVERTER_MDL_PROPERTY_TYPE_TEXCOORD2D)
        .value("TEXCOORD3D", OMNI_CONVERTER_MDL_PROPERTY_TYPE_TEXCOORD3D)
        .value("TOKEN", OMNI_CONVERTER_MDL_PROPERTY_TYPE_TOKEN)
        .value("ASSET", OMNI_CONVERTER_MDL_PROPERTY_TYPE_ASSET)
        .value("UNDEFINED", OMNI_CONVERTER_MDL_PROPERTY_TYPE_UNDEFINED)
        .export_values();

    py::class_<_MaterialProperty, std::shared_ptr<_MaterialProperty>>(m, "MaterialProperty", R"(
        Object to hold property of material. The property can be both the inputs of describing
        MaterialDescription or MaterialLoaderOutput. MaterialProperty is a wrapper of a named value.
    )")
        .def(
            py::init(
                [](const std::string& name,
                   const std::string displayName,
                   const std::string& groupName,
                   bool isTextureProperty,
                   py::object value,
                   py::object defaultValue,
                   py::object minValue,
                   py::object maxValue,
                   OmniConverterMDLPropertyType detailedType,
                   const std::string& colorSpace,
                   py::object textureTranslation,
                   py::object textureScale,
                   bool singlePrecision)
                {
                    auto p = _MaterialProperty();
                    p.SetName(name);
                    p.SetDisplayName(displayName);
                    p.SetGroupName(groupName);
                    p.SetIsTextureProperty(isTextureProperty);
                    p.SetValue(value);
                    p.SetDefaultValue(defaultValue);
                    p.SetMinValue(minValue);
                    p.SetMaxValue(maxValue);
                    p.SetDetailedValueType(detailedType);
                    p.SetColorSpace(colorSpace);
                    p.SetTextureTranslation(textureTranslation);
                    p.SetTextureScale(textureScale);
                    p.SetIsSinglePrecision(singlePrecision);

                    return p;
                }
            ),
            py::arg("name") = "",
            py::arg("display_name") = "",
            py::arg("group_name") = "",
            py::arg("is_texture_property") = false,
            py::arg("value") = py::none(),
            py::arg("default_value") = py::none(),
            py::arg("min_value") = py::none(),
            py::arg("max_value") = py::none(),
            py::arg("detailed_type") = OMNI_CONVERTER_MDL_PROPERTY_TYPE_UNDEFINED,
            py::arg("color_space") = "",
            py::arg("texture_translation") = py::none(),
            py::arg("texture_scale") = py::none(),
            py::arg("single_precision") = false
        )
        .def_property("name", &_MaterialProperty::GetName, &_MaterialProperty::SetName)
        .def_property("value", &_MaterialProperty::GetValue, &_MaterialProperty::SetValue)
        .def_property("is_texture_property", &_MaterialProperty::IsTextureProperty, &_MaterialProperty::SetIsTextureProperty)
        .def_property("group_name", &_MaterialProperty::GetGroupName, &_MaterialProperty::SetGroupName)
        .def_property("display_name", &_MaterialProperty::GetDisplayName, &_MaterialProperty::SetDisplayName)
        .def_property("detailed_value_type", &_MaterialProperty::GetDetailedValueType, &_MaterialProperty::SetDetailedValueType)
        .def_property("default_value", &_MaterialProperty::GetDefaultValue, &_MaterialProperty::SetDefaultValue)
        .def_property("min_value", &_MaterialProperty::GetMinValue, &_MaterialProperty::SetMinValue)
        .def_property("max_value", &_MaterialProperty::GetMaxValue, &_MaterialProperty::SetMaxValue)
        .def_property("color_space", &_MaterialProperty::GetColorSpace, &_MaterialProperty::SetColorSpace)
        .def_property("texture_translation", &_MaterialProperty::GetTextureTranslation, &_MaterialProperty::SetTextureTranslation)
        .def_property("texture_scale", &_MaterialProperty::GetTextureScale, &_MaterialProperty::SetTextureScale)
        .def_property("single_precision", &_MaterialProperty::IsSinglePrecision, &_MaterialProperty::SetIsSinglePrecision);

    py::class_<_MaterialLoaderOutput, std::shared_ptr<_MaterialLoaderOutput>>(m, "MaterialLoaderOutput", R"(
        Object to hold output of material loader after parsing inputs of MaterialDescription.

        It includes the input properties that will be applied to material, material file path,
        and also the entry identifier of material. If entry identifier is empty, it will use
        the file name of material file by default.
    )")
        .def(py::init<>())
        .def(
            "__getitem__",
            [](const _MaterialLoaderOutput& self, size_t i)
            {
                if (i >= self.Size())
                {
                    throw py::index_error();
                }
                return self.GetProperty(i);
            }
        )
        .def(
            "__setitem__",
            [](_MaterialLoaderOutput& self, size_t i, const std::shared_ptr<_MaterialProperty>& property)
            {
                if (i >= self.Size())
                {
                    throw py::index_error();
                }
                self.SetProperty(i, property);
            }
        )
        .def("append", &_MaterialLoaderOutput::AddProperty)
        .def_property("material_path", &_MaterialLoaderOutput::GetMaterialFilePath, &_MaterialLoaderOutput::SetMaterialFilePath)
        .def_property("builtin", &_MaterialLoaderOutput::IsBuiltInMaterial, &_MaterialLoaderOutput::SetBuiltIn)
        .def_property("material_sub_identifier", &_MaterialLoaderOutput::GetMaterialSubIdentifier, &_MaterialLoaderOutput::SetMaterialSubIdentifier)
        .def(
            "__len__",
            [](const _MaterialLoaderOutput& self)
            {
                return self.Size();
            }
        );

    py::class_<_MaterialDescription, std::shared_ptr<_MaterialDescription>>(m, "MaterialDescription", R"(
        Object to hold of material description after parsing from raw assets.

        A material description is a group of named properties.
    )")
        .def_property_readonly("name", &_MaterialDescription::GetName)
        .def_property_readonly("class_id", &_MaterialDescription::GetClassId)
        .def(
            "__getitem__",
            [](const _MaterialDescription& self, size_t i)
            {
                if (i >= self.Size())
                {
                    throw py::index_error();
                }
                return self.GetProperty(i);
            }
        )
        .def(
            "__len__",
            [](const _MaterialDescription& self)
            {
                return self.Size();
            }
        );

    py::class_<_Content, std::shared_ptr<_Content>>(
        m,
        "Content",
        R"(
            Object to hold file content.

            Use python std :obj:`memoryview` to access it. For example:
        )",
        py::buffer_protocol()
    )
        .def(
            "__getitem__",
            [](const _Content& self, size_t i)
            {
                if (i >= self.getSize())
                {
                    throw py::index_error();
                }
                return self.buffer()[i];
            }
        )
        .def(
            "__setitem__",
            [](_Content& self, size_t i, char v)
            {
                if (i >= self.getSize())
                {
                    throw py::index_error();
                }
                self.buffer()[i] = v;
            }
        )
        .def("assign", &_Content::assign)
        .def(
            "__len__",
            [](const _Content& self)
            {
                return self.getSize();
            }
        )
        .def_buffer(
            [](_Content& self) -> py::buffer_info
            {
                return py::buffer_info(self.buffer(), self.getSize());
            }
        );

    py::enum_<OmniConverterStatus>(m, "OmniConverterStatus")
        .value("OK", OmniConverterStatus::OK)
        .value("IN_PROGRESS", OmniConverterStatus::IN_PROGRESS)
        .value("CANCELLED", OmniConverterStatus::CANCELLED)
        .value("UNSUPPORTED_IMPORT_FORMAT", OmniConverterStatus::UNSUPPORTED_IMPORT_FORMAT)
        .value("INCOMPLETE_IMPORT_FORMAT", OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT)
        .value("FILE_NOT_EXISTED", OmniConverterStatus::FILE_NOT_EXISTED)
        .value("FILE_READ_ERROR", OmniConverterStatus::FILE_READ_ERROR)
        .value("FILE_WRITE_ERROR", OmniConverterStatus::FILE_WRITE_ERROR)
        .value("DIRECTORY_CREATE_FAILED", OmniConverterStatus::DIRECTORY_CREATE_FAILED)
        .value("UNSUPPORTED_EXPORT_FORMAT", OmniConverterStatus::UNSUPPORTED_EXPORT_FORMAT)
        .value("UNKNOWN", OmniConverterStatus::UNKNOWN)
        .export_values();

#define STR(str) #str
#define EXPORT_FUNCTION(name) m.def(STR(name), &name);

    py::class_<OmniConverterBlob, std::unique_ptr<OmniConverterBlob, py::nodelete>>(m, "OmniConverterBlob");
    py::class_<OmniConverterFuture, std::unique_ptr<OmniConverterFuture, py::nodelete>>(m, "OmniConverterFuture");

    ///////////////////////////////////////////
    // functions binding
    m.def(
        "omniConverterSetFileCallbacks",
        [](py::object makeDirsCallback,
           py::object binaryWriteCallback,
           py::object pathExistsCallback,
           py::object readCallback,
           py::object layerWriteCallback,
           py::object fileCopyCallback)
        {
            gPythonMakeDirsCallback = makeDirsCallback;
            gPythonBinaryWriteCallback = binaryWriteCallback;
            gPythonLayerWriteCallback = layerWriteCallback;
            gPythonPathExistsCallback = pathExistsCallback;
            gPythonReadCallback = readCallback;
            gPythonFileCopyCallback = fileCopyCallback;
            OmniConverterMakeDirs converterMkdir = nullptr;
            OmniConverterReader converterRead = nullptr;
            OmniConverterBinaryWriter converterBinaryWrite = nullptr;
            OmniConverterUsdWriter converterLayerWrite = nullptr;
            OmniConverterPathExists converterPathExists = nullptr;
            OmniConverterFileCopy converterFileCopy = nullptr;
            static auto makeDirsCallbackWrapper = [](const char* path)
            {
                py::gil_scoped_acquire gil;
                bool success = true;
                try
                {
                    auto ret = gPythonMakeDirsCallback(path);
                    success = ret.cast<bool>();
                }
                catch (...)
                {
                    return false;
                }

                return success;
            };

            static auto binaryWriteCallbackWrapper = [](const char* path, OmniConverterBlob* blob)
            {
                py::gil_scoped_acquire gil;
                bool success = true;
                try
                {
                    auto pythonContent = std::make_shared<_Content>(blob);
                    auto ret = gPythonBinaryWriteCallback(path, pythonContent);
                    success = ret.cast<bool>();
                }
                catch (...)
                {
                    return false;
                }

                return success;
            };

            static auto layerWriteCallbackWrapper = [](const char* path, const char* layerIdentifier)
            {
                py::gil_scoped_acquire gil;
                bool success = true;
                try
                {
                    auto ret = gPythonLayerWriteCallback(path, layerIdentifier);
                    success = ret.cast<bool>();
                }
                catch (...)
                {
                    return false;
                }

                return success;
            };

            static auto fileCopyCallbackWrapper = [](const char* targetPath, const char* sourcePath)
            {
                py::gil_scoped_acquire gil;
                bool success = true;
                try
                {
                    auto ret = gPythonFileCopyCallback(targetPath, sourcePath);
                    success = ret.cast<bool>();
                }
                catch (...)
                {
                    return false;
                }

                return success;
            };

            static auto pathExistsCallbackWrapper = [](const char* path)
            {
                py::gil_scoped_acquire gil;
                bool success = true;
                try
                {
                    auto ret = gPythonPathExistsCallback(path);
                    success = ret.cast<bool>();
                }
                catch (...)
                {
                    return false;
                }

                return success;
            };

            static auto readCallbackWrapper = [](const char* path, OmniConverterBlob* blob)
            {
                py::gil_scoped_acquire gil;
                bool success = true;
                try
                {
                    auto pythonContent = std::make_shared<_Content>(nullptr);
                    auto ret = gPythonReadCallback(path, pythonContent);
                    pythonContent->moveTo(blob);
                    success = ret.cast<bool>();
                }
                catch (...)
                {
                    return false;
                }

                return success;
            };

            if (!makeDirsCallback.is_none())
            {
                converterMkdir = makeDirsCallbackWrapper;
            }

            if (!readCallback.is_none())
            {
                converterRead = readCallbackWrapper;
            }

            if (!binaryWriteCallback.is_none())
            {
                converterBinaryWrite = binaryWriteCallbackWrapper;
            }

            if (!layerWriteCallback.is_none())
            {
                converterLayerWrite = layerWriteCallbackWrapper;
            }

            if (!pathExistsCallback.is_none())
            {
                converterPathExists = pathExistsCallbackWrapper;
            }

            if (!fileCopyCallback.is_none())
            {
                converterFileCopy = fileCopyCallbackWrapper;
            }

            omniConverterSetFileCallbacks(
                converterMkdir,
                converterRead,
                converterBinaryWrite,
                converterPathExists,
                converterLayerWrite,
                converterFileCopy
            );
        },
        R"(Set file callbacks to overwrite file IO.)",
        py::arg("make_dirs_fn"),
        py::arg("binary_write_fn"),
        py::arg("path_exists_fn"),
        py::arg("read_file_fn"),
        py::arg("layer_write_fn"),
        py::arg("file_copy_fn") = py::cast<py::none>(Py_None)
    );

    m.def(
        "omniConverterSetLogCallback",
        [](py::object logCallback)
        {
            gPythonLogCallback = logCallback;
            static auto logsCallbackWrapper = [](const char* message)
            {
                py::gil_scoped_acquire gil;
                CallPythonLogCallback(message);
            };

            if (!logCallback.is_none())
            {
                omniConverterSetLogCallback(logsCallbackWrapper);
            }
            else
            {
                gPythonLogCallback = py::object();
                omniConverterSetLogCallback(nullptr);
            }
        }
    );

    m.def(
        "omniConverterSetProgressCallback",
        [](py::object progressCallback)
        {
            gPythonProgressCallback = progressCallback;
            static auto progressCallbackWrapper = [](OmniConverterFuture* future, uint32_t progress, uint32_t total)
            {
                py::gil_scoped_acquire gil;
                try
                {
                    gPythonProgressCallback(future, progress, total);
                }
                catch (...)
                {
                }
            };

            if (!progressCallback.is_none())
            {
                omniConverterSetProgressCallback(progressCallbackWrapper);
            }
            else
            {
                omniConverterSetProgressCallback(nullptr);
            }
        }
    );

    m.def(
        "omniConverterSetMaterialCallback",
        [](py::object pyMaterialLoader)
        {
            gPythonMaterialLoader = pyMaterialLoader;
            static auto materialLoader = [](OmniConverterFuture* future, OmniConverterMaterialDescription* material)
            {
                py::gil_scoped_acquire gil;
                std::shared_ptr<_MaterialDescription> materialDescription = std::make_shared<_MaterialDescription>(material);
                try
                {
                    py::object pyOutput = gPythonMaterialLoader(future, materialDescription);
                    if (!pyOutput.is_none())
                    {
                        _MaterialLoaderOutput output = pyOutput.cast<_MaterialLoaderOutput>();
                        omniConverteSetMaterialFilePath(
                            material,
                            output.GetMaterialFilePath().c_str(),
                            output.GetMaterialSubIdentifier().c_str(),
                            output.IsBuiltInMaterial()
                        );
                        for (size_t i = 0; i < output.Size(); i++)
                        {
                            OmniConverterMDLPropertyMetadata metadata;
                            auto property = output.GetProperty(i);
                            metadata.groupName = property->GetGroupName().c_str();
                            metadata.displayName = property->GetDisplayName().c_str();
                            metadata.detailType = property->GetDetailedValueType();
                            metadata.colorSpace = property->GetColorSpace().c_str();
                            metadata.singlePrecision = property->IsSinglePrecision();

                            auto valueType = property->GetValueType();
                            if (valueType == OMNI_CONVERTER_VALUE_TYPE_UNDEFINED)
                            {
                                continue;
                            }

                            OmniConverterMDLPropertyMetadata::_Value defaultValue;
                            if (ToValue(property->GetDefaultValue(), defaultValue))
                            {
                                metadata.hasDefaultValue = true;
                                metadata.defaultValue = defaultValue;
                            }
                            else
                            {
                                metadata.hasDefaultValue = false;
                            }

                            OmniConverterMDLPropertyMetadata::_Value minValue;
                            // Min value cannot be bool type
                            if (ToValue(property->GetMinValue(), minValue))
                            {
                                metadata.hasMinValue = true;
                                metadata.minValue = minValue;
                            }
                            else
                            {
                                metadata.hasMinValue = false;
                            }

                            OmniConverterMDLPropertyMetadata::_Value maxValue;
                            // Max value cannot be bool type
                            if (ToValue(property->GetMaxValue(), maxValue))
                            {
                                metadata.hasMaxValue = true;
                                metadata.maxValue = maxValue;
                            }
                            else
                            {
                                metadata.hasMaxValue = false;
                            }

                            // TODO: Type check between detailed type and real value type
                            // to make sure they are matched.

                            auto outputProperty = omniConverterCreateOutputProperty(material, property->GetName().c_str(), &metadata);
                            omniConverterSetIsTextureProperty(outputProperty, property->IsTextureProperty());
                            omniConverterSetTextureTranslation(outputProperty, property->GetTextureTranslation());
                            omniConverterSetTextureScale(outputProperty, property->GetTextureTranslation());
                            switch (valueType)
                            {
                                case OMNI_CONVERTER_VALUE_TYPE_BOOL:
                                {
                                    const auto& boolValue = py::bool_(property->GetValue());
                                    omniConverterSetBool(outputProperty, bool(boolValue));
                                    break;
                                }
                                case OMNI_CONVERTER_VALUE_TYPE_INT32:
                                {
                                    const auto& intValue = py::int_(property->GetValue());
                                    omniConverterSetInt32(outputProperty, int(intValue));
                                    break;
                                }
                                case OMNI_CONVERTER_VALUE_TYPE_DOUBLE:
                                {
                                    const auto& doubleValue = py::float_(property->GetValue());
                                    omniConverterSetDouble(outputProperty, double(doubleValue));
                                    break;
                                }
                                case OMNI_CONVERTER_VALUE_TYPE_DOUBLE2:
                                {
                                    const auto& tuple2Value = py::tuple(property->GetValue());
                                    omniConverterSetDouble2(
                                        outputProperty,
                                        { double(py::float_(tuple2Value[0])), double(py::float_(tuple2Value[1])) }
                                    );
                                    break;
                                }
                                case OMNI_CONVERTER_VALUE_TYPE_DOUBLE3:
                                {
                                    const auto& tuple3Value = py::tuple(property->GetValue());
                                    omniConverterSetDouble3(
                                        outputProperty,
                                        { double(py::float_(tuple3Value[0])), double(py::float_(tuple3Value[1])), double(py::float_(tuple3Value[2])) }
                                    );
                                    break;
                                }
                                case OMNI_CONVERTER_VALUE_TYPE_DOUBLE4:
                                {
                                    const auto& tuple4Value = py::tuple(property->GetValue());
                                    omniConverterSetDouble4(
                                        outputProperty,
                                        { double(py::float_(tuple4Value[0])),
                                          double(py::float_(tuple4Value[1])),
                                          double(py::float_(tuple4Value[2])),
                                          double(py::float_(tuple4Value[3])) }
                                    );
                                    break;
                                }
                                case OMNI_CONVERTER_VALUE_TYPE_DOUBLE9:
                                {
                                    const auto& tuple9Value = py::tuple(property->GetValue());
                                    omniConverterSetDouble9(
                                        outputProperty,
                                        { double(py::float_(tuple9Value[0])),
                                          double(py::float_(tuple9Value[1])),
                                          double(py::float_(tuple9Value[2])),
                                          double(py::float_(tuple9Value[3])),
                                          double(py::float_(tuple9Value[4])),
                                          double(py::float_(tuple9Value[5])),
                                          double(py::float_(tuple9Value[6])),
                                          double(py::float_(tuple9Value[7])),
                                          double(py::float_(tuple9Value[8])) }
                                    );
                                    break;
                                }
                                case OMNI_CONVERTER_VALUE_TYPE_DOUBLE16:
                                {
                                    const auto& tuple16Value = py::tuple(property->GetValue());
                                    omniConverterSetDouble16(
                                        outputProperty,
                                        { double(py::float_(tuple16Value[0])),
                                          double(py::float_(tuple16Value[1])),
                                          double(py::float_(tuple16Value[2])),
                                          double(py::float_(tuple16Value[3])),
                                          double(py::float_(tuple16Value[4])),
                                          double(py::float_(tuple16Value[5])),
                                          double(py::float_(tuple16Value[6])),
                                          double(py::float_(tuple16Value[7])),
                                          double(py::float_(tuple16Value[8])),
                                          double(py::float_(tuple16Value[9])),
                                          double(py::float_(tuple16Value[10])),
                                          double(py::float_(tuple16Value[11])),
                                          double(py::float_(tuple16Value[12])),
                                          double(py::float_(tuple16Value[13])),
                                          double(py::float_(tuple16Value[14])),
                                          double(py::float_(tuple16Value[15])) }
                                    );
                                    break;
                                }
                                case OMNI_CONVERTER_VALUE_TYPE_STRING:
                                {
                                    const auto& stringValue = std::string(py::str(property->GetValue()));
                                    omniConverterSetString(outputProperty, { stringValue.c_str(), stringValue.size() });
                                    break;
                                }
                                default:
                                    break;
                            }
                        }
                    }
                    else
                    {
                        return false;
                    }
                }
                catch (py::error_already_set& e)
                {
                    CallPythonLogCallback(e.what());
                    return false;
                }
                catch (...)
                {
                    CallPythonLogCallback("Failed to load material because of unknown error.");
                    return false;
                }

                return true;
            };

            if (!pyMaterialLoader.is_none())
            {
                omniConverterSetMaterialCallback(materialLoader);
            }
            else
            {
                omniConverterSetMaterialCallback(nullptr);
            }
        }
    );

    EXPORT_FUNCTION(omniConverterCreateUSD);
    EXPORT_FUNCTION(omniConverterCreateAsset);
    EXPORT_FUNCTION(omniConverterCheckFutureStatus);
    EXPORT_FUNCTION(omniConverterGetFutureDetailedError);
    EXPORT_FUNCTION(omniConverterCancelFuture);
    EXPORT_FUNCTION(omniConverterReleaseFuture);
    EXPORT_FUNCTION(omniConverterSetCacheFolder);

    m.def(
        "omniConverterPopulateMaterials",
        [](const std::string& assetPath)
        {
            std::vector<std::shared_ptr<_MaterialDescription>> materialDescriptions;
            std::shared_ptr<OmniConverterAssetHandle> assetHandle = std::shared_ptr<OmniConverterAssetHandle>(
                omniConverterOpenAsset(assetPath.c_str()),
                omniConverterCloseAsset
            );
            size_t numMaterials = omniConverterGetNumMaterials(assetHandle.get());
            for (size_t i = 0; i < numMaterials; i++)
            {
                auto materialDescription = omniConverterGetMaterialDescription(assetHandle.get(), i);
                materialDescriptions.push_back(std::make_shared<_MaterialDescription>(materialDescription));
            }

            return materialDescriptions;
        }
    );

}
