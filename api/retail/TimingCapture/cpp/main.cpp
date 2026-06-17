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
// Timing Capture Sample (C++)
//
// Demonstrates how to open a PIX timing capture document (.wpix),
// inspect basic document information, optionally resolve symbols
// using a PDB path with status / progress callbacks, enumerate system
// monitor counters, and close the document.
//
// Usage: TimingCapture <path-to-timing-capture.wpix> [full-pdb-path]
//

#include <wrl/client.h>
#include <wrl/implements.h>
using Microsoft::WRL::ComPtr;

#include "d3d12.h"
#include "PixApi.h"

#include <cwchar>
#include <map>
#include <string>
#include <vector>

namespace
{
    bool CheckHr(HRESULT hr, LPCWSTR operationName)
    {
        if (FAILED(hr))
        {
            wprintf(L"%ls failed.\n", operationName);
            wprintf(L"Error code: 0x%08X\n", static_cast<unsigned int>(hr));
            return false;
        }

        return true;
    }

    struct SystemMonitorCounterDescription
    {
        std::wstring DisplayName;
        std::wstring Units;
        float DefinedMin = 0.0f;
        float DefinedMax = 0.0f;
    };

    LPCWSTR GetWideStringOrDefault(LPCWSTR value, LPCWSTR defaultValue = L"(none)")
    {
        return (value && *value) ? value : defaultValue;
    }

    // Inline IPixProgressNotifications implementation: prints status and
    // progress events as they fire, so a slow ResolveSymbols call gives
    // the user real-time feedback rather than appearing to hang.
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

    void TryPrintSystemMonitorCounters(IPixFactory* factory)
    {
        PIX_CONNECTION_DESC_LOCAL localConnectionDesc = {};
        localConnectionDesc.Flags = PIX_CONNECTION_FLAG_LOCAL_NONE;

        PIX_CONNECTION_DESC connectionDesc = {};
        connectionDesc.Type = PIX_CONNECTION_TYPE_LOCAL;
        connectionDesc.pLocal = &localConnectionDesc;

        ComPtr<IPixConnectionDocument> connectionDocument;
        HRESULT hr = factory->OpenConnectionDocument(
            &connectionDesc,
            nullptr,
            IID_PPV_ARGS(connectionDocument.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
        {
            wprintf(L"System monitor counter enumeration is unavailable.\n");
            wprintf(L"OpenConnectionDocument failed: 0x%08X\n", static_cast<unsigned int>(hr));
            return;
        }

        ComPtr<IPixGetCounterDescriptionsResults> counterDescriptions;
        hr = connectionDocument->GetCounterDescriptions(
            IID_PPV_ARGS(counterDescriptions.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
        {
            wprintf(L"System monitor counter enumeration is unavailable.\n");
            wprintf(L"GetCounterDescriptions failed: 0x%08X\n", static_cast<unsigned int>(hr));
            return;
        }

        std::map<UINT, std::wstring> counterGroupNamesById;
        const UINT64 counterGroupCount = counterDescriptions->GetNumCounterGroups();
        for (UINT64 counterGroupIndex = 0; counterGroupIndex < counterGroupCount; ++counterGroupIndex)
        {
            ComPtr<IPixSystemMonitorCounterGroup> counterGroup;
            hr = counterDescriptions->GetCounterGroupDescription(
                counterGroupIndex,
                IID_PPV_ARGS(counterGroup.ReleaseAndGetAddressOf()));
            if (FAILED(hr))
            {
                wprintf(L"Failed to read a system monitor counter group: 0x%08X\n", static_cast<unsigned int>(hr));
                continue;
            }

            counterGroupNamesById[counterGroup->GetCounterGroupId()] = GetWideStringOrDefault(counterGroup->GetName());
        }

        std::map<UINT, std::vector<SystemMonitorCounterDescription>> visibleCountersByGroupId;
        const UINT64 totalCounterCount = counterDescriptions->GetNumCounters();
        UINT64 visibleCounterCount = 0;

        for (UINT64 counterIndex = 0; counterIndex < totalCounterCount; ++counterIndex)
        {
            ComPtr<IPixSystemMonitorCounter> counterDescription;
            hr = counterDescriptions->GetCounterDescription(
                counterIndex,
                IID_PPV_ARGS(counterDescription.ReleaseAndGetAddressOf()));
            if (FAILED(hr))
            {
                wprintf(L"Failed to read a system monitor counter: 0x%08X\n", static_cast<unsigned int>(hr));
                continue;
            }

            if (!counterDescription->GetIsVisible() || counterDescription->GetIsInternal())
            {
                continue;
            }

            visibleCountersByGroupId[counterDescription->GetCounterGroupId()].push_back({
                GetWideStringOrDefault(counterDescription->GetDisplayName(), L"(unnamed counter)"),
                GetWideStringOrDefault(counterDescription->GetUnits()),
                counterDescription->GetDefinedMin(),
                counterDescription->GetDefinedMax() });
            ++visibleCounterCount;
        }

        wprintf(L"System monitor counters: %llu visible of %llu total.\n", visibleCounterCount, totalCounterCount);
        if (visibleCountersByGroupId.empty())
        {
            wprintf(L"No visible non-internal system monitor counters were found.\n");
            return;
        }

        for (auto const& [counterGroupId, counterDescriptionsForGroup] : visibleCountersByGroupId)
        {
            auto const counterGroupName = counterGroupNamesById.find(counterGroupId);
            LPCWSTR groupName = counterGroupName != counterGroupNamesById.end()
                ? counterGroupName->second.c_str()
                : L"(group unavailable)";

            wprintf(
                L"  Group %u: %ls (%zu counters)\n",
                counterGroupId,
                groupName,
                counterDescriptionsForGroup.size());

            for (SystemMonitorCounterDescription const& counterDescription : counterDescriptionsForGroup)
            {
                wprintf(L"    %ls\n", counterDescription.DisplayName.c_str());
                wprintf(L"      GroupId: %u\n", counterGroupId);
                wprintf(L"      Units: %ls\n", counterDescription.Units.c_str());
                wprintf(
                    L"      Range: %.3f to %.3f\n",
                    counterDescription.DefinedMin,
                    counterDescription.DefinedMax);
            }
        }
    }

    void PrintUsage()
    {
        wprintf(L"Usage: TimingCapture.exe <path-to-timing-capture.wpix> [full-pdb-path]\n");
    }
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2 || argc > 3)
    {
        PrintUsage();
        return 1;
    }

    LPCWSTR timingCapturePath = argv[1];
    LPCWSTR fullPdbPath = argc > 2 ? argv[2] : nullptr;

    // Step 1: Create the PIX factory.
    ComPtr<IPixFactory> factory;
    if (!CheckHr(PixCreateFactory(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())), L"PixCreateFactory"))
    {
        return 1;
    }

    // Step 2: Open the timing capture document.
    ComPtr<IPixTimingCaptureDocument> timingCaptureDocument;
    if (!CheckHr(
        factory->OpenTimingCaptureDocument(
            timingCapturePath,
            IID_PPV_ARGS(timingCaptureDocument.ReleaseAndGetAddressOf())),
        L"OpenTimingCaptureDocument"))
    {
        return 1;
    }

    // Step 3: Print basic document information.
    wprintf(L"Capture path: %ls\n", GetWideStringOrDefault(timingCaptureDocument->GetCapturePath()));
    wprintf(L"PIX storage path: %ls\n", GetWideStringOrDefault(timingCaptureDocument->GetPixStoragePath()));

    // Step 4: Optionally resolve symbols using the provided PDB path.
    // Symbol resolution can take a long time; the IPixProgressNotifications
    // implementation prints status / progress events as they fire so the
    // user gets real-time feedback rather than apparent hangs.
    if (fullPdbPath != nullptr)
    {
        wprintf(L"Resolving symbols using: %ls\n", fullPdbPath);

        TimingCaptureSymbolSettings symbolSettings = {};
        symbolSettings.UseNTSymbolPath = TRUE;
        symbolSettings.IncludeSourceData = TRUE;
        symbolSettings.IncludeTypeData = FALSE;
        symbolSettings.IncludeKernelSymbols = FALSE;

        ComPtr<ProgressNotificationsImpl> progressNotifications =
            Microsoft::WRL::Make<ProgressNotificationsImpl>();
        if (!CheckHr(
            timingCaptureDocument->ResolveSymbols(fullPdbPath, &symbolSettings, progressNotifications.Get()),
            L"ResolveSymbols"))
        {
            timingCaptureDocument->Close();
            return 1;
        }
    }
    else
    {
        wprintf(L"Skipping symbol resolution (pass a PDB path to demo it).\n");
    }

    // Step 5: Enumerate available system monitor counters.
    TryPrintSystemMonitorCounters(factory.Get());

    // Step 6: Close the timing capture document.
    if (!CheckHr(timingCaptureDocument->Close(), L"Close"))
    {
        return 1;
    }

    // Step 7: Print success.
    wprintf(L"Timing capture sample completed successfully.\n");
    return 0;
}
