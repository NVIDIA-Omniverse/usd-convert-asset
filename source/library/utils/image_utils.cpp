// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "image_utils.h"

#if defined(_WIN32)
#    define STBI_MSC_SECURE_CRT
#else
#    include <dlfcn.h>
#endif

#include <cstdlib> // for free()

// STB_IMAGE_IMPLEMENTATION and STB_IMAGE_WRITE_IMPLEMENTATION are already
// defined in tiny_gltf.cpp to avoid duplicate symbol linker errors
#define STBI_FREE(p) free(p)
#include "../thirdparty/stb_image.h"

#if defined(_WIN32)
#    define STBI_MSC_SECURE_CRT
#endif

#undef snprintf
#define STBIW_FREE(p) free(p)
#include "../thirdparty/stb_image_write.h"

// Forward declare stbi_write_png_to_mem which is defined in tiny_gltf.cpp
// but not part of the public API in stb_image_write.h
#ifdef __cplusplus
extern "C"
{
#endif
    unsigned char* stbi_write_png_to_mem(const unsigned char* pixels, int stride_bytes, int x, int y, int n, int* out_len);
#ifdef __cplusplus
}
#endif

static auto gStbImageDeleter = [](void* buffer)
{
    free((unsigned char*)buffer);
};


bool ImageUtils::HasAlphaChannel(const uint8_t* compressedImageData, size_t size)
{
    ImageHeader header;
    if (LoadImageInfoFromMemory(compressedImageData, size, header))
    {
        return header.channels == 4;
    }

    return false;
}

bool ImageUtils::LoadImageInfoFromMemory(const uint8_t* compressedImageData, size_t size, ImageHeader& header)
{
    header.bitsPerChannel = stbi_is_16_bit_from_memory(compressedImageData, size) ? 16 : 8;
    return stbi_info_from_memory(compressedImageData, size, &header.width, &header.height, &header.channels);
}

bool ImageUtils::LoadImageFromMemory(const uint8_t* compressedImageData, size_t size, Image& image)
{
    uint8_t* data = nullptr;
    if (stbi_is_16_bit_from_memory(compressedImageData, size))
    {
        data = (uint8_t*)stbi_load_16_from_memory(compressedImageData, size, &image.info.width, &image.info.height, &image.info.channels, 0);
    }
    else
    {
        data = (uint8_t*)stbi_load_from_memory(compressedImageData, size, &image.info.width, &image.info.height, &image.info.channels, 0);
    }

    if (data)
    {
        size_t size = image.info.width * image.info.height * image.info.channels * sizeof(data[0]);
        image.decompressedData = createOmniConverterBlob((uint8_t*)data, size, gStbImageDeleter);
    }

    return image.decompressedData != nullptr;
}

#if 0
Image ImageUtils::CreateDecompressedImage(int width, int height, int bitsPerChannel, int channels)
{
    ImageHeader header;
    header.width = width;
    header.height = height;
    header.channels = channels;
    header.bitsPerChannel = bitsPerChannel;

    Image image;
    image.info = header;
    size_t imageSize = width * height * channels * (bitsPerChannel / 8);

    OmniConverterBlobPtr resultBlob;
    resultBlob = createOmniConverterBlob(new uint8_t[imageSize], imageSize);
    uint8_t* resultData = (uint8_t*)resultBlob->buffer;
    memset(resultData, 0, imageSize);

    image.decompressedData = resultBlob;

    return image;
}

OmniConverterBlobPtr ImageUtils::ScaleImageAsPNG(const uint8_t* compressedImageData, size_t size, double scale, double offset)
{
    Image sourceImage;
    if (!ImageUtils::LoadImageFromMemory(compressedImageData, size, sourceImage))
    {
        return nullptr;
    }

    Image targetImage = ImageUtils::CreateDecompressedImage(
        sourceImage.info.width,
        sourceImage.info.height,
        sourceImage.info.bitsPerChannel,
        sourceImage.info.channels
    );
    size_t targetImageChannelBytes = targetImage.info.bitsPerChannel / 8;
    size_t targetImagePixelSize = targetImage.info.channels * targetImageChannelBytes;
    uint32_t targetImageLineSize = targetImage.info.width * targetImagePixelSize;
    uint8_t* targetData = (uint8_t*)targetImage.decompressedData->buffer;
    uint8_t* sourceData = (uint8_t*)sourceImage.decompressedData->buffer;
    double offsetValue;
    if (targetImage.info.bitsPerChannel == 16)
    {
        offsetValue = offset * (double)std::numeric_limits<uint16_t>::max();
    }
    else
    {
        offsetValue = offset * (double)std::numeric_limits<uint8_t>::max();
    }

    for (int lineNo = 0; lineNo < targetImage.info.height; lineNo++)
    {
        size_t targetImageLineBase = lineNo * targetImageLineSize;
        for (size_t pixelIndex = 0; pixelIndex < targetImage.info.width; pixelIndex++)
        {
            uint8_t* targetDataStart = targetData + targetImageLineBase + pixelIndex * targetImagePixelSize;
            uint8_t* sourceDataStart = sourceData + targetImageLineBase + pixelIndex * targetImagePixelSize;
            if (targetImage.info.bitsPerChannel == 16)
            {
                uint16_t* sourceDataStart16 = (uint16_t*)sourceDataStart;
                uint16_t* targetDataStart16 = (uint16_t*)targetDataStart;
                for (int channel = 0; channel < targetImage.info.channels; channel++)
                {
                    double scaledValue = scale * sourceDataStart16[channel] + offsetValue;
                    targetDataStart16[channel] = (uint16_t)std::min(scaledValue, (double)std::numeric_limits<uint16_t>::max());
                }
            }
            else
            {
                for (int channel = 0; channel < targetImage.info.channels; channel++)
                {
                    double scaledValue = scale * sourceDataStart[channel] + offsetValue;
                    targetDataStart[channel] = (uint8_t)std::min(scaledValue, (double)std::numeric_limits<uint8_t>::max());
                }
            }
        }
    }

    int channelPngSize;
    auto channelPng = stbi_write_png_to_mem(
        targetData,
        targetImageLineSize,
        targetImage.info.width,
        targetImage.info.height,
        targetImage.info.channels,
        &channelPngSize
    );
    if (channelPngSize != 0)
    {
        return createOmniConverterBlob(channelPng, channelPngSize, gStbImageDeleter);
    }

    return nullptr;
}
#endif

OmniConverterBlobPtr ImageUtils::ExtractSourceChannelAndSetTargetChannelAsPNG(
    const uint8_t* targetImageData,
    size_t targetImageSize,
    int targetImageChannel,
    const uint8_t* sourceImageData,
    size_t sourceImageSize,
    int sourceImageChannel,
    double scaleSource,
    double offsetSource
)
{
    Image targetImage;
    if (!ImageUtils::LoadImageFromMemory(targetImageData, targetImageSize, targetImage))
    {
        return nullptr;
    }

    if (targetImageChannel != -1 && targetImageChannel >= targetImage.info.channels)
    {
        return nullptr;
    }

    size_t resultChannels = targetImage.info.channels;
    if (targetImageChannel == -1)
    {
        targetImageChannel = resultChannels;
        resultChannels += 1;
    }
    size_t targetImageChannelBytes = targetImage.info.bitsPerChannel / 8;
    size_t targetImagePixelSize = targetImage.info.channels * targetImageChannelBytes;
    size_t resultImagePixelSize = resultChannels * targetImageChannelBytes;
    uint32_t targetImageLineSize = targetImage.info.width * targetImagePixelSize;
    uint32_t resultImageLineSize = targetImage.info.width * resultImagePixelSize;
    uint32_t resultImageSize = resultImageLineSize * targetImage.info.height;
    uint8_t* targetData = (uint8_t*)targetImage.decompressedData->buffer;

    OmniConverterBlobPtr resultBlob;
    resultBlob = createOmniConverterBlob(new uint8_t[resultImageSize], resultImageSize);
    uint8_t* resultData = (uint8_t*)resultBlob->buffer;
    memset(resultData, 0, resultImageSize);

    // Copy target image into result image.
    if (resultChannels == targetImage.info.channels)
    {
        memcpy(resultData, targetImage.decompressedData->buffer, targetImage.decompressedData->size);
    }
    else
    {
        for (size_t lineNo = 0; lineNo < targetImage.info.height; lineNo++)
        {
            size_t targetImageLineBase = lineNo * targetImageLineSize;
            size_t resultImageLineBase = lineNo * resultImageLineSize;
            for (size_t pixelIndex = 0; pixelIndex < targetImage.info.width; pixelIndex++)
            {
                uint8_t* resultDataStart = resultData + resultImageLineBase + pixelIndex * resultImagePixelSize;
                uint8_t* targetDataStart = targetData + targetImageLineBase + pixelIndex * targetImagePixelSize;
                memcpy(resultDataStart, targetDataStart, targetImagePixelSize);
            }
        }
    }

    Image resultImage;
    resultImage.info = targetImage.info;
    resultImage.info.channels = resultChannels;
    resultImage.decompressedData = resultBlob;
    if (ImageUtils::ExtractSourceChannelAndSetTargetChannel(
            resultImage,
            targetImageChannel,
            sourceImageData,
            sourceImageSize,
            sourceImageChannel,
            scaleSource,
            offsetSource
        ))
    {
        int channelPngSize;
        auto channelPng = stbi_write_png_to_mem(
            resultData,
            resultImageLineSize,
            targetImage.info.width,
            targetImage.info.height,
            resultChannels,
            &channelPngSize
        );
        if (channelPngSize != 0)
        {
            return createOmniConverterBlob(channelPng, channelPngSize, gStbImageDeleter);
        }
    }

    return nullptr;
}

bool ImageUtils::ExtractSourceChannelAndSetTargetChannel(
    Image& targetImage,
    int targetImageChannel,
    const uint8_t* sourceImageData,
    size_t sourceImageSize,
    int sourceImageChannel,
    double scaleSource,
    double offsetSource
)
{
    Image sourceImage;
    if (!ImageUtils::LoadImageFromMemory(sourceImageData, sourceImageSize, sourceImage))
    {
        return false;
    }

    // Don't support to copy 16bits channel to 8bits channel.
    if (sourceImage.info.width != targetImage.info.width || sourceImage.info.height != targetImage.info.height ||
        sourceImage.info.bitsPerChannel > targetImage.info.bitsPerChannel)
    {
        return false;
    }

    if (targetImageChannel >= targetImage.info.channels)
    {
        return false;
    }

    if (sourceImageChannel != -1 && sourceImageChannel >= sourceImage.info.channels)
    {
        return false;
    }

    size_t targetImageChannelBytes = targetImage.info.bitsPerChannel / 8;
    size_t sourceImageChannelBytes = sourceImage.info.bitsPerChannel / 8;
    size_t targetImagePixelSize = targetImage.info.channels * targetImageChannelBytes;
    size_t sourceImagePixelSize = sourceImage.info.channels * sourceImageChannelBytes;
    uint32_t targetImageLineSize = targetImage.info.width * targetImagePixelSize;
    uint32_t sourceImageLineSize = sourceImage.info.width * sourceImagePixelSize;
    uint8_t* targetData = (uint8_t*)targetImage.decompressedData->buffer;
    uint8_t* sourceData = (uint8_t*)sourceImage.decompressedData->buffer;

    double sourceOffsetValue;
    if (sourceImage.info.bitsPerChannel == 16)
    {
        sourceOffsetValue = offsetSource * (double)std::numeric_limits<uint16_t>::max();
    }
    else
    {
        sourceOffsetValue = offsetSource * (double)std::numeric_limits<uint8_t>::max();
    }

    // Extracts source image and sets to target channel.
    for (size_t lineNo = 0; lineNo < sourceImage.info.height; lineNo++)
    {
        size_t sourceImageLineBase = lineNo * sourceImageLineSize;
        size_t targetImageLineBase = lineNo * targetImageLineSize;
        for (size_t pixelIndex = 0; pixelIndex < sourceImage.info.width; pixelIndex++)
        {
            uint8_t* targetDataStart = targetData + targetImageLineBase + pixelIndex * targetImagePixelSize;
            uint8_t* sourceDataStart = sourceData + sourceImageLineBase + pixelIndex * sourceImagePixelSize;
            uint32_t finalValue = 0;
            if (sourceImageChannel == -1) // Average
            {
                if (sourceImage.info.bitsPerChannel == 16)
                {
                    uint16_t* sourceData16bit = (uint16_t*)sourceDataStart;
                    for (size_t channel = 0; channel < sourceImage.info.channels; channel++)
                    {
                        finalValue += sourceData16bit[channel];
                    }
                }
                else // Single byte
                {
                    for (size_t channel = 0; channel < sourceImage.info.channels; channel++)
                    {
                        for (size_t channel = 0; channel < sourceImage.info.channels; channel++)
                        {
                            finalValue += sourceDataStart[channel];
                        }
                    }
                }
                finalValue = finalValue / sourceImage.info.channels;
            }
            else
            {
                if (sourceImage.info.bitsPerChannel == 16) // Single byte
                {
                    uint16_t* sourceData16bit = (uint16_t*)sourceDataStart;
                    finalValue = sourceData16bit[sourceImageChannel];
                }
                else
                {
                    finalValue = sourceDataStart[sourceImageChannel];
                }
            }

            double scaledValue = scaleSource * finalValue + sourceOffsetValue;
            if (targetImage.info.bitsPerChannel == 16)
            {
                uint16_t* targetData16bit = (uint16_t*)targetDataStart;
                targetData16bit[targetImageChannel] = (uint16_t)std::min(scaledValue, (double)std::numeric_limits<uint16_t>::max());
            }
            else
            {
                targetDataStart[targetImageChannel] = (uint8_t)std::min(scaledValue, (double)std::numeric_limits<uint8_t>::max());
            }
        }
    }

    return true;
}

OmniConverterBlobPtr ImageUtils::MergeTwoImageChannelsAsPNG(
    const uint8_t* firstImageData,
    size_t firstImageSize,
    int firstImageChannel,
    int firstTargetChannel,
    const uint8_t* secondImageData,
    size_t secondImageSize,
    int secondImageChannel,
    int secondTargetChannel,
    double scaleFirst,
    double offsetFirst,
    double scaleSecond,
    double offsetSecond
)
{
    if (firstTargetChannel < 0 || firstTargetChannel >= 3 || secondTargetChannel < 0 || secondTargetChannel >= 3)
    {
        return nullptr;
    }

    ImageHeader firstImageInfo;
    if (!ImageUtils::LoadImageInfoFromMemory(firstImageData, firstImageSize, firstImageInfo))
    {
        return nullptr;
    }

    ImageHeader secondImageInfo;
    if (!ImageUtils::LoadImageInfoFromMemory(secondImageData, secondImageSize, secondImageInfo))
    {
        return nullptr;
    }

    if (firstImageInfo.width != secondImageInfo.width || firstImageInfo.height != secondImageInfo.height)
    {
        return nullptr;
    }

    if (firstImageChannel != -1 && firstImageChannel >= firstImageInfo.channels)
    {
        return nullptr;
    }

    if (secondImageChannel != -1 && secondImageChannel >= secondImageInfo.channels)
    {
        return nullptr;
    }

    int bitsPerChannel = std::max(firstImageInfo.bitsPerChannel, secondImageInfo.bitsPerChannel);

    Image resultImage;
    resultImage.info = firstImageInfo;
    resultImage.info.channels = 3;
    resultImage.info.bitsPerChannel = bitsPerChannel;
    size_t resultImageLineSize = resultImage.info.width * 3 * (resultImage.info.bitsPerChannel / 8);
    size_t resultSize = resultImageLineSize * resultImage.info.height;
    resultImage.decompressedData = createOmniConverterBlob(new uint8_t[resultSize], resultSize);

    if (!ImageUtils::ExtractSourceChannelAndSetTargetChannel(
            resultImage,
            firstTargetChannel,
            firstImageData,
            firstImageSize,
            firstImageChannel,
            scaleFirst,
            offsetFirst
        ))
    {
        return nullptr;
    }

    if (!ImageUtils::ExtractSourceChannelAndSetTargetChannel(
            resultImage,
            secondTargetChannel,
            secondImageData,
            secondImageSize,
            secondImageChannel,
            scaleSecond,
            offsetSecond
        ))
    {
        return nullptr;
    }

    int channelPngSize;
    auto channelPng = stbi_write_png_to_mem(
        (unsigned char*)resultImage.decompressedData->buffer,
        resultImageLineSize,
        resultImage.info.width,
        resultImage.info.height,
        resultImage.info.channels,
        &channelPngSize
    );
    if (channelPngSize != 0)
    {
        return createOmniConverterBlob(channelPng, channelPngSize, gStbImageDeleter);
    }

    return nullptr;
}
