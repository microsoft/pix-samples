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
// GPU Counters Sample (C++)
//
// Demonstrates collecting GPU performance data from an
// analyzed PIX GPU capture:
//   1. Create a PIX factory and open a .wpix capture.
//   2. Get the capture analysis interface and connect to the local GPU.
//   3. Start analysis (replay) on the connected GPU.
//   4. Enumerate available counter groups and counters.
//   5. Collect a few counters and print sample values.
//   6. Collect GPU occupancy data.
//   7. Collect high-frequency counters.
//   8. Read per-event GPU timing data.
//

#include <wil/com.h>
#include <wil/resource.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "PixApi.h"

namespace
{
    constexpr HRESULT E_PIX_DEVELOPER_MODE_NOT_ENABLED = 0x8abc0000;
    constexpr HRESULT E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE = 0x8abc0001;

    struct CounterSelection
    {
        PIX_GPU_COUNTER_ID CounterId = 0;
        std::wstring CounterName;
        std::wstring CounterGroupName;
        PIX_FORMAT_SPECIFIER_TYPE DataType = PIX_FORMAT_SPECIFIER_DEFAULT;
    };

    const wchar_t* GetWideStringOrDefault(LPCWSTR value, const wchar_t* defaultValue = L"(none)")
    {
        return (value != nullptr && value[0] != L'\0') ? value : defaultValue;
    }

    bool PrintErrorAndReturnFalse(HRESULT hresult, std::wstring_view errorMessage)
    {
        if (SUCCEEDED(hresult))
        {
            return true;
        }

        if (hresult == E_PIX_DEVELOPER_MODE_NOT_ENABLED || hresult == E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE)
        {
            wprintf(L"ERROR: Windows Developer Mode is required for this operation.\n");
            wprintf(L"Enable it in Settings > Privacy & security > For developers.\n");
        }

        wprintf(L"%ls\n", errorMessage.data());
        wprintf(L"HRESULT: 0x%08X\n", static_cast<unsigned int>(hresult));
        return false;
    }

    std::wstring FormatCounterValue(UINT64 rawValue, PIX_FORMAT_SPECIFIER_TYPE dataType)
    {
        if (rawValue == PIX_EVENT_COUNTER_NONE)
        {
            return L"(no data)";
        }

        const UINT32 formatBits = static_cast<UINT32>(dataType);
        const UINT32 typeBits = formatBits & PIX_FORMAT_SPECIFIER_TYPE_BITMASK;
        const UINT32 sizeBits = formatBits & PIX_FORMAT_SPECIFIER_SIZE_BITMASK;

        std::wstringstream valueStream;

        if (typeBits == PIX_FORMAT_SPECIFIER_TYPE_FLOAT)
        {
            if (sizeBits == PIX_FORMAT_SPECIFIER_SIZE_32BIT)
            {
                const UINT32 rawValue32 = static_cast<UINT32>(rawValue);
                float floatValue = 0.0f;
                std::memcpy(&floatValue, &rawValue32, sizeof(floatValue));
                valueStream << floatValue;
                return valueStream.str();
            }

            if (sizeBits == PIX_FORMAT_SPECIFIER_SIZE_64BIT)
            {
                double floatValue = 0.0;
                std::memcpy(&floatValue, &rawValue, sizeof(floatValue));
                valueStream << floatValue;
                return valueStream.str();
            }
        }
        else if (typeBits == PIX_FORMAT_SPECIFIER_TYPE_INT)
        {
            if (sizeBits == PIX_FORMAT_SPECIFIER_SIZE_16BIT)
            {
                valueStream << static_cast<INT16>(rawValue);
                return valueStream.str();
            }

            if (sizeBits == PIX_FORMAT_SPECIFIER_SIZE_32BIT)
            {
                valueStream << static_cast<INT32>(rawValue);
                return valueStream.str();
            }

            valueStream << static_cast<INT64>(rawValue);
            return valueStream.str();
        }
        else if (typeBits == PIX_FORMAT_SPECIFIER_TYPE_UINT)
        {
            if (sizeBits == PIX_FORMAT_SPECIFIER_SIZE_16BIT)
            {
                valueStream << static_cast<UINT16>(rawValue);
                return valueStream.str();
            }

            if (sizeBits == PIX_FORMAT_SPECIFIER_SIZE_32BIT)
            {
                valueStream << static_cast<UINT32>(rawValue);
                return valueStream.str();
            }

            valueStream << rawValue;
            return valueStream.str();
        }
        else if (typeBits == PIX_FORMAT_SPECIFIER_TYPE_BOOL)
        {
            return rawValue == 0 ? L"false" : L"true";
        }
        else if (typeBits == PIX_FORMAT_SPECIFIER_TYPE_HEX)
        {
            valueStream << L"0x" << std::uppercase << std::hex;

            if (sizeBits == PIX_FORMAT_SPECIFIER_SIZE_16BIT)
            {
                valueStream << static_cast<UINT16>(rawValue);
            }
            else if (sizeBits == PIX_FORMAT_SPECIFIER_SIZE_32BIT)
            {
                valueStream << static_cast<UINT32>(rawValue);
            }
            else
            {
                valueStream << rawValue;
            }

            return valueStream.str();
        }

        valueStream << rawValue;
        return valueStream.str();
    }

    void PrintOptionalFeatureUnavailable(
        const wchar_t* featureName,
        HRESULT hresult)
    {
        wprintf(L"\n%ls is not supported or unavailable for this capture on the current hardware.\n", featureName);
        wprintf(L"HRESULT: 0x%08X\n", static_cast<unsigned int>(hresult));
    }

    std::wstring FormatOptionalUInt64(UINT64 value)
    {
        if (value == PIX_EVENT_TIMING_NONE)
        {
            return L"(none)";
        }

        return std::to_wstring(value);
    }

    bool HasCounterSelection(
        std::vector<CounterSelection> const& selectedCounters,
        PIX_GPU_COUNTER_ID counterId)
    {
        for (CounterSelection const& selectedCounter : selectedCounters)
        {
            if (selectedCounter.CounterId == counterId)
            {
                return true;
            }
        }

        return false;
    }

    bool SelectCountersForCollection(
        IPixGpuCaptureCounters* gpuCounters,
        std::vector<CounterSelection>* selectedCounters)
    {
        wil::com_ptr_nothrow<IPixCollection> counterGroups;
        if (!PrintErrorAndReturnFalse(
            gpuCounters->GetCounterGroups(IID_PPV_ARGS(counterGroups.put())),
            L"GetCounterGroups failed"))
        {
            return false;
        }

        std::unordered_map<PIX_GPU_COUNTER_ID, std::wstring> counterGroupNamesById;

        wprintf(L"Available counter groups:\n");
        for (UINT64 groupIndex = 0; groupIndex < counterGroups->GetCount(); ++groupIndex)
        {
            wil::com_ptr_nothrow<IPixGpuCounterGroupDescription> counterGroup;
            if (!PrintErrorAndReturnFalse(
                counterGroups->Get(groupIndex, IID_PPV_ARGS(counterGroup.put())),
                L"Failed to get a counter group"))
            {
                return false;
            }

            wil::com_ptr_nothrow<IPixCollection> groupCounters;
            if (!PrintErrorAndReturnFalse(
                counterGroup->GetCounters(IID_PPV_ARGS(groupCounters.put())),
                L"GetCounters for a counter group failed"))
            {
                return false;
            }

            const wchar_t* counterGroupName = GetWideStringOrDefault(counterGroup->GetName());
            wprintf(L"  [%llu] %ls (%llu counters)\n",
                groupIndex,
                counterGroupName,
                groupCounters->GetCount());

            for (UINT64 counterIndex = 0; counterIndex < groupCounters->GetCount(); ++counterIndex)
            {
                wil::com_ptr_nothrow<IPixGpuCounterDescription> counterDescription;
                if (!PrintErrorAndReturnFalse(
                    groupCounters->Get(counterIndex, IID_PPV_ARGS(counterDescription.put())),
                    L"Failed to get a counter description from a group"))
                {
                    return false;
                }

                const PIX_GPU_COUNTER_ID counterId = counterDescription->GetId();
                counterGroupNamesById[counterId] = counterGroupName;

                // Pick the first 3 counters whose group name contains "D3D"
                // (Direct3D pipeline statistics, IA/VS/PS counts, etc.).
                // This narrows the selection to a small, comparable set;
                // a real tool would either let the user choose or take
                // every counter the API offers.
                if (!HasCounterSelection(*selectedCounters, counterId) &&
                    selectedCounters->size() < 3 &&
                    std::wstring_view(counterGroupName).find(L"D3D") != std::wstring_view::npos)
                {
                    selectedCounters->push_back({
                        counterId,
                        GetWideStringOrDefault(counterDescription->GetName()),
                        counterGroupName,
                        counterDescription->GetDataType() });
                }
            }
        }

        wil::com_ptr_nothrow<IPixCollection> counters;
        if (!PrintErrorAndReturnFalse(
            gpuCounters->GetCounters(IID_PPV_ARGS(counters.put())),
            L"GetCounters failed"))
        {
            return false;
        }

        wprintf(L"\nAvailable counters:\n");
        for (UINT64 counterIndex = 0; counterIndex < counters->GetCount(); ++counterIndex)
        {
            wil::com_ptr_nothrow<IPixGpuCounterDescription> counterDescription;
            if (!PrintErrorAndReturnFalse(
                counters->Get(counterIndex, IID_PPV_ARGS(counterDescription.put())),
                L"Failed to get a counter description"))
            {
                return false;
            }

            const PIX_GPU_COUNTER_ID counterId = counterDescription->GetId();
            auto counterGroupNameIterator = counterGroupNamesById.find(counterId);
            const wchar_t* counterGroupName =
                counterGroupNameIterator != counterGroupNamesById.end()
                ? counterGroupNameIterator->second.c_str()
                : L"(ungrouped)";

            wprintf(L"  [%u] %ls :: %ls\n",
                counterId,
                counterGroupName,
                GetWideStringOrDefault(counterDescription->GetName()));

            if (!HasCounterSelection(*selectedCounters, counterId) && selectedCounters->size() < 3)
            {
                selectedCounters->push_back({
                    counterId,
                    GetWideStringOrDefault(counterDescription->GetName()),
                    counterGroupName,
                    counterDescription->GetDataType() });
            }
        }

        if (counters->GetCount() == 0)
        {
            wprintf(L"No GPU counters were reported for this capture analysis.\n");
            return false;
        }

        return true;
    }

    bool FindPreferredQueue(
        IPixGpuCaptureDocument* captureDocument,
        wil::com_ptr_nothrow<IPixGpuCaptureQueueInfo>& preferredQueue)
    {
        wil::com_ptr_nothrow<IPixCollection> queues;
        if (!PrintErrorAndReturnFalse(
            captureDocument->GetQueues(IID_PPV_ARGS(queues.put())),
            L"GetQueues failed"))
        {
            return false;
        }

        for (UINT64 queueIndex = 0; queueIndex < queues->GetCount(); ++queueIndex)
        {
            wil::com_ptr_nothrow<IPixGpuCaptureQueueInfo> queueInfo;
            if (!PrintErrorAndReturnFalse(
                queues->Get(queueIndex, IID_PPV_ARGS(queueInfo.put())),
                L"Failed to get queue info"))
            {
                return false;
            }

            if (queueInfo->GetType() == PIX_QUEUE_TYPE_GRAPHICS)
            {
                preferredQueue = std::move(queueInfo);
                return true;
            }

            if (!preferredQueue && queueInfo->GetType() != PIX_QUEUE_TYPE_CPU)
            {
                preferredQueue = std::move(queueInfo);
            }
        }

        return preferredQueue != nullptr;
    }

    bool CollectAndPrintSampleCounterData(
        IPixGpuCaptureDocument* captureDocument,
        IPixGpuCaptureCounters* gpuCounters,
        std::vector<CounterSelection> const& selectedCounters)
    {
        wil::com_ptr_nothrow<IPixGpuCaptureQueueInfo> preferredQueue;
        if (!FindPreferredQueue(captureDocument, preferredQueue))
        {
            wprintf(L"\nNo GPU queue was found in the capture, so counter values were not collected.\n");
            return true;
        }

        std::vector<PIX_GPU_COUNTER_ID> counterIds;
        for (CounterSelection const& selectedCounter : selectedCounters)
        {
            counterIds.push_back(selectedCounter.CounterId);
        }

        wil::com_ptr_nothrow<IPixGpuCaptureCounterData> counterData;
        if (!PrintErrorAndReturnFalse(
            gpuCounters->CollectCounters(
                static_cast<UINT64>(counterIds.size()),
                counterIds.data(),
                IID_PPV_ARGS(counterData.put())),
            L"CollectCounters failed"))
        {
            return false;
        }

        UINT64 queueDataCount = 0;
        if (!PrintErrorAndReturnFalse(
            counterData->GetQueueDataCount(preferredQueue.get(), &queueDataCount),
            L"GetQueueDataCount failed"))
        {
            return false;
        }

        wprintf(L"\nCollected %llu counters on queue '%ls' (%u events).\n",
            static_cast<unsigned long long>(selectedCounters.size()),
            GetWideStringOrDefault(preferredQueue->GetName()),
            preferredQueue->GetEventCount());
        wprintf(L"Queue data points available: %llu\n", queueDataCount);

        for (CounterSelection const& selectedCounter : selectedCounters)
        {
            bool printedValue = false;

            for (UINT32 eventIndex = 0; eventIndex < preferredQueue->GetEventCount(); ++eventIndex)
            {
                PIX_EVENT_INFO eventInfo = {};
                if (!PrintErrorAndReturnFalse(
                    preferredQueue->GetEvent(eventIndex, &eventInfo),
                    L"GetEvent failed"))
                {
                    return false;
                }

                if (counterData->HasEventData(selectedCounter.CounterId, &eventInfo) == FALSE)
                {
                    continue;
                }

                UINT64 counterValue = PIX_EVENT_COUNTER_NONE;
                if (!PrintErrorAndReturnFalse(
                    counterData->GetEventData(selectedCounter.CounterId, &eventInfo, &counterValue),
                    L"GetEventData failed"))
                {
                    return false;
                }

                std::wstring formattedCounterValue = FormatCounterValue(counterValue, selectedCounter.DataType);
                wprintf(L"  %ls :: %ls = %ls (event %u: %S)\n",
                    selectedCounter.CounterGroupName.c_str(),
                    selectedCounter.CounterName.c_str(),
                    formattedCounterValue.c_str(),
                    eventIndex,
                    eventInfo.Name != nullptr ? eventInfo.Name : "(unnamed event)");
                printedValue = true;
                break;
            }

            if (!printedValue)
            {
                wprintf(L"  %ls :: %ls = (no event data found on queue '%ls')\n",
                    selectedCounter.CounterGroupName.c_str(),
                    selectedCounter.CounterName.c_str(),
                    GetWideStringOrDefault(preferredQueue->GetName()));
            }
        }

        return true;
    }

    bool CollectAndPrintSampleOccupancyData(IPixGpuCaptureAnalysis* analysis)
    {
        wil::com_ptr_nothrow<IPixGpuCaptureOccupancy> occupancy;
        const HRESULT getOccupancyResult = analysis->GetOccupancy(IID_PPV_ARGS(occupancy.put()));
        if (FAILED(getOccupancyResult))
        {
            PrintOptionalFeatureUnavailable(L"GPU occupancy", getOccupancyResult);
            return true;
        }

        wil::com_ptr_nothrow<IPixCollection> occupancyTypes;
        const HRESULT getOccupancyTypesResult = occupancy->GetOccupancyTypes(IID_PPV_ARGS(occupancyTypes.put()));
        if (FAILED(getOccupancyTypesResult))
        {
            PrintOptionalFeatureUnavailable(L"GPU occupancy", getOccupancyTypesResult);
            return true;
        }

        wil::com_ptr_nothrow<IPixCollection> occupancyStages;
        const HRESULT getOccupancyStagesResult = occupancy->GetOccupancyStages(IID_PPV_ARGS(occupancyStages.put()));
        if (FAILED(getOccupancyStagesResult))
        {
            PrintOptionalFeatureUnavailable(L"GPU occupancy", getOccupancyStagesResult);
            return true;
        }

        wprintf(L"\nAvailable occupancy types: %llu\n", occupancyTypes->GetCount());
        for (UINT64 typeIndex = 0; typeIndex < occupancyTypes->GetCount(); ++typeIndex)
        {
            wil::com_ptr_nothrow<IPixGpuCaptureOccupancyType> occupancyType;
            const HRESULT getOccupancyTypeResult = occupancyTypes->Get(typeIndex, IID_PPV_ARGS(occupancyType.put()));
            if (FAILED(getOccupancyTypeResult))
            {
                PrintOptionalFeatureUnavailable(L"GPU occupancy", getOccupancyTypeResult);
                return true;
            }

            wprintf(L"  [%llu] %ls (max slots: %u)\n",
                typeIndex,
                GetWideStringOrDefault(occupancyType->GetName()),
                occupancyType->GetMaxSlots());
        }

        wprintf(L"Available occupancy stages: %llu\n", occupancyStages->GetCount());
        for (UINT64 stageIndex = 0; stageIndex < occupancyStages->GetCount(); ++stageIndex)
        {
            wil::com_ptr_nothrow<IPixGpuCaptureOccupancyStage> occupancyStage;
            const HRESULT getOccupancyStageResult = occupancyStages->Get(stageIndex, IID_PPV_ARGS(occupancyStage.put()));
            if (FAILED(getOccupancyStageResult))
            {
                PrintOptionalFeatureUnavailable(L"GPU occupancy", getOccupancyStageResult);
                return true;
            }

            wprintf(L"  [%llu] %ls (%ls)\n",
                stageIndex,
                GetWideStringOrDefault(occupancyStage->GetName()),
                GetWideStringOrDefault(occupancyStage->GetAbbreviation()));
        }

        if (occupancyTypes->GetCount() == 0 || occupancyStages->GetCount() == 0)
        {
            wprintf(L"No occupancy types or stages were reported for this capture analysis.\n");
            return true;
        }

        wil::com_ptr_nothrow<IPixGpuCaptureOccupancyData> occupancyData;
        const HRESULT collectOccupancyResult = occupancy->CollectOccupancy(IID_PPV_ARGS(occupancyData.put()));
        if (FAILED(collectOccupancyResult))
        {
            PrintOptionalFeatureUnavailable(L"GPU occupancy", collectOccupancyResult);
            return true;
        }

        bool printedSummary = false;
        for (UINT64 typeIndex = 0; typeIndex < occupancyTypes->GetCount() && !printedSummary; ++typeIndex)
        {
            wil::com_ptr_nothrow<IPixGpuCaptureOccupancyType> occupancyType;
            const HRESULT getOccupancyTypeResult = occupancyTypes->Get(typeIndex, IID_PPV_ARGS(occupancyType.put()));
            if (FAILED(getOccupancyTypeResult))
            {
                PrintOptionalFeatureUnavailable(L"GPU occupancy", getOccupancyTypeResult);
                return true;
            }

            for (UINT64 stageIndex = 0; stageIndex < occupancyStages->GetCount() && !printedSummary; ++stageIndex)
            {
                wil::com_ptr_nothrow<IPixGpuCaptureOccupancyStage> occupancyStage;
                const HRESULT getOccupancyStageResult = occupancyStages->Get(stageIndex, IID_PPV_ARGS(occupancyStage.put()));
                if (FAILED(getOccupancyStageResult))
                {
                    PrintOptionalFeatureUnavailable(L"GPU occupancy", getOccupancyStageResult);
                    return true;
                }

                UINT64 occupancyPointCount = 0;
                PIX_OCCUPANCY_POINT const* occupancyPoints = nullptr;
                const HRESULT getPointsResult = occupancyData->GetPoints(
                    occupancyType.get(),
                    occupancyStage.get(),
                    &occupancyPointCount,
                    &occupancyPoints);
                if (FAILED(getPointsResult) || occupancyPointCount == 0 || occupancyPoints == nullptr)
                {
                    continue;
                }

                PIX_OCCUPANCY_POINT const& firstPoint = occupancyPoints[0];
                PIX_OCCUPANCY_POINT const& lastPoint = occupancyPoints[occupancyPointCount - 1];
                wprintf(L"Collected %llu occupancy points for %ls / %ls.\n",
                    occupancyPointCount,
                    GetWideStringOrDefault(occupancyType->GetName()),
                    GetWideStringOrDefault(occupancyStage->GetName()));
                wprintf(L"  First point: time=%llu ns slots=%u\n",
                    firstPoint.TimeNanoseconds,
                    firstPoint.Slots);
                wprintf(L"  Last point:  time=%llu ns slots=%u\n",
                    lastPoint.TimeNanoseconds,
                    lastPoint.Slots);
                printedSummary = true;
            }
        }

        if (!printedSummary)
        {
            wprintf(L"Occupancy data was collected, but no sample points were returned.\n");
        }

        return true;
    }

    bool CollectAndPrintSampleHighFrequencyCounterData(IPixGpuCaptureAnalysis* analysis)
    {
        wil::com_ptr_nothrow<IPixGpuCaptureHighFrequencyCounters> highFrequencyCounters;
        const HRESULT getHighFrequencyCountersResult = analysis->GetHighFrequencyCounters(
            IID_PPV_ARGS(highFrequencyCounters.put()));
        if (FAILED(getHighFrequencyCountersResult))
        {
            PrintOptionalFeatureUnavailable(L"High-frequency counters", getHighFrequencyCountersResult);
            return true;
        }

        wil::com_ptr_nothrow<IPixCollection> counters;
        const HRESULT getCountersResult = highFrequencyCounters->GetCounters(IID_PPV_ARGS(counters.put()));
        if (FAILED(getCountersResult))
        {
            PrintOptionalFeatureUnavailable(L"High-frequency counters", getCountersResult);
            return true;
        }

        wil::com_ptr_nothrow<IPixCollection> counterGroups;
        const HRESULT getCounterGroupsResult = highFrequencyCounters->GetCounterGroups(IID_PPV_ARGS(counterGroups.put()));
        if (FAILED(getCounterGroupsResult))
        {
            PrintOptionalFeatureUnavailable(L"High-frequency counters", getCounterGroupsResult);
            return true;
        }

        wil::com_ptr_nothrow<IPixCollection> counterSets;
        const HRESULT getCounterSetsResult = highFrequencyCounters->GetGpuCounterSets(IID_PPV_ARGS(counterSets.put()));
        if (FAILED(getCounterSetsResult))
        {
            PrintOptionalFeatureUnavailable(L"High-frequency counters", getCounterSetsResult);
            return true;
        }

        wprintf(L"\nAvailable high-frequency counters: %llu\n", counters->GetCount());
        const UINT64 countersToPrint = counters->GetCount() < 8 ? counters->GetCount() : 8;
        for (UINT64 counterIndex = 0; counterIndex < countersToPrint; ++counterIndex)
        {
            wil::com_ptr_nothrow<IPixGpuCaptureHighFrequencyCounter> counter;
            const HRESULT getCounterResult = counters->Get(counterIndex, IID_PPV_ARGS(counter.put()));
            if (FAILED(getCounterResult))
            {
                PrintOptionalFeatureUnavailable(L"High-frequency counters", getCounterResult);
                return true;
            }

            wprintf(L"  [%llu] %ls\n",
                counterIndex,
                GetWideStringOrDefault(counter->GetName()));
        }

        wprintf(L"Available high-frequency counter groups: %llu\n", counterGroups->GetCount());
        for (UINT64 groupIndex = 0; groupIndex < counterGroups->GetCount(); ++groupIndex)
        {
            wil::com_ptr_nothrow<IPixGpuCaptureCounterCollection> counterGroup;
            const HRESULT getCounterGroupResult = counterGroups->Get(groupIndex, IID_PPV_ARGS(counterGroup.put()));
            if (FAILED(getCounterGroupResult))
            {
                PrintOptionalFeatureUnavailable(L"High-frequency counters", getCounterGroupResult);
                return true;
            }

            wprintf(L"  [%llu] %ls (%llu counters)\n",
                groupIndex,
                GetWideStringOrDefault(counterGroup->GetName()),
                counterGroup->GetCount());
        }

        wprintf(L"Available high-frequency counter sets: %llu\n", counterSets->GetCount());
        for (UINT64 setIndex = 0; setIndex < counterSets->GetCount(); ++setIndex)
        {
            wil::com_ptr_nothrow<IPixGpuCaptureCounterCollection> counterSet;
            const HRESULT getCounterSetResult = counterSets->Get(setIndex, IID_PPV_ARGS(counterSet.put()));
            if (FAILED(getCounterSetResult))
            {
                PrintOptionalFeatureUnavailable(L"High-frequency counters", getCounterSetResult);
                return true;
            }

            wprintf(L"  [%llu] %ls (%llu counters)\n",
                setIndex,
                GetWideStringOrDefault(counterSet->GetName()),
                counterSet->GetCount());
        }

        if (counterSets->GetCount() == 0)
        {
            wprintf(L"No high-frequency counter sets were reported for this capture analysis.\n");
            return true;
        }

        wil::com_ptr_nothrow<IPixGpuCaptureHighFrequencyCounterData> counterData;
        const HRESULT collectCounterDataResult = highFrequencyCounters->CollectCounterData(
            counterSets.get(),
            IID_PPV_ARGS(counterData.put()));
        if (FAILED(collectCounterDataResult))
        {
            PrintOptionalFeatureUnavailable(L"High-frequency counters", collectCounterDataResult);
            return true;
        }

        wil::com_ptr_nothrow<IPixGpuCaptureCounterCollection> firstCounterSet;
        const HRESULT getFirstCounterSetResult = counterSets->Get(0, IID_PPV_ARGS(firstCounterSet.put()));
        if (FAILED(getFirstCounterSetResult))
        {
            PrintOptionalFeatureUnavailable(L"High-frequency counters", getFirstCounterSetResult);
            return true;
        }

        if (firstCounterSet->GetCount() == 0)
        {
            wprintf(L"The first high-frequency counter set is empty.\n");
            return true;
        }

        wil::com_ptr_nothrow<IPixGpuCaptureHighFrequencyCounter> firstCounter;
        const HRESULT getFirstCounterResult = firstCounterSet->Get(0, IID_PPV_ARGS(firstCounter.put()));
        if (FAILED(getFirstCounterResult))
        {
            PrintOptionalFeatureUnavailable(L"High-frequency counters", getFirstCounterResult);
            return true;
        }

        UINT64 batchId = 0;
        UINT64 sampleCount = 0;
        PIX_HIGH_FREQUENCY_COUNTER_TIMESTAMP_NS const* sampleTimeStamps = nullptr;
        PIX_HIGH_FREQUENCY_COUNTER_VALUE const* sampleValues = nullptr;
        const HRESULT getSamplesResult = counterData->GetSamples(
            firstCounterSet.get(),
            firstCounter.get(),
            &batchId,
            &sampleCount,
            &sampleTimeStamps,
            &sampleValues);
        if (FAILED(getSamplesResult))
        {
            PrintOptionalFeatureUnavailable(L"High-frequency counters", getSamplesResult);
            return true;
        }

        wprintf(L"Collected %llu high-frequency samples for set '%ls' and counter '%ls' (batch %llu).\n",
            sampleCount,
            GetWideStringOrDefault(firstCounterSet->GetName()),
            GetWideStringOrDefault(firstCounter->GetName()),
            batchId);

        const UINT64 samplesToPrint = sampleCount < 5 ? sampleCount : 5;
        for (UINT64 sampleIndex = 0; sampleIndex < samplesToPrint; ++sampleIndex)
        {
            wprintf(L"  [%llu] %llu ns -> %g\n",
                sampleIndex,
                sampleTimeStamps[sampleIndex],
                sampleValues[sampleIndex]);
        }

        if (sampleCount == 0)
        {
            wprintf(L"No high-frequency samples were returned for the first counter set.\n");
        }

        return true;
    }

    bool CollectAndPrintPerEventTimingData(
        IPixGpuCaptureDocument* captureDocument,
        IPixGpuCaptureAnalysis* analysis)
    {
        wil::com_ptr_nothrow<IPixGpuCaptureTiming> timing;
        const HRESULT collectTimingResult = analysis->CollectTiming(IID_PPV_ARGS(timing.put()));
        if (FAILED(collectTimingResult))
        {
            PrintOptionalFeatureUnavailable(L"Per-event GPU timing", collectTimingResult);
            return true;
        }

        wil::com_ptr_nothrow<IPixGpuCaptureQueueInfo> preferredQueue;
        if (!FindPreferredQueue(captureDocument, preferredQueue))
        {
            wprintf(L"\nNo GPU queue was found in the capture, so timing values were not collected.\n");
            return true;
        }

        UINT64 queueDataCount = 0;
        const HRESULT getQueueDataCountResult = timing->GetQueueDataCount(preferredQueue.get(), &queueDataCount);
        if (FAILED(getQueueDataCountResult))
        {
            PrintOptionalFeatureUnavailable(L"Per-event GPU timing", getQueueDataCountResult);
            return true;
        }

        wprintf(L"\nQueue '%ls' has %llu timing records.\n",
            GetWideStringOrDefault(preferredQueue->GetName()),
            queueDataCount);

        UINT32 printedTimingEvents = 0;
        for (UINT32 eventIndex = 0;
            eventIndex < preferredQueue->GetEventCount() && printedTimingEvents < 5;
            ++eventIndex)
        {
            PIX_EVENT_INFO eventInfo = {};
            const HRESULT getEventResult = preferredQueue->GetEvent(eventIndex, &eventInfo);
            if (FAILED(getEventResult))
            {
                PrintOptionalFeatureUnavailable(L"Per-event GPU timing", getEventResult);
                return true;
            }

            if (timing->HasEventData(&eventInfo) == FALSE)
            {
                continue;
            }

            PIX_EVENT_TIMING eventTiming = {};
            const HRESULT getEventDataResult = timing->GetEventData(&eventInfo, &eventTiming);
            if (FAILED(getEventDataResult))
            {
                PrintOptionalFeatureUnavailable(L"Per-event GPU timing", getEventDataResult);
                return true;
            }

            const UINT64 topEnd =
                eventTiming.TopStart != PIX_EVENT_TIMING_NONE && eventTiming.TopDuration != PIX_EVENT_TIMING_NONE
                ? eventTiming.TopStart + eventTiming.TopDuration
                : PIX_EVENT_TIMING_NONE;
            const UINT64 eopEnd =
                eventTiming.EopStart != PIX_EVENT_TIMING_NONE && eventTiming.EopDuration != PIX_EVENT_TIMING_NONE
                ? eventTiming.EopStart + eventTiming.EopDuration
                : PIX_EVENT_TIMING_NONE;

            wprintf(L"  [%u] %S\n",
                eventIndex,
                eventInfo.Name != nullptr ? eventInfo.Name : "(unnamed event)");
            wprintf(L"      Top: start=%ls ns duration=%ls ns end=%ls ns\n",
                FormatOptionalUInt64(eventTiming.TopStart).c_str(),
                FormatOptionalUInt64(eventTiming.TopDuration).c_str(),
                FormatOptionalUInt64(topEnd).c_str());
            wprintf(L"      EOP: start=%ls ns duration=%ls ns end=%ls ns\n",
                FormatOptionalUInt64(eventTiming.EopStart).c_str(),
                FormatOptionalUInt64(eventTiming.EopDuration).c_str(),
                FormatOptionalUInt64(eopEnd).c_str());
            ++printedTimingEvents;
        }

        if (printedTimingEvents == 0)
        {
            wprintf(L"No per-event GPU timing data was reported for the selected queue.\n");
        }

        return true;
    }
}

int wmain(int argumentCount, wchar_t* argumentValues[])
{
    if (argumentCount != 2)
    {
        wprintf(L"Usage: GpuCounters.exe <path-to-capture.wpix>\n");
        return 1;
    }

    const std::filesystem::path captureFilePath = std::filesystem::absolute(argumentValues[1]);
    std::error_code fileSystemError;
    if (!std::filesystem::exists(captureFilePath, fileSystemError))
    {
        wprintf(L"Capture file not found: %ls\n", captureFilePath.c_str());
        return 1;
    }

    // Step 1: Create the PIX factory.
    wil::com_ptr_nothrow<IPixFactory> factory;
    if (!PrintErrorAndReturnFalse(
        PixCreateFactory(IID_PPV_ARGS(factory.put())),
        L"PixCreateFactory failed"))
    {
        return 1;
    }

    // Step 2: Open the GPU capture document.
    wil::com_ptr_nothrow<IPixGpuCaptureDocument> captureDocument;
    if (!PrintErrorAndReturnFalse(
        factory->OpenGpuCaptureDocument(captureFilePath.c_str(), IID_PPV_ARGS(captureDocument.put())),
        L"OpenGpuCaptureDocument failed"))
    {
        return 1;
    }

    // Step 3: Get the analysis interface from the capture document.
    wil::com_ptr_nothrow<IPixGpuCaptureAnalysis> analysis;
    if (!PrintErrorAndReturnFalse(
        captureDocument->GetAnalysis(IID_PPV_ARGS(analysis.put())),
        L"GetAnalysis failed"))
    {
        return 1;
    }

    // Step 4: Connect analysis to the local GPU.
    PIX_CONNECTION_DESC_LOCAL localConnection = {};
    PIX_CONNECTION_DESC analysisConnectionDescription = {};
    analysisConnectionDescription.Type = PIX_CONNECTION_TYPE_LOCAL;
    analysisConnectionDescription.pLocal = &localConnection;

    if (!PrintErrorAndReturnFalse(
        analysis->Connect(&analysisConnectionDescription, nullptr),
        L"Connect failed"))
    {
        return 1;
    }

    auto disconnectAnalysis = wil::scope_exit([&analysis]()
    {
        if (analysis)
        {
            analysis->Disconnect();
        }
    });

    // Step 5: Start analysis (replay). This can take time.
    wprintf(L"Starting analysis for '%ls'. This may take a moment...\n", captureFilePath.c_str());
    if (!PrintErrorAndReturnFalse(
        analysis->StartAnalysis(nullptr, nullptr, nullptr),
        L"StartAnalysis failed"))
    {
        return 1;
    }

    auto stopAnalysis = wil::scope_exit([&analysis]()
    {
        if (analysis)
        {
            analysis->StopAnalysis();
        }
    });

    // Step 6: Get the GPU counter interface.
    wil::com_ptr_nothrow<IPixGpuCaptureCounters> gpuCounters;
    if (!PrintErrorAndReturnFalse(
        analysis->GetGpuCounters(IID_PPV_ARGS(gpuCounters.put())),
        L"GetGpuCounters failed"))
    {
        return 1;
    }

    // Step 7: Enumerate available counters and counter groups.
    std::vector<CounterSelection> selectedCounters;
    if (!SelectCountersForCollection(gpuCounters.get(), &selectedCounters))
    {
        return 1;
    }

    // Step 8: Collect counter data for a few counters.
    if (!CollectAndPrintSampleCounterData(captureDocument.get(), gpuCounters.get(), selectedCounters))
    {
        return 1;
    }

    // Step 9: Collect GPU occupancy.
    if (!CollectAndPrintSampleOccupancyData(analysis.get()))
    {
        return 1;
    }

    // Step 10: Collect high-frequency counters.
    if (!CollectAndPrintSampleHighFrequencyCounterData(analysis.get()))
    {
        return 1;
    }

    // Step 11: Read per-event GPU timing.
    if (!CollectAndPrintPerEventTimingData(captureDocument.get(), analysis.get()))
    {
        return 1;
    }

    // Step 12: Stop analysis and disconnect.
    wprintf(L"\nFinished collecting GPU counters, occupancy, high-frequency counters, and timing data.\n");
    return 0;
}
