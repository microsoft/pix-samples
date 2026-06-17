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
// GPU Counters Sample (C#)
//
// Demonstrates the PIX GPU performance analysis workflow for an existing GPU capture:
//   1. Create a PIX factory.
//   2. Open a GPU capture document.
//   3. Get the analysis interface.
//   4. Connect to the local GPU.
//   5. Start analysis (requires Windows Developer Mode).
//   6. Get the GPU counters interface.
//   7. Enumerate available counters and counter groups.
//   8. Print counter display name, group, and units/data type.
//   9. Collect GPU occupancy data.
//   10. Collect high-frequency counters.
//   11. Read per-event GPU timing data.
//   12. Stop analysis and disconnect.
//
// Usage: GpuCounters <path-to-gpu-capture-file>
//

using Microsoft.PIX;
using Microsoft.PIX.Extension;
using Microsoft.PIX.Extension.GpuCapture;
using Microsoft.PIX.Extension.GpuCapture.Analysis;
using Microsoft.PIX.Extension.GpuCapture.Analysis.Counters;
using Microsoft.PIX.Extension.GpuCapture.Analysis.Timing;
using System.Runtime.InteropServices;

const ulong PixEventTimingNone = ulong.MaxValue;

if (args.Length < 1)
{
    Console.Error.WriteLine("Usage: GpuCounters <path-to-gpu-capture-file>");
    return 1;
}

string captureFilePath = args[0];
if (!File.Exists(captureFilePath))
{
    Console.Error.WriteLine($"GPU capture file not found: {captureFilePath}");
    return 1;
}

IPixGpuCaptureAnalysis? analysis = null;
bool analysisConnected = false;
bool analysisStarted = false;

try
{
    // Step 1: Create the PIX factory (entry point for all operations).
    var factory = PixApiExtensions.PixCreateFactory<IPixFactory>();

    // Step 2: Open the GPU capture document.
    var captureDocument = factory.OpenGpuCaptureDocument<IPixGpuCaptureDocument>(captureFilePath);

    // Step 3: Get the analysis interface.
    analysis = captureDocument.GetAnalysis();

    // Step 4: Connect analysis to the local GPU.
    analysis.Connect(Microsoft.PIX.Extension.DeviceConnection.PIX_CONNECTION_DESC.CreateLocal());
    analysisConnected = true;

    // Step 5: Start analysis (Windows Developer Mode is required).
    analysis.StartAnalysis();
    analysisStarted = true;

    // Step 6: Get the GPU counters interface.
    var gpuCounters = analysis.GetGpuCounters();

    // Steps 7-8: Enumerate counters and print their details.
    PrintAvailableCounters(gpuCounters);

    // Step 9: Collect GPU occupancy.
    PrintOccupancyData(analysis);

    // Step 10: Collect high-frequency counters.
    PrintHighFrequencyCounterData(analysis);

    // Step 11: Read per-event GPU timing.
    PrintPerEventTimingData(captureDocument, analysis);

    Console.WriteLine();
    Console.WriteLine("Finished collecting GPU counters, occupancy, high-frequency counters, and timing data.");
    return 0;
}
catch (COMException ex)
{
    WriteDeveloperModeHelpIfNeeded(ex.HResult);
    Console.Error.WriteLine($"COM error: 0x{ex.HResult:X8}");
    Console.Error.WriteLine(ex.Message);
    return 1;
}
catch (Exception ex)
{
    Console.Error.WriteLine(ex.Message);
    return 1;
}
finally
{
    // Step 12: Stop analysis and disconnect.
    if (analysis != null)
    {
        if (analysisStarted)
        {
            try
            {
                analysis.StopAnalysis();
            }
            catch (COMException ex)
            {
                Console.Error.WriteLine($"Warning: StopAnalysis failed during cleanup (0x{ex.HResult:X8}).");
            }
        }

        if (analysisConnected)
        {
            try
            {
                analysis.Disconnect();
            }
            catch (COMException ex)
            {
                Console.Error.WriteLine($"Warning: Disconnect failed during cleanup (0x{ex.HResult:X8}).");
            }
        }
    }
}

void PrintAvailableCounters(IPixGpuCaptureCounters gpuCounters)
{
    var counterGroupNamesById = BuildCounterGroupLookup(gpuCounters);
    var counters = gpuCounters.GetCounters();

    Console.WriteLine($"Available GPU counters: {counters.GetCount()}");
    Console.WriteLine();

    for (uint counterIndex = 0; counterIndex < counters.GetCount(); ++counterIndex)
    {
        var counterDescription = counters.Get<IPixGpuCounterDescription>(counterIndex);
        string displayName = counterDescription.GetName().ToString() ?? "(unnamed counter)";
        string groupName = counterGroupNamesById.TryGetValue(counterDescription.GetId(), out var resolvedGroupName)
            ? resolvedGroupName
            : "(group unavailable)";

        // The retail GPU counter description exposes a format/data-type enum rather
        // than a separate units string, so print that value in the Units field.
        string units = counterDescription.GetDataType().ToString();

        Console.WriteLine($"[{counterIndex}] {displayName}");
        Console.WriteLine($"    Group: {groupName}");
        Console.WriteLine($"    Units: {units}");
    }
}

void PrintOccupancyData(IPixGpuCaptureAnalysis analysis)
{
    Console.WriteLine();
    Console.WriteLine("GPU occupancy:");

    try
    {
        var occupancy = GetOccupancy(analysis);
        var occupancyTypes = GetOccupancyTypesCollection(occupancy);
        var occupancyStages = GetOccupancyStagesCollection(occupancy);

        Console.WriteLine($"Available occupancy types: {occupancyTypes.GetCount()}");
        for (ulong typeIndex = 0; typeIndex < occupancyTypes.GetCount(); ++typeIndex)
        {
            var occupancyType = occupancyTypes.Get<IPixGpuCaptureOccupancyType>(typeIndex);
            string typeName = occupancyType.GetName().ToString() ?? "(unnamed occupancy type)";
            Console.WriteLine($"  [{typeIndex}] {typeName} (max slots: {occupancyType.GetMaxSlots()})");
        }

        Console.WriteLine($"Available occupancy stages: {occupancyStages.GetCount()}");
        for (ulong stageIndex = 0; stageIndex < occupancyStages.GetCount(); ++stageIndex)
        {
            var occupancyStage = occupancyStages.Get<IPixGpuCaptureOccupancyStage>(stageIndex);
            string stageName = occupancyStage.GetName().ToString() ?? "(unnamed occupancy stage)";
            string stageAbbreviation = occupancyStage.GetAbbreviation().ToString() ?? "?";
            Console.WriteLine($"  [{stageIndex}] {stageName} ({stageAbbreviation})");
        }

        if (occupancyTypes.GetCount() == 0 || occupancyStages.GetCount() == 0)
        {
            Console.WriteLine("No occupancy types or stages were reported for this capture analysis.");
            return;
        }

        var occupancyData = CollectOccupancyData(occupancy);
        bool printedSummary = false;

        for (ulong typeIndex = 0; typeIndex < occupancyTypes.GetCount() && !printedSummary; ++typeIndex)
        {
            var occupancyType = occupancyTypes.Get<IPixGpuCaptureOccupancyType>(typeIndex);
            string typeName = occupancyType.GetName().ToString() ?? "(unnamed occupancy type)";

            for (ulong stageIndex = 0; stageIndex < occupancyStages.GetCount() && !printedSummary; ++stageIndex)
            {
                var occupancyStage = occupancyStages.Get<IPixGpuCaptureOccupancyStage>(stageIndex);
                string stageName = occupancyStage.GetName().ToString() ?? "(unnamed occupancy stage)";
                ulong pointCount = GetOccupancyPointSummary(
                    occupancyData,
                    occupancyType,
                    occupancyStage,
                    out var firstPoint,
                    out var lastPoint);
                if (pointCount == 0)
                {
                    continue;
                }

                Console.WriteLine($"Collected {pointCount} occupancy points for {typeName} / {stageName}.");
                Console.WriteLine($"  First point: time={firstPoint.TimeNanoseconds} ns slots={firstPoint.Slots}");
                Console.WriteLine($"  Last point:  time={lastPoint.TimeNanoseconds} ns slots={lastPoint.Slots}");
                printedSummary = true;
            }
        }

        if (!printedSummary)
        {
            Console.WriteLine("Occupancy data was collected, but no sample points were returned.");
        }
    }
    catch (Exception ex) when (ex is COMException || ex is NotImplementedException)
    {
        PrintOptionalFeatureUnavailable("GPU occupancy", ex);
    }
}

void PrintHighFrequencyCounterData(IPixGpuCaptureAnalysis analysis)
{
    Console.WriteLine();
    Console.WriteLine("High-frequency counters:");

    try
    {
        var highFrequencyCounters = GetHighFrequencyCounters(analysis);
        var counters = GetHighFrequencyCounterCollection(highFrequencyCounters);
        var counterGroups = GetHighFrequencyCounterGroups(highFrequencyCounters);
        var counterSets = GetHighFrequencyCounterSets(highFrequencyCounters);

        Console.WriteLine($"Available high-frequency counters: {counters.GetCount()}");
        ulong countersToPrint = Math.Min(counters.GetCount(), 8);
        for (ulong counterIndex = 0; counterIndex < countersToPrint; ++counterIndex)
        {
            var counter = counters.Get<IPixGpuCaptureHighFrequencyCounter>(counterIndex);
            string counterName = counter.GetName().ToString() ?? "(unnamed high-frequency counter)";
            Console.WriteLine($"  [{counterIndex}] {counterName}");
        }

        Console.WriteLine($"Available high-frequency counter groups: {counterGroups.GetCount()}");
        for (ulong groupIndex = 0; groupIndex < counterGroups.GetCount(); ++groupIndex)
        {
            var counterGroup = counterGroups.Get<IPixGpuCaptureCounterCollection>(groupIndex);
            string groupName = counterGroup.GetName().ToString() ?? "(unnamed high-frequency counter group)";
            Console.WriteLine($"  [{groupIndex}] {groupName} ({counterGroup.GetCount()} counters)");
        }

        Console.WriteLine($"Available high-frequency counter sets: {counterSets.GetCount()}");
        for (ulong setIndex = 0; setIndex < counterSets.GetCount(); ++setIndex)
        {
            var counterSet = counterSets.Get<IPixGpuCaptureCounterCollection>(setIndex);
            string setName = counterSet.GetName().ToString() ?? "(unnamed high-frequency counter set)";
            Console.WriteLine($"  [{setIndex}] {setName} ({counterSet.GetCount()} counters)");
        }

        if (counterSets.GetCount() == 0)
        {
            Console.WriteLine("No high-frequency counter sets were reported for this capture analysis.");
            return;
        }

        var counterData = CollectHighFrequencyCounterData(highFrequencyCounters, counterSets);
        var firstCounterSet = counterSets.Get<IPixGpuCaptureCounterCollection>(0);
        if (firstCounterSet.GetCount() == 0)
        {
            Console.WriteLine("The first high-frequency counter set is empty.");
            return;
        }

        var firstCounter = firstCounterSet.Get<IPixGpuCaptureHighFrequencyCounter>(0);
        string setNameText = firstCounterSet.GetName().ToString() ?? "(unnamed high-frequency counter set)";
        string counterNameText = firstCounter.GetName().ToString() ?? "(unnamed high-frequency counter)";
        GetHighFrequencySampleSummary(
            counterData,
            firstCounterSet,
            firstCounter,
            out ulong batchId,
            out ulong sampleCount,
            out ulong firstSampleTimeStamp,
            out double firstSampleValue);

        Console.WriteLine($"Collected {sampleCount} high-frequency samples for set '{setNameText}' and counter '{counterNameText}' (batch {batchId}).");
        if (sampleCount == 0)
        {
            Console.WriteLine("No high-frequency samples were returned for the first counter set.");
            return;
        }

        Console.WriteLine($"  First sample: {firstSampleTimeStamp} ns -> {firstSampleValue}");
    }
    catch (Exception ex) when (ex is COMException || ex is NotImplementedException)
    {
        PrintOptionalFeatureUnavailable("High-frequency counters", ex);
    }
}

void PrintPerEventTimingData(IPixGpuCaptureDocument captureDocument, IPixGpuCaptureAnalysis analysis)
{
    Console.WriteLine();
    Console.WriteLine("Per-event GPU timing:");

    try
    {
        var timingData = analysis.CollectTiming<IPixGpuCaptureTiming>();
        var preferredQueue = FindPreferredQueue(captureDocument);
        if (preferredQueue == null)
        {
            Console.WriteLine("No GPU queue was found in the capture, so timing values were not collected.");
            return;
        }

        string queueName = preferredQueue.GetName().ToString() ?? "(unnamed queue)";
        ulong queueTimingCount = timingData.GetQueueDataCount(preferredQueue);
        Console.WriteLine($"Queue '{queueName}' has {queueTimingCount} timing records.");

        uint printedTimingEvents = 0;
        for (uint eventIndex = 0; eventIndex < preferredQueue.GetEventCount() && printedTimingEvents < 5; ++eventIndex)
        {
            PIX_EVENT_INFO eventInfo = preferredQueue.GetEvent(eventIndex);
            if (!timingData.HasEventData(eventInfo))
            {
                continue;
            }

            PIX_EVENT_TIMING eventTiming = timingData.GetEventData(eventInfo);
            ulong topEnd = eventTiming.TopStart == PixEventTimingNone || eventTiming.TopDuration == PixEventTimingNone
                ? PixEventTimingNone
                : eventTiming.TopStart + eventTiming.TopDuration;
            ulong eopEnd = eventTiming.EopStart == PixEventTimingNone || eventTiming.EopDuration == PixEventTimingNone
                ? PixEventTimingNone
                : eventTiming.EopStart + eventTiming.EopDuration;
            string eventName = eventInfo.Name.ToString() ?? "(unnamed event)";

            Console.WriteLine($"  [{eventIndex}] {eventName}");
            Console.WriteLine($"      Top: start={FormatTimingValue(eventTiming.TopStart)} ns duration={FormatTimingValue(eventTiming.TopDuration)} ns end={FormatTimingValue(topEnd)} ns");
            Console.WriteLine($"      EOP: start={FormatTimingValue(eventTiming.EopStart)} ns duration={FormatTimingValue(eventTiming.EopDuration)} ns end={FormatTimingValue(eopEnd)} ns");
            ++printedTimingEvents;
        }

        if (printedTimingEvents == 0)
        {
            Console.WriteLine("No per-event GPU timing data was reported for the selected queue.");
        }
    }
    catch (Exception ex) when (ex is COMException || ex is NotImplementedException)
    {
        PrintOptionalFeatureUnavailable("Per-event GPU timing", ex);
    }
}

Dictionary<uint, string> BuildCounterGroupLookup(IPixGpuCaptureCounters gpuCounters)
{
    var counterGroupNamesById = new Dictionary<uint, List<string>>();
    var counterGroups = gpuCounters.GetCounterGroups();

    Console.WriteLine($"Available GPU counter groups: {counterGroups.GetCount()}");

    for (uint groupIndex = 0; groupIndex < counterGroups.GetCount(); ++groupIndex)
    {
        var counterGroupDescription = counterGroups.Get<IPixGpuCounterGroupDescription>(groupIndex);
        string groupName = counterGroupDescription.GetName().ToString() ?? "(unnamed group)";
        var groupCounters = counterGroupDescription.GetCounters();

        Console.WriteLine($"  - {groupName} ({groupCounters.GetCount()} counters)");

        for (uint counterIndex = 0; counterIndex < groupCounters.GetCount(); ++counterIndex)
        {
            var counterDescription = groupCounters.Get<IPixGpuCounterDescription>(counterIndex);
            if (!counterGroupNamesById.TryGetValue(counterDescription.GetId(), out var groupNames))
            {
                groupNames = new List<string>();
                counterGroupNamesById[counterDescription.GetId()] = groupNames;
            }

            if (!groupNames.Any(existingGroupName => string.Equals(existingGroupName, groupName, StringComparison.OrdinalIgnoreCase)))
            {
                groupNames.Add(groupName);
            }
        }
    }

    return counterGroupNamesById.ToDictionary(
        pair => pair.Key,
        pair => string.Join(", ", pair.Value));
}

IPixGpuCaptureQueueInfo? FindPreferredQueue(IPixGpuCaptureDocument captureDocument)
{
    IPixGpuCaptureQueueInfo? preferredQueue = null;
    var queues = captureDocument.GetQueues();

    for (ulong queueIndex = 0; queueIndex < queues.GetCount(); ++queueIndex)
    {
        var queueInfo = queues.Get<IPixGpuCaptureQueueInfo>(queueIndex);
        if (queueInfo.GetType() == PIX_QUEUE_TYPE.PIX_QUEUE_TYPE_GRAPHICS)
        {
            return queueInfo;
        }

        if (preferredQueue == null && queueInfo.GetType() != PIX_QUEUE_TYPE.PIX_QUEUE_TYPE_CPU)
        {
            preferredQueue = queueInfo;
        }
    }

    return preferredQueue;
}

IPixGpuCaptureOccupancy GetOccupancy(IPixGpuCaptureAnalysis analysis)
{
    analysis.GetOccupancy(typeof(IPixGpuCaptureOccupancy).GUID, out var obj);
    return (IPixGpuCaptureOccupancy)obj;
}

IPixCollection GetOccupancyTypesCollection(IPixGpuCaptureOccupancy occupancy)
{
    occupancy.GetOccupancyTypes(typeof(IPixCollection).GUID, out var obj);
    return (IPixCollection)obj;
}

IPixCollection GetOccupancyStagesCollection(IPixGpuCaptureOccupancy occupancy)
{
    occupancy.GetOccupancyStages(typeof(IPixCollection).GUID, out var obj);
    return (IPixCollection)obj;
}

IPixGpuCaptureOccupancyData CollectOccupancyData(IPixGpuCaptureOccupancy occupancy)
{
    occupancy.CollectOccupancy(typeof(IPixGpuCaptureOccupancyData).GUID, out var obj);
    return (IPixGpuCaptureOccupancyData)obj;
}

IPixGpuCaptureHighFrequencyCounters GetHighFrequencyCounters(IPixGpuCaptureAnalysis analysis)
{
    analysis.GetHighFrequencyCounters(typeof(IPixGpuCaptureHighFrequencyCounters).GUID, out var obj);
    return (IPixGpuCaptureHighFrequencyCounters)obj;
}

IPixCollection GetHighFrequencyCounterCollection(IPixGpuCaptureHighFrequencyCounters highFrequencyCounters)
{
    highFrequencyCounters.GetCounters(typeof(IPixCollection).GUID, out var obj);
    return (IPixCollection)obj;
}

IPixCollection GetHighFrequencyCounterGroups(IPixGpuCaptureHighFrequencyCounters highFrequencyCounters)
{
    highFrequencyCounters.GetCounterGroups(typeof(IPixCollection).GUID, out var obj);
    return (IPixCollection)obj;
}

IPixCollection GetHighFrequencyCounterSets(IPixGpuCaptureHighFrequencyCounters highFrequencyCounters)
{
    highFrequencyCounters.GetGpuCounterSets(typeof(IPixCollection).GUID, out var obj);
    return (IPixCollection)obj;
}

IPixGpuCaptureHighFrequencyCounterData CollectHighFrequencyCounterData(
    IPixGpuCaptureHighFrequencyCounters highFrequencyCounters,
    IPixCollection counterSets)
{
    highFrequencyCounters.CollectCounterData(counterSets, typeof(IPixGpuCaptureHighFrequencyCounterData).GUID, out var obj);
    return (IPixGpuCaptureHighFrequencyCounterData)obj;
}

ulong GetOccupancyPointSummary(
    IPixGpuCaptureOccupancyData occupancyData,
    IPixGpuCaptureOccupancyType occupancyType,
    IPixGpuCaptureOccupancyStage occupancyStage,
    out PIX_OCCUPANCY_POINT firstPoint,
    out PIX_OCCUPANCY_POINT lastPoint)
{
    firstPoint = default;
    lastPoint = default;

    unsafe
    {
        ulong pointCount = 0;
        PIX_OCCUPANCY_POINT* occupancyPoints = null;
        occupancyData.GetPoints(occupancyType, occupancyStage, ref pointCount, ref occupancyPoints);
        if (pointCount == 0 || occupancyPoints == null)
        {
            return 0;
        }

        firstPoint = occupancyPoints[0];
        lastPoint = occupancyPoints[pointCount - 1];
        return pointCount;
    }
}

void GetHighFrequencySampleSummary(
    IPixGpuCaptureHighFrequencyCounterData counterData,
    IPixGpuCaptureCounterCollection counterSet,
    IPixGpuCaptureHighFrequencyCounter counter,
    out ulong batchId,
    out ulong sampleCount,
    out ulong firstSampleTimeStamp,
    out double firstSampleValue)
{
    batchId = 0;
    sampleCount = 0;
    firstSampleTimeStamp = 0;
    firstSampleValue = 0.0;

    unsafe
    {
        ulong* sampleTimeStamps = null;
        double* sampleValues = null;
        counterData.GetSamples(counterSet, counter, ref batchId, ref sampleCount, ref sampleTimeStamps, ref sampleValues);
        if (sampleCount == 0 || sampleTimeStamps == null || sampleValues == null)
        {
            return;
        }

        firstSampleTimeStamp = sampleTimeStamps[0];
        firstSampleValue = sampleValues[0];
    }
}

string FormatTimingValue(ulong value)
{
    return value == PixEventTimingNone ? "(none)" : value.ToString();
}

void PrintOptionalFeatureUnavailable(string featureName, Exception ex)
{
    if (ex is COMException comException)
    {
        Console.WriteLine($"{featureName} is not supported or unavailable for this capture on the current hardware (0x{comException.HResult:X8}).");
    }
    else if (ex is NotImplementedException)
    {
        Console.WriteLine($"{featureName} is not implemented by the current PIX runtime for this capture.");
    }
    else
    {
        Console.WriteLine($"{featureName} is not available: {ex.Message}");
    }
}

void WriteDeveloperModeHelpIfNeeded(int hresult)
{
    const int E_PIX_DEVELOPER_MODE_NOT_ENABLED = unchecked((int)0x8abc0000);
    const int E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE = unchecked((int)0x8abc0001);

    if (hresult == E_PIX_DEVELOPER_MODE_NOT_ENABLED || hresult == E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE)
    {
        Console.Error.WriteLine("ERROR: Windows Developer Mode is required for this operation.");
        Console.Error.WriteLine("Enable it in Settings > Privacy & security > For developers,");
        Console.Error.WriteLine("or run: reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock\" /v AllowDevelopmentWithoutDevLicense /t REG_DWORD /d 1 /f");
    }
}
