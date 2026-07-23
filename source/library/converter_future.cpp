// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "converter_future.h"

#include "exporters/exporter.h"
#include "importers/importer.h"
#include "pxr_includes.h"
#include "utils/utils.h"

#include <chrono>
#include <future>
#include <iostream>
#include <thread>


OmniConverterFuture::~OmniConverterFuture()
{
    Stop();
}

void OmniConverterFuture::Start()
{
    mContext->SetStatus(OmniConverterStatus::IN_PROGRESS);
    mContext->mThisFuture = this;
    std::thread t(OmniConverterFuture::Convert, mContext);
    t.detach();
}

void OmniConverterFuture::Stop()
{
    mContext->FinishProgress();
    mContext->Exit();
    auto Status = GetStatus();
    if (Status == OmniConverterStatus::IN_PROGRESS)
    {
        mContext->SetStatus(OmniConverterStatus::CANCELLED);
    }
}

OmniConverterStatus OmniConverterFuture::GetStatus() const
{
    return mContext->GetStatus();
}

const std::string& OmniConverterFuture::GetDetailedError() const
{
    return mContext->GetDetailedError();
}

void OmniConverterFuture::Convert(std::shared_ptr<OmniConverterFuture::FutureThreadContext> context)
{
    OmniConverterStatus status = OmniConverterStatus::OK;
    std::string detailedError;

    if (context->IsExited())
    {
        context->converterContext.Log("Asset convert cancelled...");
        context->SetStatus(OmniConverterStatus::CANCELLED);
        return;
    }

    bool fileNotExisted = false;
    const auto& importAssetPath = context->converterContext.GetImportAssetPath();
    const auto& outputAssetPath = context->converterContext.GetOutputAssetPath();
    if (context->converterContext.IsInMemoryImport() && context->converterContext.IsInMemoryOutput())
    {
        context->converterContext.Log("Asset convert failed as both input and output paths are anonymous.");
        context->SetStatus(OmniConverterStatus::UNSUPPORTED_EXPORT_FORMAT);
        return;
    }

    if (context->converterContext.IsInMemoryImport())
    {
        auto layer = PXR_NS::SdfLayer::Find(importAssetPath);
        if (!layer)
        {
            fileNotExisted = true;
        }
    }
    else if (!context->converterContext.IsPathExisted(importAssetPath))
    {
        fileNotExisted = true;
    }

    if (fileNotExisted)
    {
        context->converterContext.Log("Asset convert failed as " + importAssetPath + " cannot be found.");
        context->SetStatus(OmniConverterStatus::FILE_NOT_EXISTED);
        return;
    }

    if (!context->converterContext.IsSupportedImportAsset())
    {
        const std::string extension = StringUtils::ToLower(PathUtils::GetExtension(importAssetPath));
        detailedError = "Unsupported import format";
        if (!extension.empty())
        {
            detailedError += ": ." + extension;
        }
        detailedError += ". Supported formats: " + OmniConverterContext::GetSupportedImportFormatsForError();
        context->converterContext.Log(detailedError);
        context->SetDetailedError(detailedError);
        context->SetStatus(OmniConverterStatus::UNSUPPORTED_IMPORT_FORMAT);
        return;
    }

    // Output can be anonymous layer too so all imports will be in-memory.
    std::string baseOutPath = PathUtils::GetDirName(outputAssetPath);
    if (context->converterContext.IsInMemoryOutput())
    {
        auto layer = PXR_NS::SdfLayer::Find(outputAssetPath);
        if (!layer)
        {
            detailedError = "Asset convert failed as anonymous layer " + outputAssetPath + " cannot be found.";
            context->converterContext.Log(detailedError);
            context->SetStatus(OmniConverterStatus::FILE_NOT_EXISTED);
            return;
        }
    }
    else if (!context->converterContext.IsPathExisted(baseOutPath) && !context->converterContext.MakeDirectories(baseOutPath))
    {
        detailedError = "Asset convert failed as dir " + baseOutPath + " cannot be created.";
        context->converterContext.Log(detailedError);
        context->SetStatus(OmniConverterStatus::DIRECTORY_CREATE_FAILED);
        return;
    }

    auto importer = CreateImporter(context->converterContext);
    auto startImportTime = std::chrono::system_clock::now();

    // Checks caching
    bool cached = false;
    if (context->converterContext.IsCachingEnabled())
    {
        context->converterContext.Log("Caching is enabled, using cache store: " + context->converterContext.GetCacheFolder());
        const std::string& sha1Hash = importer->ComputeHash(context);
        context->converterContext.SetImportAssetDigest(sha1Hash);
        if (!sha1Hash.empty())
        {
            context->converterContext.Log("Asset hash: " + sha1Hash);
            const std::string& cacheItemPath = PathUtils::JoinPaths(
                { context->converterContext.GetCacheFolder(), sha1Hash, CACHED_USD_MAIN_FILE_NAME }
            );
            if (PathUtils::IsPathExisted(cacheItemPath))
            {
                context->converterContext.Log("Asset caching is found: " + PathUtils::GetDirName(cacheItemPath));

                auto endImportTime = std::chrono::system_clock::now();
                auto importCost = std::chrono::duration_cast<std::chrono::seconds>(endImportTime - startImportTime).count();
                context->converterContext.Log("Costs " + std::to_string(importCost) + "s to import asset.");
                importer.reset();

                auto outputLayer = PXR_NS::SdfLayer::Find(outputAssetPath);
                auto cachedLayer = PXR_NS::SdfLayer::FindOrOpen(cacheItemPath);
                if (!cachedLayer)
                {
                    context->converterContext.Log("ERROR: Cached layer " + cacheItemPath + " is broken. Removing...");
                    if (!PathUtils::RemoveAll(cacheItemPath))
                    {
                        context->converterContext.Log("ERROR: Failed to remove cached path " + cacheItemPath + ".");
                    }
                }
                else
                {
                    cached = true;
                    auto outputStage = PXR_NS::UsdStage::Open(outputLayer);
                    auto rootPrim = outputStage->DefinePrim(PXR_NS::SdfPath::AbsoluteRootPath().AppendElementString("Root"));
#if !defined(_WIN32)
                    if (PathUtils::IsOmniversePath(importAssetPath))
                    {
                        // Omni client needs local path to be prefixed with scheme, otherwise
                        // when the local path is referenced into a non-local stage. The resolver
                        // will concat the path as relative path.
                        rootPrim.GetReferences().AddReference("file:" + cacheItemPath);
                    }
                    else
#endif
                    {
                        rootPrim.GetReferences().AddReference(cacheItemPath);
                    }
                    PXR_NS::UsdUtilsCopyLayerMetadata(cachedLayer, outputLayer);
                    outputStage->SetDefaultPrim(rootPrim);
                }
            }
        }
    }

    if (!cached)
    {
        auto importedStage = importer->ImportStage(context, status, detailedError);
        auto endImportTime = std::chrono::system_clock::now();
        auto importCost = std::chrono::duration_cast<std::chrono::seconds>(endImportTime - startImportTime).count();
        context->converterContext.Log("Costs " + std::to_string(importCost) + "s for importer to import asset.");

        importer.reset();

        if (importedStage && status == OmniConverterStatus::OK)
        {
            if (context->converterContext.MergeAllMeshes())
            {
                StageUtils::MergeMeshes(importedStage, "mesh");
            }

            // Creates root node always
            if (!importedStage->rootNode)
            {
                importedStage->rootNode = std::make_shared<StageNode>("Root");
            }

            auto exporter = CreateExporter(context);
            auto startExportTime = std::chrono::system_clock::now();
            status = exporter->Export(importedStage, detailedError);
            auto endExportTime = std::chrono::system_clock::now();
            auto exportCost = std::chrono::duration_cast<std::chrono::seconds>(endExportTime - startExportTime).count();
            context->converterContext.Log("Costs " + std::to_string(exportCost) + "s for exporter to export asset.");
        }
    }

    context->converterContext.clear();
    context->FinishProgress();
    context->SetDetailedError(detailedError);
    context->SetStatus(status);
}

void OmniConverterFuture::FutureThreadContext::SetStatus(OmniConverterStatus status)
{
    std::lock_guard<std::mutex> lock(mStatusMutex);
    mCurrentStatus = status;
}

OmniConverterStatus OmniConverterFuture::FutureThreadContext::GetStatus()
{
    std::lock_guard<std::mutex> lock(mStatusMutex);
    return mCurrentStatus;
}

void OmniConverterFuture::FutureThreadContext::SetDetailedError(const std::string& error)
{
    std::lock_guard<std::mutex> lock(mStatusMutex);
    mDetailedError = error;
}

const std::string& OmniConverterFuture::FutureThreadContext::GetDetailedError()
{
    std::lock_guard<std::mutex> lock(mStatusMutex);
    return mDetailedError;
}

bool OmniConverterFuture::FutureThreadContext::MaterialLoader(OmniConverterMaterialDescription* material)
{
    std::lock_guard<std::mutex> lock(mExitMutex);
    if (!mExited)
    {
        return converterContext.LoadMaterial(mThisFuture, material);
    }

    return false;
}

void OmniConverterFuture::FutureThreadContext::StartProgress(uint32_t totalSteps)
{
    std::lock_guard<std::mutex> lock(mExitMutex);
    if (!mExited)
    {
        mTotalSteps = totalSteps;
        converterContext.ReportProgress(mThisFuture, 0, mTotalSteps);
    }
}

void OmniConverterFuture::FutureThreadContext::IncrementProgress(uint32_t steps)
{
    std::lock_guard<std::mutex> lock(mExitMutex);
    if (!mExited)
    {
        mCurrentProgress += steps;
        if (mCurrentProgress > mTotalSteps)
        {
            mCurrentProgress = mTotalSteps;
        }
        converterContext.ReportProgress(mThisFuture, mCurrentProgress, mTotalSteps);
    }
}

void OmniConverterFuture::FutureThreadContext::FinishProgress()
{
    std::lock_guard<std::mutex> lock(mExitMutex);
    if (!mExited)
    {
        converterContext.ReportProgress(mThisFuture, mTotalSteps, mTotalSteps);
    }
}

bool OmniConverterFuture::FutureThreadContext::IsExited()
{
    std::lock_guard<std::mutex> lock(mExitMutex);
    return mExited;
}

void OmniConverterFuture::FutureThreadContext::Exit()
{
    std::lock_guard<std::mutex> lock(mExitMutex);
    mExited = true;
}
