//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************
//
// Capture Converter Sample (C++)
//
// Demonstrates how to inspect the on-disk format of a PIX GPU capture
// file and upgrade older captures to the current format.
//

#include <wrl/client.h>
#include <wrl/implements.h>
using Microsoft::WRL::ComPtr;

#include "d3d12.h"
#include "PixApi.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace
{
    // Inline IPixProgressNotifications implementation: prints status and
    // progress events as they fire while UpgradeGpuCaptureFile runs.
    // Capture upgrades on large files can take many seconds; passing a
    // notifications object lets the user see the work happening rather
    // than apparent hang.
    class ProgressNotificationsImpl : public Microsoft::WRL::RuntimeClass<
        Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
        IPixProgressNotifications>
    {
    public:
        void OnStatus(_In_ LPCWSTR status) override
        {
            wprintf(L"  Status: %ls\n", status ? status : L"");
        }

        void OnProgress(_In_ float progress) override
        {
            wprintf(L"  Progress: %.0f%%\n", progress * 100.0f);
        }
    };
}

const wchar_t* GetGpuCaptureFileFormatName(PIX_GPU_CAPTURE_FILE_FORMAT fileFormat)
{
    switch (fileFormat)
    {
    case PIX_GPU_CAPTURE_FILE_FORMAT_NO_FILE:
        return L"No file";
    case PIX_GPU_CAPTURE_FILE_FORMAT_INVALID_OR_CORRUPT:
        return L"Invalid or corrupt";
    case PIX_GPU_CAPTURE_FILE_FORMAT_PRE2026:
        return L"Pre-2026";
    case PIX_GPU_CAPTURE_FILE_FORMAT_2026:
        return L"2026";
    default:
        return L"Unknown";
    }
}

std::wstring GetUpgradedCapturePath(std::filesystem::path const& sourcePath)
{
    std::wstring upgradedFileName = sourcePath.stem().wstring();
    upgradedFileName += L"_upgraded";
    upgradedFileName += sourcePath.extension().wstring();

    return (sourcePath.parent_path() / upgradedFileName).wstring();
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 2)
    {
        wprintf(L"Usage: CaptureConverter.exe path\\to\\capture.wpix\n");
        return 1;
    }

    std::filesystem::path sourceCapturePath = argv[1];

    // Step 1: Create the PIX factory.
    ComPtr<IPixFactory> factory;
    HRESULT hr = PixCreateFactory(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        wprintf(L"PixCreateFactory failed. hr=0x%08X\n", static_cast<unsigned int>(hr));
        return 1;
    }

    // Step 2: Create the capture file converter.
    ComPtr<IPixCaptureFileConverter> captureFileConverter;
    hr = factory->CreatePixCaptureFileConverter(IID_PPV_ARGS(captureFileConverter.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        wprintf(L"CreatePixCaptureFileConverter failed. hr=0x%08X\n", static_cast<unsigned int>(hr));
        return 1;
    }

    // Step 3: Detect the capture file format.
    PIX_GPU_CAPTURE_FILE_FORMAT fileFormat = captureFileConverter->GetGpuCaptureFileFormat(sourceCapturePath.c_str());
    wprintf(
        L"Detected capture format for '%ls': %ls (%u)\n",
        sourceCapturePath.c_str(),
        GetGpuCaptureFileFormatName(fileFormat),
        static_cast<unsigned int>(fileFormat));

    if (fileFormat == PIX_GPU_CAPTURE_FILE_FORMAT_NO_FILE)
    {
        wprintf(L"The specified capture file does not exist.\n");
        return 1;
    }

    if (fileFormat == PIX_GPU_CAPTURE_FILE_FORMAT_INVALID_OR_CORRUPT)
    {
        wprintf(L"The specified file is not a valid PIX GPU capture.\n");
        return 1;
    }

    if (fileFormat == PIX_GPU_CAPTURE_FILE_FORMAT_CURRENT)
    {
        wprintf(L"The capture is already in the current format. No upgrade is needed.\n");
        return 0;
    }

    if (fileFormat != PIX_GPU_CAPTURE_FILE_FORMAT_PRE2026)
    {
        wprintf(L"The capture format is not recognized as upgradeable by this sample.\n");
        return 1;
    }

    // Step 4: Upgrade the capture file to a new destination.
    std::wstring upgradedCapturePath = GetUpgradedCapturePath(sourceCapturePath);
    wprintf(L"Upgrading capture to '%ls'...\n", upgradedCapturePath.c_str());

    hr = captureFileConverter->UpgradeGpuCaptureFile(
        sourceCapturePath.c_str(),
        upgradedCapturePath.c_str(),
        Microsoft::WRL::Make<ProgressNotificationsImpl>().Get());
    if (FAILED(hr))
    {
        wprintf(L"UpgradeGpuCaptureFile failed. hr=0x%08X\n", static_cast<unsigned int>(hr));
        return 1;
    }

    // Step 5: Print the upgrade results.
    wprintf(L"Upgrade succeeded.\n");
    wprintf(L"Upgraded capture saved to: %ls\n", upgradedCapturePath.c_str());
    return 0;
}
