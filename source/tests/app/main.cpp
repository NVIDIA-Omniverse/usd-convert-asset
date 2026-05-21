// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "usd_convert_asset.h"

#include <cstring>
#include <experimental/filesystem>
#include <fstream>
#include <iostream>
#include <time.h>
using namespace std;
namespace fs = std::experimental::filesystem;

const char* gHelperStr =
    "Help: test_executable.exe [--previewsurface] asset_path [usd_path]\n    "
    "--previewsurface: Optional. If it's set, it will export material surface as previewsurface for USD import.\n     "
    "asset_path: Must. The asset to be converted.\n     "
    "usd_path: Optional. If usd_path is not provided, the default output usd will be under the same path as asset_path"
    " with .usda extension.";

std::string StatusToString(OmniConverterStatus status)
{
    switch (status)
    {
        case OmniConverterStatus::OK:
            return "OK";
        case OmniConverterStatus::CANCELLED:
            return "Cancelled";
        case OmniConverterStatus::IN_PROGRESS:
            return "In Progress";
        case OmniConverterStatus::UNSUPPORTED_IMPORT_FORMAT:
            return "Unsupported Format";
        case OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT:
            return "Incomplete File";
        case OmniConverterStatus::FILE_READ_ERROR:
            return "Asset Not Found";
        case OmniConverterStatus::FILE_WRITE_ERROR:
            return "Output Path Cannot be Opened";
        case OmniConverterStatus::UNKNOWN:
            return "Unknown";
        default:
            return "Unknown";
    }
}

int main(int argc, char** argv)
{
    int flags = OMNI_CONVERTER_FLAGS_SINGLE_MESH_FILE | OMNI_CONVERTER_FLAGS_DISABLE_INSTANCING | OMNI_CONVERTER_FLAGS_FBX_BAKING_SCALES_INTO_MESH;
    int curveSubdivision = -1;
    string assetPath("");
    string outputAssetPath("");
    if (argc < 2)
    {
        cout << "ERROR: Asset path should be provided." << endl;
        cout << gHelperStr << endl;
        return -1;
    }
    else
    {
        bool inputAssetSet = false;
        if (argc > 2)
        {
            for (int i = 1; i < argc; i++)
            {
                    if (strncmp(argv[i], "--curveSubdivide", 16) == 0)
                    {
                        if (i + 1 < argc)
                        {
                            curveSubdivision = std::min(atoi(argv[i + 1]), 256);
                            i++;
                        }
                    }
                    else if (!inputAssetSet)
                    {
                        assetPath = argv[i];
                        inputAssetSet = true;
                    }
                    else
                    {
                        outputAssetPath = argv[i];
                        break;
                    }
            }
        }
    }

    if (outputAssetPath.empty())
    {
        outputAssetPath = assetPath;
        size_t pos = outputAssetPath.find_last_of('.');
        if (pos == std::string::npos)
        {
            cout << "ERROR: Valid output path should be provided." << std::endl;
            return -1;
        }
        outputAssetPath.replace(pos, outputAssetPath.length() - pos, ".usda");
    }

    omniConverterSetLogCallback(
        [](const char* message)
        {
            cout << message << endl;
        }
    );

    omniConverterSetProgressCallback(
        [](OmniConverterFuture* future, uint32_t progress, uint32_t total)
        {
            cout << "Progress: " << progress << "/" << total << endl;
        }
    );

    clock_t startTime, endTime;
    startTime = clock();
    auto future = curveSubdivision > 0 ? omniConverterCreateCurveAsset(assetPath.c_str(), outputAssetPath.c_str(), flags, curveSubdivision) :
                                         omniConverterCreateAsset(assetPath.c_str(), outputAssetPath.c_str(), flags);
    OmniConverterStatus status = OmniConverterStatus::OK;
    while (true)
    {
        status = omniConverterCheckFutureStatus(future);
        if (status != OmniConverterStatus::IN_PROGRESS)
        {
            break;
        }
    }
    endTime = clock();
    cout << "Total Time : " << (double)(endTime - startTime) / CLOCKS_PER_SEC << "s" << endl;

    if (status == OmniConverterStatus::OK)
    {
        cout << "Asset convert sucessfully." << endl;
    }
    else
    {
        cout << "Asset convert failed with error status: " << StatusToString(status) << endl;
    }

    omniConverterReleaseFuture(future);
    return (int)status;
}
