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
// DrPix Sample (C#)
//
// Demonstrates the PIX Dr. PIX workflow using an existing GPU capture:
//   1. Create a PIX factory and open the GPU capture provided on the command line.
//   2. Connect analysis and start it (requires Windows Developer Mode).
//   3. Get the Dr. PIX interface.
//   4. Enumerate the available experiments.
//   5. Register a callback that prints when experiment results arrive.
//   6. Run all experiments across the capture's valid GPU event range.
//   7. Wait for completion and print a final summary.
//

using Microsoft.PIX;
using Microsoft.PIX.Extension;
using Microsoft.PIX.Extension.DeviceConnection;
using Microsoft.PIX.Extension.DrPix;
using Microsoft.PIX.Extension.GpuCapture;
using Microsoft.PIX.Extension.GpuCapture.Analysis;
using System.Runtime.InteropServices;

if (args.Length < 1)
{
    Console.WriteLine("Usage: DrPix <path-to-capture.wpix>");
    return 1;
}

string captureFilePath = Path.GetFullPath(args[0]);
if (!File.Exists(captureFilePath))
{
    Console.WriteLine("ERROR: Capture file not found: {0}", captureFilePath);
    return 1;
}

IPixGpuCaptureDocument? gpuCaptureDocument = null;
IPixGpuCaptureAnalysis? analysis = null;
IPixGpuCaptureDrPix? drPix = null;
IPixAsyncOperation? asyncOperation = null;
IPixCollection? experimentResults = null;
bool analysisConnected = false;
bool analysisStarted = false;

try
{
    // Step 1: Create the PIX factory and open the GPU capture document.
    IPixFactory pixFactory = PixApiExtensions.PixCreateFactory<IPixFactory>();
    gpuCaptureDocument = pixFactory.OpenGpuCaptureDocument<IPixGpuCaptureDocument>(captureFilePath);

    // Step 2: Connect analysis and start it.
    analysis = gpuCaptureDocument.GetAnalysis();
    analysis.Connect(Microsoft.PIX.Extension.DeviceConnection.PIX_CONNECTION_DESC.CreateLocal());
    analysisConnected = true;
    StartAnalysis(analysis);
    analysisStarted = true;

    // Step 3: Get the Dr. PIX interface.
    drPix = analysis.GetDrPix();

    // Step 4: Enumerate the available experiments.
    var experiments = EnumerateExperiments(drPix);
    if (experiments.Count == 0)
    {
        Console.WriteLine("No Dr. PIX experiments are available for this capture.");
        return 0;
    }

    var experimentsByGuid = experiments.ToDictionary(experiment => experiment.Guid);
    var (firstEvent, lastEvent) = FindExperimentEventRange(gpuCaptureDocument);

    Console.WriteLine(
        "Using GPU event range {0} -> {1} for experiment execution.",
        firstEvent.GpuId,
        lastEvent.GpuId);

    // Step 5: Register a callback that prints when experiment results arrive.
    var experimentCallback = new ExperimentCallback(experimentsByGuid);
    var progressNotifications = new ProgressNotificationsHelper(
        statusMessage =>
        {
            Console.WriteLine("Status: {0}", statusMessage);
        },
        progressValue =>
        {
            Console.WriteLine("Progress: {0:P0}", progressValue);
        });

    IPixCancellationToken cancellationToken = pixFactory.CreateCancellationToken();
    PIX_EXPERIMENT_RUN_PARAMS[] runParameters = BuildRunParameters(
        experiments,
        firstEvent,
        lastEvent);

    // Step 6: Run the experiments.
    Console.WriteLine("Running {0} Dr. PIX experiment(s)...", runParameters.Length);
    asyncOperation = RunExperimentsAsync(
        drPix,
        runParameters,
        experimentCallback,
        progressNotifications,
        cancellationToken);

    // Step 7: Wait for completion and print a final summary.
    Console.WriteLine("Waiting for experiment completion...");
    experimentResults = asyncOperation.GetResult<IPixCollection>();

    PrintSummary(
        experimentResults,
        experimentsByGuid,
        experimentCallback.ResultCount);

    Console.WriteLine("DrPix sample completed successfully.");
    return 0;
}
catch (COMException comException)
{
    PrintComError(comException);
    return 1;
}
catch (Exception exception)
{
    Console.WriteLine("ERROR: {0}", exception.Message);
    return 1;
}
finally
{
    // Mirror the DrPix C++ variant: explicitly stop and disconnect the
    // analysis session before releasing COM interfaces. Releasing alone
    // does not tear down the device-side session, which can surface as
    // "analysis already running" on back-to-back runs of this sample.
    if (analysis != null)
    {
        if (analysisStarted)
        {
            try { analysis.StopAnalysis(); }
            catch (COMException ex) { Console.Error.WriteLine($"Warning: StopAnalysis failed during cleanup (0x{ex.HResult:X8})."); }
        }
        if (analysisConnected)
        {
            try { analysis.Disconnect(); }
            catch (COMException ex) { Console.Error.WriteLine($"Warning: Disconnect failed during cleanup (0x{ex.HResult:X8})."); }
        }
    }

    ReleaseComObject(experimentResults);
    ReleaseComObject(asyncOperation);
    ReleaseComObject(drPix);
    ReleaseComObject(analysis);
    ReleaseComObject(gpuCaptureDocument);

    experimentResults = null;
    asyncOperation = null;
    drPix = null;
    analysis = null;
    gpuCaptureDocument = null;

    GC.Collect();
    GC.WaitForPendingFinalizers();
    GC.Collect();
}

static void StartAnalysis(IPixGpuCaptureAnalysis analysis)
{
    try
    {
        // Developer Mode is required for GPU analysis and Dr. PIX experiments.
        analysis.StartAnalysis();
    }
    catch (COMException comException)
    {
        const int DeveloperModeNotEnabled = unchecked((int)0x8abc0000);
        const int FeatureRequiresDeveloperMode = unchecked((int)0x8abc0001);

        if (comException.HResult == DeveloperModeNotEnabled || comException.HResult == FeatureRequiresDeveloperMode)
        {
            Console.WriteLine("ERROR: Windows Developer Mode is required for this operation.");
            Console.WriteLine("Enable it in Settings > Privacy & security > For developers,");
            Console.WriteLine("or run: reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock\" /v AllowDevelopmentWithoutDevLicense /t REG_DWORD /d 1 /f");
        }

        throw;
    }
}

static List<ExperimentInfo> EnumerateExperiments(IPixGpuCaptureDrPix drPix)
{
    ulong experimentCount = drPix.GetExperimentCount();
    Console.WriteLine("Found {0} Dr. PIX experiment(s):", experimentCount);

    var experiments = new List<ExperimentInfo>((int)experimentCount);

    for (ulong experimentIndex = 0; experimentIndex < experimentCount; experimentIndex++)
    {
        PIX_EXPERIMENT_DESC experimentDescription = GetExperimentDescription(
            drPix,
            experimentIndex);

        var experimentInfo = new ExperimentInfo(
            experimentDescription.Guid,
            experimentDescription.Name.ToString(),
            experimentDescription.Category.ToString(),
            experimentDescription.HelpText.ToString(),
            experimentDescription.Source);

        experiments.Add(experimentInfo);

        Console.WriteLine(
            "  [{0}] {1} ({2})",
            experimentIndex,
            experimentInfo.Name,
            experimentInfo.Guid);
        Console.WriteLine("       Category: {0}", experimentInfo.Category);
        Console.WriteLine("       Source: {0}", experimentInfo.Source);
        Console.WriteLine("       Description: {0}", experimentInfo.HelpText);
    }

    return experiments;
}

static unsafe PIX_EXPERIMENT_DESC GetExperimentDescription(
    IPixGpuCaptureDrPix drPix,
    ulong experimentIndex)
{
    PIX_EXPERIMENT_DESC experimentDescription = default;
    drPix.GetExperiment(experimentIndex, &experimentDescription);
    return experimentDescription;
}

static (PIX_EVENT_INFO FirstEvent, PIX_EVENT_INFO LastEvent) FindExperimentEventRange(IPixGpuCaptureDocument gpuCaptureDocument)
{
    IPixCollection queues = gpuCaptureDocument.GetQueues();

    const uint InvalidGpuId = uint.MaxValue;
    PIX_EVENT_INFO firstEvent = default;
    PIX_EVENT_INFO lastEvent = default;
    firstEvent.GpuId = InvalidGpuId;
    bool foundValidEvent = false;

    for (ulong queueIndex = 0; queueIndex < queues.GetCount(); queueIndex++)
    {
        IPixGpuCaptureQueueInfo queueInfo = queues.Get<IPixGpuCaptureQueueInfo>(queueIndex);

        for (uint eventIndex = 0; eventIndex < queueInfo.GetEventCount(); eventIndex++)
        {
            PIX_EVENT_INFO eventInfo = queueInfo.GetEvent(eventIndex);
            if (eventInfo.GpuId == InvalidGpuId)
            {
                continue;
            }

            if (!foundValidEvent || eventInfo.GpuId < firstEvent.GpuId)
            {
                firstEvent = eventInfo;
            }

            if (!foundValidEvent || eventInfo.GpuId > lastEvent.GpuId)
            {
                lastEvent = eventInfo;
            }

            foundValidEvent = true;
        }
    }

    if (!foundValidEvent)
    {
        throw new InvalidOperationException(
            "The capture does not contain any valid GPU events for Dr. PIX experiments.");
    }

    return (firstEvent, lastEvent);
}

static PIX_EXPERIMENT_RUN_PARAMS[] BuildRunParameters(
    IReadOnlyList<ExperimentInfo> experiments,
    PIX_EVENT_INFO firstEvent,
    PIX_EVENT_INFO lastEvent)
{
    PIX_EXPERIMENT_RUN_PARAMS[] runParameters = new PIX_EXPERIMENT_RUN_PARAMS[experiments.Count];

    for (int experimentIndex = 0; experimentIndex < experiments.Count; experimentIndex++)
    {
        runParameters[experimentIndex].ExperimentGuid = experiments[experimentIndex].Guid;
        runParameters[experimentIndex].FirstEvent = firstEvent;
        runParameters[experimentIndex].LastEvent = lastEvent;
    }

    return runParameters;
}

static unsafe IPixAsyncOperation RunExperimentsAsync(
    IPixGpuCaptureDrPix drPix,
    PIX_EXPERIMENT_RUN_PARAMS[] runParameters,
    IPixGpuCaptureExperimentCallback experimentCallback,
    IPixProgressNotifications progressNotifications,
    IPixCancellationToken cancellationToken)
{
    fixed (PIX_EXPERIMENT_RUN_PARAMS* runParametersPointer = runParameters)
    {
        drPix.RunExperiments(
            (ulong)runParameters.Length,
            runParametersPointer,
            experimentCallback,
            progressNotifications,
            cancellationToken,
            out var asyncOperation);
        return asyncOperation;
    }
}

static void PrintSummary(
    IPixCollection experimentResults,
    IReadOnlyDictionary<Guid, ExperimentInfo> experimentsByGuid,
    int callbackCount)
{
    Console.WriteLine();
    Console.WriteLine("Experiment summary:");
    Console.WriteLine("  Results returned: {0}", experimentResults.GetCount());
    Console.WriteLine("  Callback count: {0}", callbackCount);

    for (ulong resultIndex = 0; resultIndex < experimentResults.GetCount(); resultIndex++)
    {
        IPixGpuCaptureExperimentResult experimentResult = experimentResults.Get<IPixGpuCaptureExperimentResult>(resultIndex);
        PIX_EXPERIMENT_RUN_PARAMS runParameters = experimentResult.GetExperimentRunParams();
        ExperimentInfo experimentInfo = experimentsByGuid[runParameters.ExperimentGuid];

        Console.WriteLine(
            "  - {0}: status=0x{1:X8}, metrics={2}, messages={3}",
            experimentInfo.Name,
            experimentResult.GetExperimentStatus(),
            experimentResult.GetExperimentMetricCount(),
            experimentResult.GetExperimentMessageCount());
    }
}

static void PrintComError(COMException comException)
{
    Console.WriteLine("PIX API call failed: 0x{0:X8}", comException.HResult);
    Console.WriteLine(comException.Message);
}

static void ReleaseComObject(object? comObject)
{
    if (comObject == null || !Marshal.IsComObject(comObject))
    {
        return;
    }

    try
    {
        Marshal.FinalReleaseComObject(comObject);
    }
    catch
    {
    }
}

sealed record ExperimentInfo(
    Guid Guid,
    string Name,
    string Category,
    string HelpText,
    PIX_EXPERIMENT_SOURCE Source);

sealed class ExperimentCallback : IPixGpuCaptureExperimentCallback
{
    private readonly IReadOnlyDictionary<Guid, ExperimentInfo> experimentsByGuid;
    private readonly object lockObject = new();

    public ExperimentCallback(IReadOnlyDictionary<Guid, ExperimentInfo> experimentsByGuid)
    {
        this.experimentsByGuid = experimentsByGuid;
    }

    public int ResultCount { get; private set; }

    public void OnExperimentResultAvailable(object result)
    {
        var experimentResult = (IPixGpuCaptureExperimentResult)result;
        PIX_EXPERIMENT_RUN_PARAMS runParameters = experimentResult.GetExperimentRunParams();

        string experimentName = experimentsByGuid.TryGetValue(
            runParameters.ExperimentGuid,
            out ExperimentInfo? experimentInfo)
            ? experimentInfo.Name
            : runParameters.ExperimentGuid.ToString();

        int currentResultCount;
        lock (lockObject)
        {
            ResultCount++;
            currentResultCount = ResultCount;
        }

        Console.WriteLine(
            "[Callback {0}] {1}: status=0x{2:X8}, metrics={3}, messages={4}",
            currentResultCount,
            experimentName,
            experimentResult.GetExperimentStatus(),
            experimentResult.GetExperimentMetricCount(),
            experimentResult.GetExperimentMessageCount());
    }
}
