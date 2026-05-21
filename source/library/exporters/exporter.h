// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../converter_future.h"
#include "../stage.h"
#include "../usd_convert_asset_internal.h"

class Exporter
{
public:

    Exporter(const OmniFutureThreadContextPtr& context) : mExportContext(context)
    {
    }

    virtual ~Exporter(){};
    virtual OmniConverterStatus Export(const StagePtr& stage, std::string& detailedError) = 0;

protected:

    void Log(const std::string& message);
    bool UploadContent(const std::string& path, OmniConverterBlob* blob);
    size_t GetTotalExportSteps(const StagePtr& stage) const;

protected:

    OmniFutureThreadContextPtr mExportContext;
};

std::shared_ptr<Exporter> CreateExporter(const OmniFutureThreadContextPtr& context);
