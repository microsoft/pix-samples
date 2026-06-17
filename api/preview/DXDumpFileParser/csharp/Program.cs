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
// DX Dump File Parser Sample (C#)
//
// Demonstrates parsing DirectX dump files (.dxdmp)
// using the experimental PIX API. Extracts and prints:
//   - Dump metadata (app name, creation time, device error)
//   - Device error bucket and summary
//   - Command queue event history (D3D API calls, markers, driver events)
//   - GPU state tables at dump time
//   - Page fault information
//   - Resource details (GPU VA, size, dimension, attributes)
//   - Application blobs and shader debugging data
//
// Usage: DXDumpFileParser <path-to-dxdmp-file>
//

using Microsoft.PIX;
using Microsoft.PIX.Extension;
using Microsoft.PIX.Internal;
using Microsoft.PIX.Internal.Extension.PostmortemDump;
using Microsoft.PIX.Internal.Extension.ShaderDebugging;
using System.Text;

if (args.Length < 1)
{
    Console.Error.WriteLine("Usage: DXDumpFileParser <path-to-dxdmp-file>");
    return 1;
}

var factory = PixApiExtensions.PixCreateFactory<IPixFactoryExperimental>();
OpenPostmortemDump(args[0]);
return 0;

string GetString(string? str, string defaultStr = "(none)")
{
    return !string.IsNullOrEmpty(str) ? str : defaultStr;
}

string DumpPixValue(PIX_VALUE value)
{
    if (value.ValueType == PIX_VALUE_TYPE.PIX_VALUE_STRING)
    {
        return value.Value.ValueString.ToString() ?? string.Empty;
    }
    else
    {
        return $"0x{value.Value.ValueNumeric.Bits:X}";
    }
}

string GetAnnotationLink(IPixStringAnnotation annotation)
{
    switch (annotation.GetContextType())
    {
        case PIX_STRING_ANNOTATION_CONTEXT_TYPE.PIX_STRING_ANNOTATION_SHADER:
            return "Shader";
        case PIX_STRING_ANNOTATION_CONTEXT_TYPE.PIX_STRING_ANNOTATION_QUEUE:
            {
                var queue = annotation.GetContext<IPixPostmortemQueueInfo>();
                return queue.GetName().ToString() ?? string.Empty;
            }
        case PIX_STRING_ANNOTATION_CONTEXT_TYPE.PIX_STRING_ANNOTATION_EVENT:
            {
                var evt = annotation.GetContext<IPixPostmortemEvent>();
                if (evt is IPixPostmortemD3D12ApiEvent apiEvent)
                {
                    return $"[D3D API] {apiEvent.GetName()}";
                }
                else if (evt is IPixPostmortemCustomMarker customMarker)
                {
                    return $"[MARKER] Source: {customMarker.GetSource()}";
                }
                else if (evt is IPixPostmortemDriverEvent driverEvent)
                {
                    return $"[DRIVER] {driverEvent.GetName()}";
                }
                return string.Empty;
            }
        default:
            return string.Empty;
    }
}

string GetAnnotatedString(IPixAnnotatedString annotatedString)
{
    string rawString = annotatedString.GetString().ToString() ?? string.Empty;

    var annotations = annotatedString.TryGetAnnotations(out _);
    if (annotations == null || annotations.GetCount() == 0ul)
    {
        return rawString;
    }

    var sb = new StringBuilder();
    int currIndex = 0;
    ulong annotationCount = annotations.GetCount();
    for (ulong i = 0ul; i < annotationCount; ++i)
    {
        var annotation = annotations.Get<IPixStringAnnotation>(i);
        var range = annotation.GetRange();
        int startIndex = (int)Math.Min(range.StartIndex, int.MaxValue);
        int length = (int)Math.Min(range.Length, int.MaxValue);

        if (startIndex < currIndex)
        {
            // Ignore overlapping links
            continue;
        }
        if (startIndex > currIndex)
        {
            sb.Append(rawString.Substring(currIndex, startIndex - currIndex));
        }
        sb.Append($"[{rawString.Substring(startIndex, length)}]({GetAnnotationLink(annotation)})");
        currIndex = startIndex + length;
    }

    if (currIndex < rawString.Length)
    {
        sb.Append(rawString.Substring(currIndex));
    }
    return sb.ToString();
}

void DumpMetadata(IPixPostmortemDocument document)
{
    Console.WriteLine("\n===== DirectX dump file metadata =====\n");

    Console.WriteLine($"Version: {document.GetVersion()}");
    Console.WriteLine($"Device Error Code: {document.GetDeviceErrorCode()}");

    var creationTime = document.GetCreationTime();
    long fileTime = ((long)creationTime.dwHighDateTime << 32) | (uint)creationTime.dwLowDateTime;
    DateTime dt = DateTime.FromFileTimeUtc(fileTime);
    Console.WriteLine($"Creation Time: {dt:yyyy-MM-dd HH:mm:ss}");

    var appDesc = document.GetApplicationDescription();
    Console.WriteLine($"Application Name: {GetString(appDesc.D3DApplicationDesc.pName.ToString())}");
    Console.WriteLine($"Executable Name: {GetString(appDesc.D3DApplicationDesc.pExeFilename.ToString())}");
    var ver = appDesc.D3DApplicationDesc.Version;
    Console.WriteLine($"Version: {ver.VersionParts[3]}.{ver.VersionParts[2]}.{ver.VersionParts[1]}.{ver.VersionParts[0]}");
    Console.WriteLine($"Engine Name: {GetString(appDesc.D3DApplicationDesc.pEngineName.ToString())}");
    var engVer = appDesc.D3DApplicationDesc.EngineVersion;
    Console.WriteLine($"Engine Version: {engVer.VersionParts[3]}.{engVer.VersionParts[2]}.{engVer.VersionParts[1]}.{engVer.VersionParts[0]}");
}

void DumpDeviceErrorBucket(IPixPostmortemDocument document)
{
    Console.WriteLine("\n===== Device error bucket =====\n");
    Console.WriteLine($"Bucket Name: {document.GetDeviceErrorBucket()}");
    Console.WriteLine($"Documentation Link: {GetString(document.GetDocumentationLink().ToString())}");

    var briefSummary = document.GetBriefSummary();
    Console.WriteLine($"Brief Summary: {GetAnnotatedString(briefSummary)}");

    var detailedSummary = document.GetDetailedSummary();
    Console.WriteLine($"Detailed Summary: {GetAnnotatedString(detailedSummary)}");
}

void DumpResource(IPixPostmortemD3D12Resource resource, int numTabs)
{
    string tabs = new string('\t', numTabs);

    Console.WriteLine($"{tabs}Resource: {GetString(resource.GetName().ToString(), "(unnamed)")}");
    Console.WriteLine($"{tabs}\tGPU VA: 0x{resource.GetGpuVirtualAddress():X}");
    Console.WriteLine($"{tabs}\tSize: {resource.GetSizeBytes()} bytes");

    var desc = resource.TryGetDesc();
    if (desc.HasValue)
    {
        Console.WriteLine($"{tabs}\tDimension: {desc.Value.Dimension}");
        Console.WriteLine($"{tabs}\tWidth: {desc.Value.Width}, Height: {desc.Value.Height}");
    }

    var attributes = resource.GetAttributes();
    for (int i = 0; i < attributes.Count; ++i)
    {
        var attr = attributes[i];
        Console.WriteLine($"{tabs}\tAttribute[{i}]: {attr.Name} ({attr.Description}) = {DumpPixValue(attr.Value)}");
    }

    var resourceEvents = resource.GetEvents();
    ulong eventCount = resourceEvents.GetCount();
    if (eventCount > 0)
    {
        Console.WriteLine($"{tabs}\tResource events ({eventCount}):");
        for (ulong i = 0ul; i < eventCount; ++i)
        {
            var resourceEvent = resourceEvents.Get<IPixPostmortemResourceEvent>(i);
            var ts = resourceEvent.GetTimestampInNs();
            Console.WriteLine($"{tabs}\t\t[{i}] {resourceEvent.GetType()} @ {ts} ns");
        }
    }
}

void DumpResources(IPixPostmortemDocument document)
{
    var resources = document.GetResources();
    ulong resourceCount = resources.GetCount();
    Console.WriteLine("\n===== Resources =====");

    for (ulong i = 0ul; i < resourceCount; ++i)
    {
        Console.WriteLine();
        DumpResource(resources.Get<IPixPostmortemD3D12Resource>(i), 0);
    }
}

void DumpPageFaults(IPixPostmortemDocument document)
{
    var pageFaults = document.GetPageFaults();
    ulong pageFaultCount = pageFaults.GetCount();
    Console.WriteLine("\n===== Page faults =====");

    for (ulong i = 0ul; i < pageFaultCount; ++i)
    {
        var pageFault = pageFaults.Get<IPixPostmortemPageFault>(i);
        ulong pageFaultTs = pageFault.GetTimestampInNs();

        Console.WriteLine($"\t[GPU VA: 0x{pageFault.GetGpuVirtualAddress():X} @ {pageFaultTs} ns]");
        Console.WriteLine($"\t\tType: {pageFault.GetType()}");
        Console.WriteLine($"\t\tAccess: {pageFault.GetAccessType()}");

        var queue = pageFault.TryGetQueue(out _);
        if (queue != null)
        {
            Console.WriteLine($"\tQueue: {queue.GetName()}");
        }
        else
        {
            Console.WriteLine("\t(unknown)");
        }

        var resources = pageFault.GetResources();
        ulong resourceCount = resources.GetCount();
        Console.WriteLine("\tResources:");
        for (ulong j = 0ul; j < resourceCount; ++j)
        {
            DumpResource(resources.Get<IPixPostmortemD3D12Resource>(j), 2);
        }

        var resourceEvents = pageFault.GetResourceEvents();
        ulong eventCount = resourceEvents.GetCount();
        Console.WriteLine($"\tResource events ({eventCount}):");
        for (ulong j = 0ul; j < eventCount; ++j)
        {
            var resourceEvent = resourceEvents.Get<IPixPostmortemResourceEvent>(j);
            ulong ts = resourceEvent.GetTimestampInNs();
            Console.WriteLine($"\t\t[{j}] {resourceEvent.GetType()} @ {ts} ns");

            var resource = resourceEvent.TryGetResource(out _);
            if (resource != null)
            {
                Console.WriteLine($"\t\tResource: {GetString(resource.GetName().ToString(), "(unnamed)")}");
            }
        }
    }
}

void DumpEvent(IPixPostmortemEvent evt, int numTabs)
{
    string tabs = new string('\t', numTabs);

    if (evt is IPixPostmortemD3D12ApiEvent apiEvent)
    {
        Console.Write($"{tabs}{apiEvent.GetName()}");
    }
    else if (evt is IPixPostmortemStringMarker stringMarker)
    {
        Console.Write($"{tabs}\"{GetString(stringMarker.GetPayload().ToString())}\"");
    }
    else if (evt is IPixPostmortemCustomMarker customMarker)
    {
        Console.Write($"{tabs}Marker source: {customMarker.GetSource()}");
    }
    else if (evt is IPixPostmortemPixMarker pixMarker)
    {
        Console.Write($"{tabs}{pixMarker.GetName()}");
    }
    else if (evt is IPixPostmortemDriverEvent driverEvent)
    {
        Console.Write($"{tabs}{driverEvent.GetName()}");
    }

    var status = evt.GetStatus();
    if (status == PIX_EVENT_STATUS.PIX_EVENT_STATUS_IN_PROGRESS)
    {
        Console.Write(" << [PIX_EVENT_STATUS_IN_PROGRESS]");
    }
    else if (status == PIX_EVENT_STATUS.PIX_EVENT_STATUS_POSSIBLY_COMPLETED)
    {
        Console.Write(" << [PIX_EVENT_STATUS_POSSIBLY_COMPLETED]");
    }
    Console.WriteLine();

    var shaders = evt.TryGetCorrelatedShaders(out _);
    if (shaders != null)
    {
        ulong shaderCount = shaders.GetCount();
        if (shaderCount > 0ul)
        {
            Console.WriteLine($"{tabs}  Correlated shaders ({shaderCount}):");
            for (ulong i = 0ul; i < shaderCount; ++i)
            {
                var shader = shaders.Get<IPixShader>(i);
                Console.Write($"{tabs}\tShader");
                uint hashSize = shader.GetHashSizeBytes();
                if (hashSize > 0u)
                {
                    var hashBytes = new byte[hashSize];
                    shader.GetHash(hashSize, hashBytes);
                    Console.Write($" Hash={BitConverter.ToString(hashBytes).Replace("-", "").ToLower()}");
                }
                else
                {
                    Console.Write($" ID={shader.GetId()}");
                }
                Console.WriteLine($" Stage={shader.GetStage()}");
            }
        }
    }

    var resources = evt.TryGetCorrelatedResources(out _);
    if (resources != null)
    {
        ulong resourceCount = resources.GetCount();
        if (resourceCount > 0ul)
        {
            Console.WriteLine($"{tabs}  Correlated resources ({resourceCount}):");
            for (ulong i = 0ul; i < resourceCount; ++i)
            {
                var resource = resources.Get<IPixPostmortemD3D12Resource>(i);
                Console.WriteLine($"{tabs}\t{GetString(resource.GetName().ToString())}");
            }
        }
    }
}

void DumpChildEvents(IPixPostmortemEvent parentEvent, int numTabs)
{
    var childEvents = parentEvent.GetChildEvents();
    ulong numChildEvents = childEvents.GetCount();

    for (ulong i = 0ul; i < numChildEvents; i++)
    {
        var childEvent = childEvents.Get<IPixPostmortemEvent>(i);
        DumpEvent(childEvent, numTabs);
        DumpChildEvents(childEvent, numTabs + 1);
    }
}

void DumpQueueEvents(IPixPostmortemQueueInfo queue, int numTabs)
{
    var events = queue.GetEvents();
    ulong numEvents = events.GetCount();

    for (ulong i = 0ul; i < numEvents; i++)
    {
        var rootEvent = events.Get<IPixPostmortemEvent>(i);
        DumpEvent(rootEvent, numTabs);
        DumpChildEvents(rootEvent, numTabs + 1);
    }
}

void DumpEngineQueues(IPixPostmortemDocument document)
{
    var queues = document.GetQueues();
    Console.WriteLine("\n===== Engine queues =====");

    for (ulong i = 0ul; i < queues.GetCount(); i++)
    {
        var queueInfo = queues.Get<IPixPostmortemQueueInfo>(i);

        Console.WriteLine($"\n===== [{queueInfo.GetName()}] =====");
        Console.WriteLine($"\tType: {queueInfo.GetType()}");
        Console.WriteLine($"\tStatus: {queueInfo.GetStatus()}");

        uint numStatusFields = queueInfo.GetHardwareStatusCount();
        if (numStatusFields > 0u)
        {
            Console.WriteLine("\n\tHardware status fields:");
            for (uint j = 0u; j < numStatusFields; j++)
            {
                var queueStatus = queueInfo.GetHardwareStatus(j);
                Console.WriteLine($"\t  [{j}] {queueStatus.Name} ({queueStatus.Description}) Severity: {queueStatus.SeverityLevel} Value: {DumpPixValue(queueStatus.Value)}");
            }
        }

        var pageFaults = queueInfo.GetPageFaults();
        ulong pageFaultCount = pageFaults.GetCount();
        if (pageFaultCount > 0ul)
        {
            Console.WriteLine("\n\tPage faults:");
            for (ulong j = 0ul; j < pageFaultCount; ++j)
            {
                var pageFault = pageFaults.Get<IPixPostmortemPageFault>(j);
                Console.WriteLine($"\t  [GPU VA: 0x{pageFault.GetGpuVirtualAddress():X}]");
                Console.WriteLine($"\t\tType: {pageFault.GetType()}");
                Console.WriteLine($"\t\tAccess Type: {pageFault.GetAccessType()}");
            }
        }

        Console.WriteLine("\n\t===== Events =====\n");
        DumpQueueEvents(queueInfo, 1);
    }
}

void DumpGpuStateRow(IPixGpuStateTableRow row, uint numColumns, int depth)
{
    string tabs = new string('\t', depth);
    Console.Write($"{tabs}{row.GetName()}");

    string? description = row.GetDescription().ToString();
    if (!string.IsNullOrEmpty(description))
    {
        Console.Write($" ({description})");
    }

    for (uint i = 0u; i < numColumns; i++)
    {
        Console.Write($"\t\t{DumpPixValue(row.GetValue(i))}");
    }
    Console.WriteLine();

    var childRows = row.GetChildRows();
    ulong childCount = childRows.GetCount();

    if (childCount == 0ul)
    {
        return;
    }

    for (ulong j = 0ul; j < childCount; j++)
    {
        DumpGpuStateRow(childRows.Get<IPixGpuStateTableRow>(j), numColumns, depth + 1);
    }
    Console.WriteLine();
}

void DumpGpuStateAtDumpTime(IPixPostmortemDocument document)
{
    Console.WriteLine("\n===== GPU state (at dump time) =====");

    var gpuStateTables = document.GetGpuStateTables();
    ulong numTables = gpuStateTables.GetCount();

    for (ulong i = 0ul; i < numTables; i++)
    {
        var gpuStateTable = gpuStateTables.Get<IPixGpuStateTable>(i);

        Console.WriteLine($"\n===== [{gpuStateTable.GetName()}] =====\n");

        string? tableDescription = gpuStateTable.GetDescription().ToString();
        if (!string.IsNullOrEmpty(tableDescription))
        {
            Console.WriteLine($"Description: {tableDescription}");
        }

        Console.Write("[Columns]\t");
        uint numColumns = gpuStateTable.GetColumnCount();
        for (uint j = 0u; j < numColumns; j++)
        {
            Console.Write($"\t\t\t\t{gpuStateTable.GetColumnName(j)}");
        }
        Console.WriteLine("\n");

        var rows = gpuStateTable.GetRows();
        ulong numRootRows = rows.GetCount();
        for (ulong k = 0ul; k < numRootRows; k++)
        {
            DumpGpuStateRow(rows.Get<IPixGpuStateTableRow>(k), numColumns, 0);
        }
        Console.WriteLine();
    }
}

void DumpApplicationBlobs(IPixPostmortemDocument document)
{
    var blobs = document.GetApplicationBlobs();
    ulong blobCount = blobs.GetCount();
    Console.WriteLine($"\n===== Application blobs ({blobCount}) =====");

    for (ulong i = 0ul; i < blobCount; ++i)
    {
        var blob = blobs.Get<IPixApplicationBlob>(i);
        Console.WriteLine($"\n  Blob [{i}]");
        Console.WriteLine($"  Metadata: 0x{blob.GetMetadata():X}");
        Console.WriteLine($"  Size: {blob.GetSizeBytes()} bytes");
    }
}

void DumpShaderDebuggingData(IPixPostmortemDocument document)
{
    var shaderDebuggingData = document.TryGetShaderData(out _);
    if (shaderDebuggingData == null)
    {
        Console.WriteLine("\n===== Shader debugging data: (none) =====");
        return;
    }

    var waves = shaderDebuggingData.GetWaves();
    ulong waveCount = waves.GetCount();
    Console.WriteLine($"\n===== Shader debugging data ({waveCount} waves) =====");

    for (ulong i = 0ul; i < waveCount; ++i)
    {
        var wave = waves.Get<IPixShaderWave>(i);
        Console.WriteLine($"\n  Wave [{i}]");
        Console.WriteLine($"  Status: {wave.GetStatus()}");
        Console.WriteLine($"  Stage: {wave.GetStage()}");

        var lanes = wave.GetLanes();
        Console.WriteLine($"  Lane count: {lanes.GetCount()}");
    }
}

void OpenPostmortemDump(string dumpFile)
{
    var document = factory.OpenPostmortemDumpDocument(dumpFile, /*logger*/ null, /*notifications*/ null, /*cancellationToken*/ null);

    DumpMetadata(document);
    DumpDeviceErrorBucket(document);
    DumpEngineQueues(document);
    DumpGpuStateAtDumpTime(document);
    DumpPageFaults(document);
    DumpResources(document);
    DumpApplicationBlobs(document);
    DumpShaderDebuggingData(document);
}
