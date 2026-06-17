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
// GPU Capture Sample (C#)
//
// Demonstrates the full PIX programmatic GPU capture workflow:
//   1. Create a PIX factory and open a local connection.
//   2. Launch a D3D12 app provided on the command line under GPU capture.
//   3. Take a GPU capture and wait for it to complete.
//   4. Open the resulting .wpix file, parse queues/events.
//   5. Connect analysis and start it (requires Developer Mode).
//

using Microsoft.PIX;
using Microsoft.PIX.Extension;
using Microsoft.PIX.Extension.DeviceConnection;
using Microsoft.PIX.Extension.GpuCapture;
using Microsoft.PIX.Extension.GpuCapture.Analysis;

if (args.Length < 1)
{
    Console.WriteLine("Usage: GpuCapture.exe <path-to-d3d12-exe>");
    return;
}

string targetExecutablePath = args[0];
if (!File.Exists(targetExecutablePath))
{
    Console.WriteLine($"File not found: {targetExecutablePath}");
    return;
}

// --- Step 1: Create the PIX factory (entry point for all operations) ---
var factory = PixApiExtensions.PixCreateFactory<IPixFactory>();

// --- Steps 2-3: Launch process under GPU capture and take a capture ---
string capturePath = TakeGpuCaptureOfSample(targetExecutablePath);

// --- Step 4: Open the capture file and parse events ---
var captureDocument = factory.OpenGpuCaptureDocument<IPixGpuCaptureDocument>(capturePath);
OpenAndParseGpuCapture(captureDocument);

// --- Step 5: Start analysis (requires Developer Mode) ---
StartAnalysis(captureDocument);


// ============================================================
// Helper functions
// ============================================================

void ExitWithError(string error)
{
    Console.WriteLine(error);
    throw new Exception(error);
}

void ExitWithHResult(string message, int hresult)
{
    const int E_PIX_DEVELOPER_MODE_NOT_ENABLED = unchecked((int)0x8abc0000);
    const int E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE = unchecked((int)0x8abc0001);

    if (hresult == E_PIX_DEVELOPER_MODE_NOT_ENABLED || hresult == E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE)
    {
        Console.WriteLine("ERROR: Windows Developer Mode is required for this operation.");
        Console.WriteLine("Enable it in Settings > Privacy & security > For developers,");
        Console.WriteLine("or run: reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock\" /v AllowDevelopmentWithoutDevLicense /t REG_DWORD /d 1 /f");
    }

    Console.WriteLine("{0} (0x{1:X8})", message, hresult);
    throw new Exception(message);
}

/// <summary>
/// Launch a process under GPU capture and return the path to the .wpix file.
/// </summary>
string TakeGpuCaptureOfSample(string targetExecutablePath)
{
    // Connect to the local PIX device.
    var connectionDesc = Microsoft.PIX.Extension.DeviceConnection.PIX_CONNECTION_DESC.CreateLocal();
    var notifications = new PixConnectionNotifications();
    var connectionDocument = factory.OpenConnectionDocument<IPixConnectionDocument>(connectionDesc, notifications);

    // Configure the launch: capture one GPU frame from a Win32 executable.
    var launchDesc = new Microsoft.PIX.Extension.DeviceConnection.PIX_LAUNCH_PROCESS_DESC();
    launchDesc.launchFlags = PIX_APPLICATION_LAUNCH_FLAGS.PIX_APPLICATION_LAUNCH_FLAG_UNDER_GPU_CAPTURE;
    launchDesc.launchMode = PIX_APPLICATION_LAUNCH_MODE.PIX_APPLICATION_LAUNCH_MODE_WIN32_EXECUTABLE;
    launchDesc.launchInfo.win32.exePath = targetExecutablePath;
    launchDesc.launchInfo.win32.commandLineArgs = string.Empty;
    launchDesc.launchInfo.win32.initialWorkingDirectory = string.Empty;

    var launchResults = connectionDocument.LaunchProcess<IPixLaunchProcessResults>(launchDesc);

    // Validate launch result. NOT_USING_D3D12 is normal — the app hasn't
    // called CreateDevice yet at this point.
    switch (launchResults.GetUnsupportedReason())
    {
        case PIX_PROCESS_UNSUPPORTED_REASON.PIX_PROCESS_UNSUPPORTED_REASON_NONE:
        case PIX_PROCESS_UNSUPPORTED_REASON.PIX_PROCESS_UNSUPPORTED_REASON_NOT_USING_D3D12:
            break;
        case PIX_PROCESS_UNSUPPORTED_REASON.PIX_PROCESS_UNSUPPORTED_REASON_WRONG_ARCHITECTURE:
            ExitWithError("Launched process has wrong architecture (needs x64)");
            break;
        case PIX_PROCESS_UNSUPPORTED_REASON.PIX_PROCESS_UNSUPPORTED_REASON_TERMINATED:
            ExitWithError("Launched process terminated before capture could begin");
            break;
    }

    // Request the GPU capture. TakeGpuCapture starts the capture async;
    // GetResult blocks until it completes (target app renders a frame).
    var captureDesc = new PIX_GPU_CAPTURE_DESC() { ProcessId = launchResults.GetProcessId() };
    connectionDocument.TakeGpuCapture(captureDesc, out var asyncOperation);
    var captureResult = asyncOperation.GetResult<IPixGpuCaptureResult>();

    // Detach from the target process (allows it to exit).
    connectionDocument.DetachFromAllProcesses(true);

    string resultPath = captureResult.GetFilename().ToString();
    Console.WriteLine("GpuCapture saved to: {0}", resultPath);
    return resultPath;
}

/// <summary>
/// Walk the capture's command queues looking for the "Hello PixApi!!!" marker from the target app.
/// </summary>
void OpenAndParseGpuCapture(IPixGpuCaptureDocument captureDocument)
{
    var queues = captureDocument.GetQueues();

    for (ulong queueIndex = 0; queueIndex < queues.GetCount(); queueIndex++)
    {
        var queueInfo = queues.Get<IPixGpuCaptureQueueInfo>(queueIndex);

        // Only inspect graphics queues (skip compute/copy).
        if (queueInfo.GetType() != PIX_QUEUE_TYPE.PIX_QUEUE_TYPE_GRAPHICS)
            continue;

        for (uint eventIndex = 0; eventIndex < queueInfo.GetEventCount(); eventIndex++)
        {
            PIX_EVENT_INFO eventInfo = queueInfo.GetEvent(eventIndex);
            string eventName = eventInfo.Name.ToString();

            if (eventName.Contains("Hello PixApi!!!"))
            {
                Console.WriteLine("Found \"Hello PixApi!!!\" @QueueIndex {0}, EventIndex {1}", queueIndex, eventIndex);
                return;
            }
        }
    }
}

/// <summary>
/// Connect analysis to the local GPU and start it.
/// Requires Windows Developer Mode to be enabled.
/// </summary>
void StartAnalysis(IPixGpuCaptureDocument captureDocument)
{
    var analysis = captureDocument.GetAnalysis();
    analysis.Connect(Microsoft.PIX.Extension.DeviceConnection.PIX_CONNECTION_DESC.CreateLocal());

    try
    {
        analysis.StartAnalysis();
    }
    catch (System.Runtime.InteropServices.COMException ex)
    {
        ExitWithHResult("StartAnalysis failed", ex.HResult);
    }
    finally
    {
        try { analysis.StopAnalysis(); } catch { }
        try { analysis.Disconnect(); } catch { }
    }
}
