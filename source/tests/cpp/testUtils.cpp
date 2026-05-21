// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0

#define DOCTEST_CONFIG_IMPLEMENT // we will be supplying main()

#include "../../library/usd_convert_asset_internal.h"
#include "../../library/utils/path_utils.h"
#include "usd_convert_asset.h"

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>

#include <algorithm>
#include <cctype>
#include <doctest/doctest.h>
#include <experimental/filesystem>
#include <string>
#include <vector>

#ifdef __aarch64__
bool supportFbxConversion = false;
#else
bool supportFbxConversion = true;
#endif
const char* rootDirPath = getenv("REPO_ROOT_PATH");

int main(int argc, char** argv)
{
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}

template <typename PrimType>
bool checkPrimExistsWithName(const std::vector<PXR_NS::UsdPrim>& prims, const std::string& name, const std::string& type)
{
    return std::find_if(
               prims.begin(),
               prims.end(),
               [&name, &type](const PXR_NS::UsdPrim& prim)
               {
                   if (!prim.IsA<PrimType>())
                   {
                       return false;
                   }
                   if (prim.GetTypeName() != type)
                   {
                       return false;
                   }
                   if (prim.GetName() != name)
                   {
                       return false;
                   }
                   return true;
               }
           ) != prims.end();
}

template <typename PrimType>
bool checkPrimExistsWithDisplay(const std::vector<PXR_NS::UsdPrim>& prims, const std::string& name, const std::string& type)
{
    return std::find_if(
               prims.begin(),
               prims.end(),
               [&name, &type](const PXR_NS::UsdPrim& prim)
               {
                   if (!prim.IsA<PrimType>())
                   {
                       return false;
                   }
                   if (prim.GetTypeName() != type)
                   {
                       return false;
                   }
                   if (prim.GetName() != name)
                   {
                       auto displayNameToken = PXR_NS::TfToken("displayName");
                       if (!prim.HasAuthoredMetadata(displayNameToken))
                       {
                           return false;
                       }
                       std::string displayName;
                       if (!prim.GetMetadata<std::string>(displayNameToken, &displayName))
                       {
                           return false;
                       }
                       if (displayName != name)
                       {
                           return false;
                       }
                   }
                   return true;
               }
           ) != prims.end();
}


OmniConverterStatus omniConverterWaitFuture(OmniConverterFuture* future)
{
    auto status = OmniConverterStatus::IN_PROGRESS;
    while (true)
    {
        status = omniConverterCheckFutureStatus(future);
        if (status != OmniConverterStatus::IN_PROGRESS)
        {
            break;
        }
    }
    return status;
};

std::string getFilePathWithExtension(const std::string& inputPath, const std::string& outputExtension)
{
    auto base = PathUtils::GetDirName(inputPath);
    auto filename = PathUtils::GetFileName(inputPath, false);
    return base + "/" + filename + outputExtension;
}

bool fileExtensionEquals(const std::string& inputPath, const std::string& extension)
{
    auto ext = PathUtils::GetExtension(inputPath);
    return ext == extension;
}

std::vector<std::string> getSupportedInputPathsIn(const std::string& folder)
{
    std::set<std::string> extensions = { "glb", "gltf", "obj", "stl" };
    if (supportFbxConversion)
    {
        extensions.insert("fbx");
    }
    std::vector<std::string> paths;
    for (const auto& entry : std::experimental::filesystem::recursive_directory_iterator(folder))
    {
        std::string filePath = entry.path().string();
        std::string extension = PathUtils::GetExtension(filePath);

        if (extensions.find(extension) != extensions.end())
        {
            paths.push_back(filePath);
        }
    }
    return paths;
}

std::vector<std::string> getSupportedInputPathsIn(std::vector<std::string>& folders)
{
    std::vector<std::string> allFiles;
    for (int i = 0; i < folders.size(); ++i)
    {
        std::vector<std::string> files = getSupportedInputPathsIn(folders[i]);
        allFiles.insert(allFiles.end(), files.begin(), files.end());
    }
    return allFiles;
}

TEST_CASE("Material interfaces test")
{
    if (!supportFbxConversion)
    {
        return;
    }
    std::shared_ptr<OmniConverterMaterialDescription> material = std::shared_ptr<OmniConverterMaterialDescription>(
        omniConverterCreateMaterialDescription("FbxSurfaceMaterial"),
        omniConverterReleaseMaterialDescription
    );
    CHECK(std::string("FbxSurfaceMaterial") == omniConverterGetMaterialName(material.get()));
}

TEST_CASE("Path utils test")
{
    CHECK(PathUtils::AbsPath("omniverse://localhost/test.usd") == "omniverse://localhost/test.usd");
    CHECK(PathUtils::IsAbsolutePath("omniverse://localhost/test.usd"));
    CHECK(!PathUtils::IsAbsolutePath("../localhost/test.usd"));

    std::string relativePath;
#if defined(_WIN32)
    PathUtils::ComputeRelativePath("c:/test/path.usd", "c:/test/subdir/a.usd", relativePath);
    CHECK(relativePath == "./../path.usd");

    PathUtils::ComputeRelativePath("c:/test/path.usd", "c:\\test\\subdir\\a.usd", relativePath);
    CHECK(relativePath == "./../path.usd");

    PathUtils::ComputeRelativePath("c:/test/path.usd", "c:/test/subdir/", relativePath);
    CHECK(relativePath == "./../path.usd");

    PathUtils::ComputeRelativePath("c:/test/path.usd", "e:/test/subdir/a.usd", relativePath);
    CHECK(relativePath == "c:/test/path.usd");
#endif

    PathUtils::ComputeRelativePath("/test/path.usd", "/test/subdir/a.usd", relativePath);
    CHECK(relativePath == "./../path.usd");

    PathUtils::ComputeRelativePath("omniverse://test/path.usd", "omniverse://test/subdir/a.usd", relativePath);
    CHECK(relativePath == "./../path.usd");

    PathUtils::ComputeRelativePath("omniverse://test/path.usd", "e:/test/subdir/a.usd", relativePath);
    CHECK(relativePath == "omniverse://test/path.usd");

    PathUtils::ComputeRelativePath("omniverse://test/path.usd", "/test/subdir/a.usd", relativePath);
    CHECK(relativePath == "omniverse://test/path.usd");
    CHECK(PathUtils::IsOmniversePath(relativePath));
    std::string urlScheme = PathUtils::GetUrlScheme(relativePath);
    CHECK(urlScheme == "omniverse");

    std::string joinedPath = PathUtils::JoinPaths({});
    CHECK(joinedPath == "");

    joinedPath = PathUtils::JoinPaths({ "test", "path.usd" });
    CHECK(joinedPath == "test/path.usd");

    // comment out as not used in the code at all
    // std::string modulePath = PathUtils::GetModulePath();
    // CHECK(modulePath.size() > 1);

    CHECK(PathUtils::ToMimeType("image.png") == "image/png");
    CHECK(PathUtils::ToMimeType("image.bmp") == "image/bmp");
    CHECK(PathUtils::ToMimeType("image.gif") == "image/gif");
    CHECK(PathUtils::ToMimeType("image.invalid") == "");

    CHECK(PathUtils::MimeToExt("image/png") == "png");
    CHECK(PathUtils::MimeToExt("image/bmp") == "bmp");
    CHECK(PathUtils::MimeToExt("image/gif") == "gif");
    CHECK(PathUtils::MimeToExt("image/invalid") == "");
}

TEST_CASE("OmniConverterMaterialProperty tests")
{
    OmniConverterMaterialProperty prop;
    prop.isTextureProperty = true;
    omniConverterSetIsTextureProperty(&prop, false);
    CHECK(!prop.isTextureProperty);
    CHECK(!omniConverterIsTextureProperty(&prop));

    OmniConverterDouble2 double2 = omniConverterGetTextureTranslation(&prop);
    double2.value[0] = 1.0;
    double2.value[1] = 2.0;
    omniConverterSetTextureTranslation(&prop, double2);
    CHECK(prop.textureTranslation[0] == 1.0);
    CHECK(prop.textureTranslation[1] == 2.0);

    double2 = omniConverterGetTextureScale(&prop);
    double2.value[0] = 1.0;
    double2.value[1] = 2.0;
    omniConverterSetTextureScale(&prop, double2);
    CHECK(prop.textureScale[0] == 1.0);
    CHECK(prop.textureScale[1] == 2.0);

    omniConverterSetBool(&prop, false);
    CHECK(!prop.value.boolValue);
    CHECK(prop.valueType == OMNI_CONVERTER_VALUE_TYPE_BOOL);

    omniConverterSetInt32(&prop, 1);
    CHECK(prop.value.intValue == 1);
    CHECK(prop.valueType == OMNI_CONVERTER_VALUE_TYPE_INT32);

    omniConverterSetDouble(&prop, 2.0);
    CHECK(prop.value.doubleValue == 2.0);
    CHECK(prop.valueType == OMNI_CONVERTER_VALUE_TYPE_DOUBLE);

    omniConverterSetDouble2(&prop, double2);
    CHECK(prop.value.double2Value[0] == 1.0);
    CHECK(prop.value.double2Value[1] == 2.0);
    CHECK(prop.valueType == OMNI_CONVERTER_VALUE_TYPE_DOUBLE2);

    OmniConverterDouble3 double3;
    double3.value[0] = 1.0;
    double3.value[1] = 2.0;
    double3.value[2] = 3.0;
    omniConverterSetDouble3(&prop, double3);
    CHECK(prop.value.double3Value[0] == 1.0);
    CHECK(prop.value.double3Value[1] == 2.0);
    CHECK(prop.value.double3Value[2] == 3.0);
    CHECK(prop.valueType == OMNI_CONVERTER_VALUE_TYPE_DOUBLE3);

    OmniConverterDouble4 double4;
    double4.value[0] = 1.0;
    double4.value[1] = 2.0;
    double4.value[2] = 3.0;
    double4.value[3] = 4.0;
    omniConverterSetDouble4(&prop, double4);
    CHECK(prop.value.double4Value[0] == 1.0);
    CHECK(prop.value.double4Value[1] == 2.0);
    CHECK(prop.value.double4Value[2] == 3.0);
    CHECK(prop.value.double4Value[3] == 4.0);
    CHECK(prop.valueType == OMNI_CONVERTER_VALUE_TYPE_DOUBLE4);

    OmniConverterDouble9 double9;
    omniConverterSetDouble9(&prop, double9);
    CHECK(prop.valueType == OMNI_CONVERTER_VALUE_TYPE_DOUBLE9);
    double9 = omniConverterToDouble9(&prop);
    CHECK(double9.value[0] == 0.0);

    OmniConverterDouble16 double16;
    omniConverterSetDouble16(&prop, double16);
    CHECK(prop.valueType == OMNI_CONVERTER_VALUE_TYPE_DOUBLE16);
    double16 = omniConverterToDouble16(&prop);
    CHECK(double16.value[0] == 0.0);

    std::string str = "test";
    omniConverterSetString(&prop, { str.c_str(), str.size() });
    CHECK(prop.valueType == OMNI_CONVERTER_VALUE_TYPE_STRING);
}
