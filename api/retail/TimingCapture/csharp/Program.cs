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
// Timing Capture Sample (C#)
//
// Demonstrates the PIX timing capture workflow:
//   1. Create a PIX factory.
//   2. Open a timing capture document from a file path.
//   3. Print the capture path and PIX storage path.
//   4. Optionally resolve symbols when a PDB path is provided.
//   5. Enumerate visible system monitor counters.
//   6. Close the document.
//   7. Print success.
//

using Microsoft.PIX;
using Microsoft.PIX.Extension;
using Microsoft.PIX.Extension.DeviceConnection;
using Microsoft.PIX.Extension.TimingCapture;
using System.Collections.Generic;
using System.Runtime.InteropServices;

if (args.Length < 1 || args.Length > 2)
{
    Console.WriteLine("Usage: TimingCapture <capture-path> [full-pdb-path]");
    return 1;
}

string captureFilePath = Path.GetFullPath(args[0]);
string? fullPdbPath = args.Length > 1 ? Path.GetFullPath(args[1]) : null;

if (!File.Exists(captureFilePath))
{
    Console.WriteLine("Capture file not found: {0}", captureFilePath);
    return 1;
}

if (fullPdbPath != null && !File.Exists(fullPdbPath))
{
    Console.WriteLine("PDB file not found: {0}", fullPdbPath);
    return 1;
}

IPixTimingCaptureDocument? timingCaptureDocument = null;

try
{
    // Step 1: Create the PIX factory (entry point for all operations).
    IPixFactory pixFactory = PixApiExtensions.PixCreateFactory<IPixFactory>();

    // Step 2: Open the timing capture document from disk.
    timingCaptureDocument = pixFactory.OpenTimingCaptureDocument<IPixTimingCaptureDocument>(captureFilePath);

    // Step 3: Print basic information about the opened capture.
    Console.WriteLine("CapturePath: {0}", timingCaptureDocument.GetCapturePath().ToString());
    Console.WriteLine("PixStoragePath: {0}", timingCaptureDocument.GetPixStoragePath().ToString());

    // Step 4: Optionally resolve symbols using the provided PDB path.
    if (fullPdbPath != null)
    {
        Console.WriteLine("Resolving symbols using: {0}", fullPdbPath);

        TimingCaptureSymbolSettings symbolSettings = new()
        {
            IncludeKernelSymbols = false,
            IncludeSourceData = true,
            IncludeTypeData = false,
            UseNTSymbolPath = true
        };

        timingCaptureDocument.ResolveSymbols(
            fullPdbPath,
            symbolSettings,
            statusMessage =>
            {
                Console.WriteLine("Status: {0}", statusMessage);
            },
            progressValue =>
            {
                Console.WriteLine("Progress: {0:P0}", progressValue);
            });
    }
    else
    {
        Console.WriteLine("Skipping symbol resolution.");
    }

    // Step 5: Enumerate available system monitor counters.
    TryPrintSystemMonitorCounters(pixFactory);

    // Step 6: Close the document.
    timingCaptureDocument.Close();
    timingCaptureDocument = null;

    // Step 7: Print success.
    Console.WriteLine("Timing capture sample completed successfully.");
    return 0;
}
catch (COMException comException)
{
    Console.WriteLine("PIX API call failed: 0x{0:X8}", comException.HResult);
    Console.WriteLine(comException.Message);
    return 1;
}
catch (Exception exception)
{
    Console.WriteLine("ERROR: {0}", exception.Message);
    return 1;
}
finally
{
    if (timingCaptureDocument != null)
    {
        try
        {
            timingCaptureDocument.Close();
        }
        catch (Exception closeException)
        {
            Console.WriteLine(
                "WARNING: Failed to close timing capture document: {0}",
                closeException.Message);
        }
    }
}

static string GetStringOrDefault(object? value, string defaultValue = "(none)")
{
    string? resolvedValue = value?.ToString();
    return string.IsNullOrWhiteSpace(resolvedValue) ? defaultValue : resolvedValue;
}

static Dictionary<uint, string> BuildSystemMonitorCounterGroupLookup(
    IPixGetCounterDescriptionsResults counterDescriptions)
{
    var counterGroupNamesById = new Dictionary<uint, string>();

    for (ulong counterGroupIndex = 0; counterGroupIndex < counterDescriptions.GetNumCounterGroups(); ++counterGroupIndex)
    {
        var counterGroup = counterDescriptions.GetCounterGroupDescription<IPixSystemMonitorCounterGroup>(counterGroupIndex);
        counterGroupNamesById[counterGroup.GetCounterGroupId()] = GetStringOrDefault(
            counterGroup.GetName(),
            "(unnamed group)");
    }

    return counterGroupNamesById;
}

static void TryPrintSystemMonitorCounters(IPixFactory pixFactory)
{
    try
    {
        var connectionDescription = Microsoft.PIX.Extension.DeviceConnection.PIX_CONNECTION_DESC.CreateLocal();
        var connectionNotifications = new PixConnectionNotifications();
        var connectionDocument = pixFactory.OpenConnectionDocument<IPixConnectionDocument>(
            connectionDescription,
            connectionNotifications);

        var counterDescriptions = connectionDocument.GetCounterDescriptions<IPixGetCounterDescriptionsResults>();
        var counterGroupNamesById = BuildSystemMonitorCounterGroupLookup(counterDescriptions);
        var visibleCountersByGroupId = new SortedDictionary<uint, List<(string DisplayName, string Units, float DefinedMin, float DefinedMax)>>();
        ulong visibleCounterCount = 0;
        ulong totalCounterCount = counterDescriptions.GetNumCounters();

        for (ulong counterIndex = 0; counterIndex < totalCounterCount; ++counterIndex)
        {
            var counterDescription = counterDescriptions.GetCounterDescription<IPixSystemMonitorCounter>(counterIndex);
            if (!counterDescription.GetIsVisible() || counterDescription.GetIsInternal())
            {
                continue;
            }

            uint counterGroupId = counterDescription.GetCounterGroupId();
            if (!visibleCountersByGroupId.TryGetValue(counterGroupId, out var countersForGroup))
            {
                countersForGroup = new List<(string DisplayName, string Units, float DefinedMin, float DefinedMax)>();
                visibleCountersByGroupId[counterGroupId] = countersForGroup;
            }

            countersForGroup.Add((
                GetStringOrDefault(counterDescription.GetDisplayName(), "(unnamed counter)"),
                GetStringOrDefault(counterDescription.GetUnits()),
                counterDescription.GetDefinedMin(),
                counterDescription.GetDefinedMax()));
            ++visibleCounterCount;
        }

        Console.WriteLine(
            "System monitor counters: {0} visible of {1} total.",
            visibleCounterCount,
            totalCounterCount);

        if (visibleCountersByGroupId.Count == 0)
        {
            Console.WriteLine("No visible non-internal system monitor counters were found.");
            return;
        }

        foreach (var counterGroupPair in visibleCountersByGroupId)
        {
            string counterGroupName = counterGroupNamesById.TryGetValue(counterGroupPair.Key, out var resolvedGroupName)
                ? resolvedGroupName
                : "(group unavailable)";

            Console.WriteLine(
                "  Group {0}: {1} ({2} counters)",
                counterGroupPair.Key,
                counterGroupName,
                counterGroupPair.Value.Count);

            foreach (var counterDescription in counterGroupPair.Value)
            {
                Console.WriteLine("    {0}", counterDescription.DisplayName);
                Console.WriteLine("      GroupId: {0}", counterGroupPair.Key);
                Console.WriteLine("      Units: {0}", counterDescription.Units);
                Console.WriteLine(
                    "      Range: {0:F3} to {1:F3}",
                    counterDescription.DefinedMin,
                    counterDescription.DefinedMax);
            }
        }
    }
    catch (COMException comException)
    {
        Console.WriteLine(
            "WARNING: System monitor counter enumeration is unavailable: 0x{0:X8}",
            comException.HResult);
    }
    catch (NotImplementedException notImplementedException)
    {
        Console.WriteLine(
            "WARNING: System monitor counter enumeration is unavailable: {0}",
            notImplementedException.Message);
    }
}
