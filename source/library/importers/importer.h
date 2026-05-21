// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../converter_future.h"
#include "../stage.h"
#include "../usd_convert_asset_internal.h"

class Importer
{
public:

    virtual ~Importer(){};
    virtual std::string ComputeHash(const OmniFutureThreadContextPtr& context)
    {
        return {};
    }
    virtual StagePtr ImportStage(const OmniFutureThreadContextPtr& context, OmniConverterStatus& status, std::string& detailedError) = 0;

    void SetCurveSubdivisionNumber(const uint32_t curveSubdivisionNumber)
    {
        mCurveSubdivisionNumber = curveSubdivisionNumber;
    }

protected:

    bool HasAlphaChannel(const OmniFutureThreadContextPtr& context, const StagePtr& stage, size_t imageIndex) const;

    uint32_t mCurveSubdivisionNumber = 1;
};

std::shared_ptr<Importer> CreateImporter(const OmniConverterContext& context);
