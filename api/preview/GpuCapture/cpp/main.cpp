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
// GPU Capture Sample (C++)
//
// Demonstrates the full PIX programmatic GPU capture workflow:
//   1. Create a PIX factory and open a local connection.
//   2. Launch a D3D12 app provided on the command line under GPU capture.
//   3. Take a GPU capture and wait for it to complete.
//   4. Open the resulting .wpix file, parse queues/events.
//   5. Connect analysis and start it (requires Developer Mode).
//

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include "d3d12.h"
#include "PixApi.h"

#include <iostream>
#include <string>
#include <filesystem>

// PIX error codes (facility 0xABC).
constexpr HRESULT E_PIX_DEVELOPER_MODE_NOT_ENABLED = 0x8abc0000;
constexpr HRESULT E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE = 0x8abc0001;

void CheckHr(HRESULT hr, std::string message)
{
    if (FAILED(hr))
    {
        if (hr == E_PIX_DEVELOPER_MODE_NOT_ENABLED || hr == E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE)
        {
            std::cout << "ERROR: Windows Developer Mode is required for this operation." << std::endl;
            std::cout << "Enable it in Settings > Privacy & security > For developers," << std::endl;
            std::cout << "or run: reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock\" "
                         "/v AllowDevelopmentWithoutDevLicense /t REG_DWORD /d 1 /f" << std::endl;
        }

        std::cout << message << std::endl;
        std::cout << "Error code: 0x" << std::hex << hr << std::endl;
        // Throw rather than exit() so the caller's RAII cleanup
        // (StopAnalysis -> Disconnect on the analysis path) runs.
        throw hr;
    }
}

// Step 1-3: Launch a process under GPU capture and take a capture.
// Returns the file path to the resulting .wpix capture file.
std::wstring TakeGpuCaptureOfSample(std::wstring const& targetExecutablePath)
{
    // Create the PIX factory — entry point for all PIX API operations.
    ComPtr<IPixFactory> factory;
    CheckHr(PixCreateFactory(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())), "PixCreateFactory failed");

    // Open a connection to the local device (localhost).
    ComPtr<IPixConnectionDocument> connectionDocument;
    PIX_CONNECTION_DESC_LOCAL local = {};
    PIX_CONNECTION_DESC connectionDesc = {};
    connectionDesc.Type = PIX_CONNECTION_TYPE_LOCAL;
    connectionDesc.pLocal = &local;
    CheckHr(factory->OpenConnectionDocument(
        &connectionDesc,
        nullptr,
        IID_PPV_ARGS(connectionDocument.ReleaseAndGetAddressOf())), "OpenConnectionDocument failed");

    // Launch the target app with GPU capture enabled. The process starts
    // suspended with PIX's capture infrastructure injected.
    PIX_LAUNCH_PROCESS_DESC launchDesc = {};
    launchDesc.launchFlags = PIX_APPLICATION_LAUNCH_FLAG_UNDER_GPU_CAPTURE;
    launchDesc.launchMode = PIX_APPLICATION_LAUNCH_MODE_WIN32_EXECUTABLE;
    launchDesc.launchInfo.win32.exePath = targetExecutablePath.c_str();
    launchDesc.launchInfo.win32.commandLineArgs = L"";
    launchDesc.launchInfo.win32.initialWorkingDirectory = L"";
    launchDesc.launchInfo.win32.numEnvVars = 0;

    ComPtr<IPixLaunchProcessResults> launchResults;
    CheckHr(connectionDocument->LaunchProcess(&launchDesc, IID_PPV_ARGS(launchResults.ReleaseAndGetAddressOf())), "LaunchProcess failed");

    // Check launch status. NOT_USING_D3D12 is expected immediately after
    // launch — the process hasn't called CreateDevice yet.
    const auto unsupportedReason = launchResults->GetUnsupportedReason();
    switch (unsupportedReason)
    {
    case PIX_PROCESS_UNSUPPORTED_REASON_NONE:
    case PIX_PROCESS_UNSUPPORTED_REASON_NOT_USING_D3D12:
        break;
    case PIX_PROCESS_UNSUPPORTED_REASON_WRONG_ARCHITECTURE:
        CheckHr(E_UNEXPECTED, "Launched process has wrong architecture (needs x64)");
        break;
    case PIX_PROCESS_UNSUPPORTED_REASON_TERMINATED:
        CheckHr(E_UNEXPECTED, "Launched process terminated before capture could begin");
        break;
    default:
        CheckHr(
            E_UNEXPECTED,
            "Launched process is unsupported (reason " + std::to_string(static_cast<int>(unsupportedReason)) + ")");
        break;
    }

    // Request a GPU capture. TakeGpuCapture is async; GetResult blocks
    // until the capture is complete (the target app renders a frame).
    PIX_GPU_CAPTURE_DESC captureDesc = {};
    captureDesc.ProcessId = launchResults->GetProcessId();

    ComPtr<IPixAsyncOperation> asyncOperation;
    CheckHr(connectionDocument->TakeGpuCapture(&captureDesc, asyncOperation.ReleaseAndGetAddressOf()), "TakeGpuCapture failed");

    ComPtr<IPixGpuCaptureResult> captureResult;
    CheckHr(asyncOperation->GetResult(IID_PPV_ARGS(captureResult.ReleaseAndGetAddressOf())), "GetResult failed (capture may have no GPU work)");

    // Detach from the launched process (allows it to exit).
    connectionDocument->DetachFromAllProcesses(TRUE);

    std::wstring capturePath(captureResult->GetFilename());
    std::wcout << L"GpuCapture saved to: " << capturePath << std::endl;

    return capturePath;
}

// Step 4: Open a capture file and walk its command queues/events.
// Looks for the "Hello PixApi!!!" marker emitted by the target app.
ComPtr<IPixGpuCaptureDocument> OpenAndParseGpuCapture(std::wstring const& capturePath)
{
    ComPtr<IPixFactory> factory;
    CheckHr(PixCreateFactory(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())), "PixCreateFactory failed");

    // Open the .wpix file as a capture document.
    ComPtr<IPixGpuCaptureDocument> captureDocument;
    CheckHr(factory->OpenGpuCaptureDocument(capturePath.c_str(), IID_PPV_ARGS(captureDocument.ReleaseAndGetAddressOf())), "OpenGpuCaptureDocument failed");

    // Enumerate command queues and their events.
    ComPtr<IPixCollection> queues;
    CheckHr(captureDocument->GetQueues(IID_PPV_ARGS(queues.ReleaseAndGetAddressOf())), "GetQueues failed");

    for (UINT queueIndex = 0; queueIndex < queues->GetCount(); queueIndex++)
    {
        ComPtr<IPixGpuCaptureQueueInfo> queueInfo;
        CheckHr(queues->Get(queueIndex, IID_PPV_ARGS(queueInfo.ReleaseAndGetAddressOf())), "Get<IPixGpuCaptureQueueInfo> failed");

        // Only look at graphics queues (skip compute/copy).
        if (queueInfo->GetType() != PIX_QUEUE_TYPE_GRAPHICS)
            continue;

        for (UINT eventIndex = 0; eventIndex < queueInfo->GetEventCount(); eventIndex++)
        {
            PIX_EVENT_INFO eventInfo = {};
            HRESULT hr = queueInfo->GetEvent(eventIndex, &eventInfo);
            if (FAILED(hr))
            {
                std::cout << "QueueIndex " << queueIndex << ", EventIndex " << eventIndex << ": <GetEvent failed>" << std::endl;
                continue;
            }

            std::string eventName(eventInfo.Name);
            std::cout << "QueueIndex " << queueIndex << ", EventIndex " << eventIndex << ": " << eventName << std::endl;

            if (eventName.find("Hello PixApi!!!") != std::string::npos)
            {
                std::cout << "Found \"Hello PixApi!!!\" @QueueIndex " << queueIndex << ", EventIndex " << eventIndex << std::endl;
            }
        }
    }

    return captureDocument;
}

// Step 5: Connect analysis to the local GPU, start it, and demonstrate
// a representative analysis-time operation. Requires Windows Developer
// Mode to be enabled. StopAnalysis + Disconnect are guaranteed to run
// even on failure via an RAII scope guard.
void StartAnalysis(ComPtr<IPixGpuCaptureDocument> const& captureDocument)
{
    ComPtr<IPixGpuCaptureAnalysis> analysis;
    CheckHr(captureDocument->GetAnalysis(IID_PPV_ARGS(analysis.ReleaseAndGetAddressOf())), "GetAnalysis failed");

    PIX_CONNECTION_DESC_LOCAL local = {};
    PIX_CONNECTION_DESC analysisConnectionDesc = {};
    analysisConnectionDesc.Type = PIX_CONNECTION_TYPE_LOCAL;
    analysisConnectionDesc.pLocal = &local;

    CheckHr(analysis->Connect(&analysisConnectionDesc, nullptr), "Analysis Connect failed");
    bool connected = true;

    // Lambda-based scope guard: StopAnalysis -> Disconnect run on every
    // exit path, including exceptions thrown by CheckHr below.
    struct AnalysisScope
    {
        IPixGpuCaptureAnalysis* a;
        bool& started;
        bool& connected;
        ~AnalysisScope()
        {
            if (started) { a->StopAnalysis(); }
            if (connected) { a->Disconnect(); }
        }
    };
    bool started = false;
    AnalysisScope scope{ analysis.Get(), started, connected };

    CheckHr(analysis->StartAnalysis(nullptr, nullptr, nullptr), "StartAnalysis failed (is Developer Mode enabled?)");
    started = true;
    std::cout << "Analysis started successfully." << std::endl;

    // Demonstrate that the analysis session is now live by walking the
    // capture's resources. Any IPixGpuCapture* operation that requires
    // analysis (e.g., GetAccessedResources, replay-driven counter
    // collection, see GpuCounters and DrPix samples) goes between
    // StartAnalysis and the scope-guard cleanup. Here we just print the
    // resource count to keep the GpuCapture sample focused on the
    // capture/analysis lifecycle rather than a specific analysis API.
    ComPtr<IPixD3D12Resources> d3dResources;
    if (SUCCEEDED(captureDocument->GetD3D12Resources(IID_PPV_ARGS(d3dResources.ReleaseAndGetAddressOf()))))
    {
        std::cout << "D3D12 resources in capture: " << d3dResources->GetCount() << std::endl;
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: GpuCapture.exe <path-to-d3d12-exe>" << std::endl;
        return 1;
    }

    std::filesystem::path targetExecutablePath(argv[1]);
    if (!std::filesystem::exists(targetExecutablePath))
    {
        std::cout << "ERROR: File not found: " << targetExecutablePath << std::endl;
        return 1;
    }

    std::wstring capturePath;
    try
    {
        // Run the full workflow: capture -> parse -> analyze.
        capturePath = TakeGpuCaptureOfSample(targetExecutablePath.wstring());

        auto captureDocument = OpenAndParseGpuCapture(capturePath);

        StartAnalysis(captureDocument);

        // Release the capture document before deleting the file.
        captureDocument.Reset();
    }
    catch (HRESULT)
    {
        // CheckHr already printed the error; clean up partial state and
        // surface a non-zero exit. The capture file may exist if the
        // failure was in OpenAndParseGpuCapture or StartAnalysis.
        if (!capturePath.empty() && std::filesystem::exists(capturePath))
        {
            std::filesystem::remove(capturePath);
        }
        return 1;
    }

    // Clean up the temporary capture file.
    std::filesystem::remove(capturePath);

    std::cout << "Sample completed successfully. Capture path: ";
    std::wcout << capturePath << std::endl;
    return 0;
}
