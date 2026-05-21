// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "usd_convert_asset_internal.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

struct OmniConverterFuture
{
public:

    class FutureThreadContext
    {
        friend struct OmniConverterFuture;

    public:

        FutureThreadContext()
        {
        }

        bool MaterialLoader(OmniConverterMaterialDescription* material);
        void StartProgress(uint32_t totalSteps);
        void IncrementProgress(uint32_t steps = 1);
        void FinishProgress();
        bool IsExited();

        OmniConverterContext converterContext;

    private:

        void SetStatus(OmniConverterStatus status);
        OmniConverterStatus GetStatus();

        void SetDetailedError(const std::string& error);
        const std::string& GetDetailedError();

        void Exit();

        std::mutex mStatusMutex;
        OmniConverterStatus mCurrentStatus = OmniConverterStatus::OK;
        std::string mDetailedError;

        OmniConverterFuture* mThisFuture = nullptr;
        uint32_t mCurrentProgress = 0;
        uint32_t mTotalSteps = 0;

        std::mutex mExitMutex;
        bool mExited = false;
    };

public:

    OmniConverterFuture(const OmniConverterContext& context)
    {
        mContext = std::shared_ptr<FutureThreadContext>(new FutureThreadContext());
        mContext->converterContext = context;
    }
    ~OmniConverterFuture();

    void Start();
    void Stop();
    OmniConverterStatus GetStatus() const;
    const std::string& GetDetailedError() const;

private:

    static void Convert(std::shared_ptr<FutureThreadContext> context);

    std::shared_ptr<FutureThreadContext> mContext;
};

typedef std::shared_ptr<OmniConverterFuture::FutureThreadContext> OmniFutureThreadContextPtr;
