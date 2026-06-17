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
// Dr. PIX Sample (C++)
//
// Demonstrates how to run Dr. PIX automated experiments on an existing
// GPU capture:
//   1. Create a PIX factory and open a .wpix GPU capture document.
//   2. Get analysis for the capture, connect to the local GPU, and start it.
//   3. Get IPixGpuCaptureDrPix and enumerate the available experiments.
//   4. Run a few experiments across the GPU work in the capture.
//   5. Receive async callbacks as results become available.
//   6. Wait for completion and print messages and metrics.
//

#include <wrl/client.h>
#include <wrl/implements.h>
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::MakeAndInitialize;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "PixApi.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// PIX error codes (facility 0xABC).
constexpr HRESULT E_PIX_DEVELOPER_MODE_NOT_ENABLED = 0x8abc0000;
constexpr HRESULT E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE = 0x8abc0001;
// Arbitrary caps to keep the sample's output small and quick. There is
// no PIX API limit on the number of experiments or per-experiment metrics
// you can run; production code should run all experiments and print all
// metrics, or expose these caps as command-line arguments.
constexpr UINT64 MaximumExperimentsToRun = 3;
constexpr UINT64 MaximumMetricsToPrintPerExperiment = 10;

struct ExperimentDescription
{
    GUID Guid = {};
    std::wstring Name;
    std::wstring Category;
    std::wstring HelpText;
    PIX_EXPERIMENT_SOURCE Source = PIX_EXPERIMENT_SOURCE_PIX;
};

HRESULT PrintFailure(HRESULT hr, wchar_t const* errorMessage)
{
    if (SUCCEEDED(hr))
    {
        return hr;
    }

    if (hr == E_PIX_DEVELOPER_MODE_NOT_ENABLED || hr == E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE)
    {
        wprintf(L"ERROR: Windows Developer Mode is required for this operation.\n");
        wprintf(L"Enable it in Settings > Privacy & security > For developers.\n");
    }

    wprintf(L"%ls\n", errorMessage);
    wprintf(L"Error code: 0x%08X\n", static_cast<unsigned int>(hr));
    return hr;
}

wchar_t const* GetExperimentSourceName(PIX_EXPERIMENT_SOURCE experimentSource)
{
    switch (experimentSource)
    {
    case PIX_EXPERIMENT_SOURCE_PIX:
        return L"PIX";
    case PIX_EXPERIMENT_SOURCE_GPU_PLUGIN:
        return L"GPU Plugin";
    default:
        return L"Unknown";
    }
}

wchar_t const* GetMessageTypeName(PIX_MESSAGE_TYPE messageType)
{
    switch (messageType)
    {
    case PIX_MESSAGE_TYPE_INFO:
        return L"Info";
    case PIX_MESSAGE_TYPE_WARNING:
        return L"Warning";
    case PIX_MESSAGE_TYPE_ERROR:
        return L"Error";
    default:
        return L"Unknown";
    }
}

std::wstring GetExperimentName(
    GUID const& experimentGuid,
    std::vector<ExperimentDescription> const& experimentDescriptions)
{
    for (ExperimentDescription const& experimentDescription : experimentDescriptions)
    {
        if (InlineIsEqualGUID(experimentDescription.Guid, experimentGuid))
        {
            return experimentDescription.Name;
        }
    }

    return L"Unknown Experiment";
}

void PrintPixValue(PIX_VALUE const& value)
{
    if (value.ValueType == PIX_VALUE_STRING)
    {
        wprintf(L"%ls", value.Value.ValueString ? value.Value.ValueString : L"(null)");
        return;
    }

    if (value.ValueType != PIX_VALUE_NUMERIC)
    {
        wprintf(L"<unknown>");
        return;
    }

    PIX_NUMERIC_VALUE const& numericValue = value.Value.ValueNumeric;

    switch (numericValue.FormatSpecifier)
    {
    case PIX_FORMAT_SPECIFIER_BOOL8:
    case PIX_FORMAT_SPECIFIER_BOOL32:
        wprintf(L"%ls", numericValue.Bits != 0 ? L"true" : L"false");
        return;

    case PIX_FORMAT_SPECIFIER_INT32:
        wprintf(L"%d", static_cast<int>(static_cast<INT32>(numericValue.Bits)));
        return;

    case PIX_FORMAT_SPECIFIER_UINT32:
        wprintf(L"%u", static_cast<unsigned int>(static_cast<UINT32>(numericValue.Bits)));
        return;

    case PIX_FORMAT_SPECIFIER_INT64:
        wprintf(L"%lld", static_cast<long long>(static_cast<INT64>(numericValue.Bits)));
        return;

    case PIX_FORMAT_SPECIFIER_UINT64:
        wprintf(L"%llu", static_cast<unsigned long long>(numericValue.Bits));
        return;

    case PIX_FORMAT_SPECIFIER_FLOAT32:
        {
            UINT32 bits = static_cast<UINT32>(numericValue.Bits);
            float floatValue = 0.0f;
            std::memcpy(&floatValue, &bits, sizeof(floatValue));
            wprintf(L"%f", static_cast<double>(floatValue));
            return;
        }

    case PIX_FORMAT_SPECIFIER_FLOAT64:
        {
            double doubleValue = 0.0;
            std::memcpy(&doubleValue, &numericValue.Bits, sizeof(doubleValue));
            wprintf(L"%f", doubleValue);
            return;
        }

    default:
        wprintf(L"0x%llX", static_cast<unsigned long long>(numericValue.Bits));
        return;
    }
}

HRESULT FindCaptureEventRange(
    IPixGpuCaptureDocument* captureDocument,
    PIX_EVENT_INFO* firstEvent,
    PIX_EVENT_INFO* lastEvent)
{
    if (captureDocument == nullptr || firstEvent == nullptr || lastEvent == nullptr)
    {
        return E_POINTER;
    }

    ComPtr<IPixCollection> queues;
    HRESULT hr = captureDocument->GetQueues(IID_PPV_ARGS(queues.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        return hr;
    }

    PIX_EVENT_INFO firstGpuEvent = {};
    PIX_EVENT_INFO lastGpuEvent = {};
    bool foundGpuEvent = false;

    for (UINT64 queueIndex = 0; queueIndex < queues->GetCount(); ++queueIndex)
    {
        ComPtr<IPixGpuCaptureQueueInfo> queueInfo;
        hr = queues->Get(queueIndex, IID_PPV_ARGS(queueInfo.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
        {
            return hr;
        }

        for (UINT32 eventIndex = 0; eventIndex < queueInfo->GetEventCount(); ++eventIndex)
        {
            PIX_EVENT_INFO eventInfo = {};
            hr = queueInfo->GetEvent(eventIndex, &eventInfo);
            if (FAILED(hr))
            {
                return hr;
            }

            if (eventInfo.GpuId == UINT32_MAX)
            {
                continue;
            }

            if (!foundGpuEvent || eventInfo.GpuId < firstGpuEvent.GpuId)
            {
                firstGpuEvent = eventInfo;
            }

            if (!foundGpuEvent || eventInfo.GpuId > lastGpuEvent.GpuId)
            {
                lastGpuEvent = eventInfo;
            }

            foundGpuEvent = true;
        }
    }

    if (!foundGpuEvent)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    *firstEvent = firstGpuEvent;
    *lastEvent = lastGpuEvent;
    return S_OK;
}

HRESULT GetExperimentDescriptions(
    IPixGpuCaptureDrPix* drPix,
    std::vector<ExperimentDescription>* experimentDescriptions)
{
    if (drPix == nullptr || experimentDescriptions == nullptr)
    {
        return E_POINTER;
    }

    experimentDescriptions->clear();

    UINT64 experimentCount = drPix->GetExperimentCount();
    experimentDescriptions->reserve(static_cast<size_t>(experimentCount));

    for (UINT64 experimentIndex = 0; experimentIndex < experimentCount; ++experimentIndex)
    {
        PIX_EXPERIMENT_DESC pixExperimentDescription = {};
        HRESULT hr = drPix->GetExperiment(experimentIndex, &pixExperimentDescription);
        if (FAILED(hr))
        {
            return hr;
        }

        ExperimentDescription experimentDescription;
        experimentDescription.Guid = pixExperimentDescription.Guid;
        experimentDescription.Name = pixExperimentDescription.Name ? pixExperimentDescription.Name : L"";
        experimentDescription.Category = pixExperimentDescription.Category ? pixExperimentDescription.Category : L"";
        experimentDescription.HelpText = pixExperimentDescription.HelpText ? pixExperimentDescription.HelpText : L"";
        experimentDescription.Source = pixExperimentDescription.Source;
        experimentDescriptions->push_back(std::move(experimentDescription));
    }

    return S_OK;
}

void PrintExperimentDescriptions(std::vector<ExperimentDescription> const& experimentDescriptions)
{
    wprintf(L"Available Dr. PIX experiments: %llu\n", static_cast<unsigned long long>(experimentDescriptions.size()));

    for (size_t experimentIndex = 0; experimentIndex < experimentDescriptions.size(); ++experimentIndex)
    {
        ExperimentDescription const& experimentDescription = experimentDescriptions[experimentIndex];
        wprintf(
            L"  [%zu] %ls\n"
            L"      Category: %ls\n"
            L"      Source: %ls\n"
            L"      Description: %ls\n",
            experimentIndex,
            experimentDescription.Name.c_str(),
            experimentDescription.Category.c_str(),
            GetExperimentSourceName(experimentDescription.Source),
            experimentDescription.HelpText.c_str());
    }
}

class ExperimentCallback final : public RuntimeClass<RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IPixGpuCaptureExperimentCallback>
{
public:
    HRESULT RuntimeClassInitialize(std::vector<ExperimentDescription> const& experimentDescriptions)
    {
        m_experimentDescriptions = experimentDescriptions;
        return S_OK;
    }

    void STDMETHODCALLTYPE OnExperimentResultAvailable(IUnknown* resultUnknown) override
    {
        if (resultUnknown == nullptr)
        {
            wprintf(L"Callback: result became available, but the result object was null.\n");
            return;
        }

        ComPtr<IPixGpuCaptureExperimentResult> experimentResult;
        HRESULT hr = resultUnknown->QueryInterface(IID_PPV_ARGS(experimentResult.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
        {
            wprintf(L"Callback: received an unexpected result object (0x%08X).\n", static_cast<unsigned int>(hr));
            return;
        }

        PIX_EXPERIMENT_RUN_PARAMS experimentRunParameters = {};
        hr = experimentResult->GetExperimentRunParams(&experimentRunParameters);
        if (FAILED(hr))
        {
            wprintf(L"Callback: failed to query experiment run parameters (0x%08X).\n", static_cast<unsigned int>(hr));
            return;
        }

        HRESULT experimentStatus = S_OK;
        hr = experimentResult->GetExperimentStatus(&experimentStatus);
        if (FAILED(hr))
        {
            experimentStatus = hr;
        }

        ++m_callbackCount;
        std::wstring experimentName = GetExperimentName(experimentRunParameters.ExperimentGuid, m_experimentDescriptions);
        wprintf(
            L"Callback: result %u is available for \"%ls\" (status 0x%08X).\n",
            m_callbackCount,
            experimentName.c_str(),
            static_cast<unsigned int>(experimentStatus));
    }

private:
    std::vector<ExperimentDescription> m_experimentDescriptions;
    unsigned int m_callbackCount = 0;
};

HRESULT PrintExperimentResult(
    IPixGpuCaptureExperimentResult* experimentResult,
    std::vector<ExperimentDescription> const& experimentDescriptions)
{
    if (experimentResult == nullptr)
    {
        return E_POINTER;
    }

    PIX_EXPERIMENT_RUN_PARAMS experimentRunParameters = {};
    HRESULT hr = experimentResult->GetExperimentRunParams(&experimentRunParameters);
    if (FAILED(hr))
    {
        return hr;
    }

    HRESULT experimentStatus = S_OK;
    hr = experimentResult->GetExperimentStatus(&experimentStatus);
    if (FAILED(hr))
    {
        return hr;
    }

    std::wstring experimentName = GetExperimentName(experimentRunParameters.ExperimentGuid, experimentDescriptions);
    wprintf(L"Experiment result for \"%ls\"\n", experimentName.c_str());
    wprintf(L"    Status: 0x%08X\n", static_cast<unsigned int>(experimentStatus));

    UINT64 messageCount = experimentResult->GetExperimentMessageCount();
    wprintf(L"    Messages: %llu\n", static_cast<unsigned long long>(messageCount));
    for (UINT64 messageIndex = 0; messageIndex < messageCount; ++messageIndex)
    {
        PIX_MESSAGE message = {};
        hr = experimentResult->GetExperimentMessage(messageIndex, &message);
        if (FAILED(hr))
        {
            return hr;
        }

        wprintf(
            L"        [%ls] %ls\n",
            GetMessageTypeName(message.Type),
            message.Message ? message.Message : L"");
    }

    UINT64 metricCount = experimentResult->GetExperimentMetricCount();
    UINT64 metricCountToPrint = (std::min)(metricCount, MaximumMetricsToPrintPerExperiment);
    wprintf(
        L"    Metrics: %llu (printing %llu)\n",
        static_cast<unsigned long long>(metricCount),
        static_cast<unsigned long long>(metricCountToPrint));

    for (UINT64 metricIndex = 0; metricIndex < metricCountToPrint; ++metricIndex)
    {
        PIX_EXPERIMENT_METRIC metric = {};
        hr = experimentResult->GetExperimentMetric(metricIndex, &metric);
        if (FAILED(hr))
        {
            return hr;
        }

        wprintf(L"        [%ls] ", metric.GroupName ? metric.GroupName : L"");
        for (UINT32 depthIndex = 0; depthIndex < metric.Depth; ++depthIndex)
        {
            wprintf(L"  ");
        }

        wprintf(
            L"%ls (%ls) = ",
            metric.Name ? metric.Name : L"",
            metric.ValueLabel ? metric.ValueLabel : L"");
        PrintPixValue(metric.Value);
        wprintf(L"\n");
    }

    if (metricCount > metricCountToPrint)
    {
        wprintf(
            L"        ... %llu additional metrics omitted ...\n",
            static_cast<unsigned long long>(metricCount - metricCountToPrint));
    }

    return S_OK;
}

HRESULT RunSample(std::filesystem::path const& capturePath)
{
    HRESULT hr = S_OK;
    bool analysisConnected = false;
    bool analysisStarted = false;
    ComPtr<IPixFactory> factory;
    ComPtr<IPixGpuCaptureDocument> captureDocument;
    ComPtr<IPixGpuCaptureAnalysis> analysis;
    ComPtr<IPixGpuCaptureDrPix> drPix;
    std::vector<ExperimentDescription> experimentDescriptions;
    ComPtr<ExperimentCallback> experimentCallback;
    PIX_EVENT_INFO firstEvent = {};
    PIX_EVENT_INFO lastEvent = {};
    UINT64 experimentCountToRun = 0;
    std::vector<PIX_EXPERIMENT_RUN_PARAMS> experimentRunParameters;
    ComPtr<IPixAsyncOperation> asyncOperation;
    ComPtr<IPixCollection> experimentResults;

    // Step 1: Create the PIX factory.
    wprintf(L"Step 1: Creating PIX factory.\n");
    hr = PixCreateFactory(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        return PrintFailure(hr, L"PixCreateFactory failed");
    }

    // Step 2: Open the GPU capture document.
    wprintf(L"Step 2: Opening GPU capture: %ls\n", capturePath.c_str());
    hr = factory->OpenGpuCaptureDocument(capturePath.c_str(), IID_PPV_ARGS(captureDocument.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        return PrintFailure(hr, L"OpenGpuCaptureDocument failed");
    }

    // Step 3: Get the capture analysis object.
    wprintf(L"Step 3: Getting capture analysis.\n");
    hr = captureDocument->GetAnalysis(IID_PPV_ARGS(analysis.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        return PrintFailure(hr, L"GetAnalysis failed");
    }

    // Step 4: Connect analysis to the local device and start it.
    wprintf(L"Step 4: Connecting analysis and starting it.\n");
    PIX_CONNECTION_DESC_LOCAL localConnection = {};
    PIX_CONNECTION_DESC connectionDescription = {};
    connectionDescription.Type = PIX_CONNECTION_TYPE_LOCAL;
    connectionDescription.pLocal = &localConnection;

    hr = analysis->Connect(&connectionDescription, nullptr);
    if (FAILED(hr))
    {
        return PrintFailure(hr, L"IPixGpuCaptureAnalysis::Connect failed");
    }
    analysisConnected = true;

    hr = analysis->StartAnalysis(nullptr, nullptr, nullptr);
    if (FAILED(hr))
    {
        hr = PrintFailure(hr, L"IPixGpuCaptureAnalysis::StartAnalysis failed");
        goto Cleanup;
    }
    analysisStarted = true;

    // Step 5: Get Dr. PIX.
    wprintf(L"Step 5: Getting Dr. PIX.\n");
    hr = analysis->GetDrPix(IID_PPV_ARGS(drPix.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        hr = PrintFailure(hr, L"IPixGpuCaptureAnalysis::GetDrPix failed");
        goto Cleanup;
    }

    // Step 6: Enumerate the available experiments.
    wprintf(L"Step 6: Enumerating experiments.\n");
    hr = GetExperimentDescriptions(drPix.Get(), &experimentDescriptions);
    if (FAILED(hr))
    {
        hr = PrintFailure(hr, L"IPixGpuCaptureDrPix::GetExperiment failed");
        goto Cleanup;
    }

    if (experimentDescriptions.empty())
    {
        hr = PrintFailure(HRESULT_FROM_WIN32(ERROR_NOT_FOUND), L"No Dr. PIX experiments are available for this capture");
        goto Cleanup;
    }

    // Step 7: Print the experiment metadata.
    PrintExperimentDescriptions(experimentDescriptions);

    // Step 8: Build a callback that reports async results as they finish.
    hr = MakeAndInitialize<ExperimentCallback>(&experimentCallback, experimentDescriptions);
    if (FAILED(hr))
    {
        hr = PrintFailure(hr, L"Failed to create the experiment callback");
        goto Cleanup;
    }

    // Step 9: Run a few experiments across the capture's GPU event range.
    wprintf(L"Step 9: Preparing experiments to run.\n");
    hr = FindCaptureEventRange(captureDocument.Get(), &firstEvent, &lastEvent);
    if (FAILED(hr))
    {
        hr = PrintFailure(hr, L"Failed to find a GPU event range in the capture");
        goto Cleanup;
    }

    experimentCountToRun = (std::min)(static_cast<UINT64>(experimentDescriptions.size()), MaximumExperimentsToRun);
    experimentRunParameters.assign(static_cast<size_t>(experimentCountToRun), PIX_EXPERIMENT_RUN_PARAMS{});
    for (UINT64 experimentIndex = 0; experimentIndex < experimentCountToRun; ++experimentIndex)
    {
        experimentRunParameters[experimentIndex].ExperimentGuid = experimentDescriptions[experimentIndex].Guid;
        experimentRunParameters[experimentIndex].FirstEvent = firstEvent;
        experimentRunParameters[experimentIndex].LastEvent = lastEvent;
        wprintf(L"  Queued experiment: %ls\n", experimentDescriptions[experimentIndex].Name.c_str());
    }

    hr = drPix->RunExperiments(
        experimentCountToRun,
        experimentRunParameters.data(),
        experimentCallback.Get(),
        nullptr,
        nullptr,
        asyncOperation.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        hr = PrintFailure(hr, L"IPixGpuCaptureDrPix::RunExperiments failed");
        goto Cleanup;
    }

    // Step 10: Wait for the async operation to finish and inspect results.
    wprintf(L"Step 10: Waiting for experiments to complete.\n");
    hr = asyncOperation->GetResult(IID_PPV_ARGS(experimentResults.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        hr = PrintFailure(hr, L"IPixAsyncOperation::GetResult failed");
        goto Cleanup;
    }

    wprintf(
        L"Completed %llu experiment result(s).\n",
        static_cast<unsigned long long>(experimentResults->GetCount()));

    for (UINT64 resultIndex = 0; resultIndex < experimentResults->GetCount(); ++resultIndex)
    {
        ComPtr<IPixGpuCaptureExperimentResult> experimentResult;
        hr = experimentResults->Get(resultIndex, IID_PPV_ARGS(experimentResult.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
        {
            hr = PrintFailure(hr, L"Failed to retrieve an experiment result from the result collection");
            goto Cleanup;
        }

        hr = PrintExperimentResult(experimentResult.Get(), experimentDescriptions);
        if (FAILED(hr))
        {
            hr = PrintFailure(hr, L"Failed to print an experiment result");
            goto Cleanup;
        }
    }

    // Step 11: Stop analysis and disconnect.
    wprintf(L"Step 11: Cleaning up analysis resources.\n");

Cleanup:
    if (analysisStarted)
    {
        HRESULT stopAnalysisHr = analysis->StopAnalysis();
        if (FAILED(stopAnalysisHr) && SUCCEEDED(hr))
        {
            hr = PrintFailure(stopAnalysisHr, L"IPixGpuCaptureAnalysis::StopAnalysis failed");
        }
    }

    if (analysisConnected)
    {
        HRESULT disconnectHr = analysis->Disconnect();
        if (FAILED(disconnectHr) && SUCCEEDED(hr))
        {
            hr = PrintFailure(disconnectHr, L"IPixGpuCaptureAnalysis::Disconnect failed");
        }
    }

    return hr;
}

int wmain(int argumentCount, wchar_t* argumentValues[])
{
    if (argumentCount != 2)
    {
        wprintf(L"Usage: DrPix.exe <path-to-capture.wpix>\n");
        return 1;
    }

    std::filesystem::path capturePath = std::filesystem::absolute(argumentValues[1]);
    if (!std::filesystem::exists(capturePath))
    {
        wprintf(L"Capture file does not exist: %ls\n", capturePath.c_str());
        return 1;
    }

    HRESULT hr = RunSample(capturePath);
    return SUCCEEDED(hr) ? 0 : 1;
}
