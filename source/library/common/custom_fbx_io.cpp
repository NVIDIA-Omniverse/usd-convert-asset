// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "custom_fbx_io.h"

CustomFBXWriteStream::CustomFBXWriteStream(FbxManager* pSdkManager, const OmniConverterBlobPtr& dataBlob) : mData(dataBlob)
{
    const char* format = "FBX binary (*.fbx)";
    mWriterID = pSdkManager->GetIOPluginRegistry()->FindWriterIDByDescription(format);
    mReaderID = -1;
}

FbxStream::EState CustomFBXWriteStream::GetState()
{
    return mData ? FbxStream::eOpen : eClosed;
}

bool CustomFBXWriteStream::Open(void* pStreamData)
{
    return (mData != nullptr);
}

size_t CustomFBXWriteStream::Write(const void* pData, FbxUInt64 pSize)
{
    if (!mData || pSize <= 0)
    {
        return 0;
    }

    if ((FbxUInt64)(mCurrentPosition + pSize) < mDataCapacity)
    {
        memcpy((uint8_t*)mData->buffer + mCurrentPosition, pData, pSize);
    }
    else
    {
        // Allocates more space to avoid frequent space allocation.
        mDataCapacity = ((mCurrentPosition + pSize) * 1.5);
        uint8_t* newData = new uint8_t[mDataCapacity];
        memcpy(newData, (uint8_t*)mData->buffer, mCurrentPosition);
        memcpy(newData + mCurrentPosition, pData, pSize);
        if (mData->deleter && mData->buffer)
        {
            mData->deleter(mData->buffer);
        }
        mData->buffer = newData;
        mData->deleter = gBlobDefaultDataDeleter;
    }
    mData->size = mCurrentPosition + pSize;
    mCurrentPosition += pSize;

    return (size_t)pSize;
}

size_t CustomFBXWriteStream::Read(void* pData, FbxUInt64 pSize) const
{
    return 0;
}

void CustomFBXWriteStream::Seek(const fbxsdk::FbxInt64& pOffset, const fbxsdk::FbxFile::ESeekPos& pSeekPos)
{
    if (!mData)
    {
        return;
    }

    switch (pSeekPos)
    {
        case FbxFile::eBegin:
            mCurrentPosition = pOffset;
            break;
        case FbxFile::eCurrent:
            mCurrentPosition = mCurrentPosition + pOffset;
            break;
        case FbxFile::eEnd:
            mCurrentPosition = (FbxInt64)mData->size + pOffset;
            break;
    }
}

CustomFBXReadStream::CustomFBXReadStream(FbxManager* pSdkManager, const OmniConverterBlobPtr& dataBlob) : mData(dataBlob)
{
    const char* format = "FBX (*.fbx)";
    mReaderID = pSdkManager->GetIOPluginRegistry()->FindReaderIDByDescription(format);
    mWriterID = -1;
}

FbxStream::EState CustomFBXReadStream::GetState()
{
    return mData ? FbxStream::eOpen : eClosed;
}

bool CustomFBXReadStream::Open(void* pStreamData)
{
    return (mData != nullptr);
}

size_t CustomFBXReadStream::Read(void* pData, FbxUInt64 pSize) const
{
    if (!mData || pSize <= 0)
    {
        return 0;
    }

    FbxInt64 currentSize = mCurrentPosition;
    // Customized IO does not support file that's over 2G.
    if (currentSize + pSize > 0x7FFFFFFF)
    {
        return 0;
    }

    FbxInt64 readSize = (mCurrentPosition + pSize > mData->size) ? (mData->size - mCurrentPosition) : pSize;
    memcpy(pData, (uint8_t*)mData->buffer + mCurrentPosition, readSize);
    mCurrentPosition += readSize;

    return (size_t)readSize;
}

void CustomFBXReadStream::Seek(const FbxInt64& pOffset, const FbxFile::ESeekPos& pSeekPos)
{
    if (!mData)
    {
        return;
    }

    switch (pSeekPos)
    {
        case FbxFile::eBegin:
            mCurrentPosition = pOffset;
            break;
        case FbxFile::eCurrent:
            mCurrentPosition = mCurrentPosition + pOffset;
            break;
        case FbxFile::eEnd:
            mCurrentPosition = (FbxInt64)mData->size + pOffset;
            break;
    }
}
