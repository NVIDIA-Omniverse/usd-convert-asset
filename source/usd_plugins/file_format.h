// SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include <pxr/base/tf/staticTokens.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/fileFormat.h>

#include <iosfwd>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

#define OMNIASSET_FILE_FORMAT_TOKENS ((Id, "omniasset"))((Version, "1.0"))((Target, "usd"))

TF_DECLARE_PUBLIC_TOKENS(OmniAssetFileFormatTokens, OMNIASSET_FILE_FORMAT_TOKENS);

TF_DECLARE_WEAK_AND_REF_PTRS(OmniAssetFileFormat);

///
/// Universal file format
class OmniAssetFileFormat : public SdfFileFormat
{
public:

    // SdfFileFormat overrides.
    virtual bool CanRead(const std::string& file) const override;
    virtual bool Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const override;
    virtual bool ReadFromString(SdfLayer* layer, const std::string& str) const override;

    virtual bool WriteToString(const SdfLayer& layer, std::string* str, const std::string& comment = std::string()) const override;
    virtual bool WriteToStream(const SdfSpecHandle& spec, std::ostream& out, size_t indent) const override;

protected:

    SDF_FILE_FORMAT_FACTORY_ACCESS;

    virtual ~OmniAssetFileFormat();
    OmniAssetFileFormat();

private:

    SdfFileFormatConstPtr _usda;
};

PXR_NAMESPACE_CLOSE_SCOPE
