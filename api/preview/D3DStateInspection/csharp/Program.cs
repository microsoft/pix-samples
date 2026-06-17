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
// Program State Inspection Sample (C#)
//
// Opens a .wpix GPU capture, starts analysis, finds the first
// Draw / Dispatch / DispatchRays / DispatchMesh event, and prints the
// program / pipeline state at that event:
//   - Program type (Graphics, Compute, Mesh, Raytracing, etc.)
//   - Global root signature (name, ApiObjectId)
//   - Pipeline state subobjects (count + per-subobject types)
//   - Shaders bound to the program (per-stage IDs)
//
// Uses IPixGpuCaptureDocument.GetProgramState(eventInfo) -- the
// public, retail-shippable entry point that returns IPixProgramState ->
// IPixGpuProgram / IPixGenericPipeline (root signature, pipeline state,
// shaders). This works against any GPU capture that contains at least
// one supported program-driven event, including non-graphics events
// (Dispatch / DispatchRays / DispatchMesh).
//
// Runtime D3D state (viewports, scissor rects, vertex/index buffers,
// root parameter values bound at the time of the draw) is not part of
// program/pipeline state and lives on a separate IPixD3DState API path
// reachable from postmortem dumps; it is intentionally outside this
// sample's scope.
//

using Microsoft.PIX;
using Microsoft.PIX.Extension;
using Microsoft.PIX.Extension.DeviceConnection;
using Microsoft.PIX.Extension.GpuCapture;
using Microsoft.PIX.Extension.GpuCapture.Resources;
using Microsoft.PIX.Internal;
using Microsoft.PIX.Internal.Extension.GpuCapture;
using Microsoft.PIX.Internal.Extension.GpuCapture.Analysis;
using System.Runtime.InteropServices;

if (args.Length < 1)
{
    Console.Error.WriteLine("Usage: D3DStateInspection <path-to-capture.wpix>");
    return 1;
}

string capturePath = Path.GetFullPath(args[0]);
if (!File.Exists(capturePath))
{
    Console.Error.WriteLine($"File not found: {capturePath}");
    return 1;
}

var factory = PixApiExtensions.PixCreateFactory<IPixFactory>();
var captureDocument = factory.OpenGpuCaptureDocument<IPixGpuCaptureDocument>(capturePath);
StartAnalysis(captureDocument);
try
{
    var selectedEvent = FindFirstGpuProgramEvent(captureDocument);
    if (selectedEvent is null)
    {
        Console.Error.WriteLine("No Draw or Dispatch event was found in the GPU capture.");
        return 1;
    }

    // GetProgramState returns IPixProgramState for any Draw / Dispatch /
    // DispatchRays / DispatchMesh event. From there, IPixGpuProgram exposes
    // the global root signature and bound shaders, and IPixGenericPipeline
    // (queryable from IPixGpuProgram for graphics/compute pipelines) gives
    // us the pipeline state object's subobject list.
    PIX_EVENT_INFO eventInfo = selectedEvent.Value.eventInfo;
    var programState = captureDocument.GetProgramState(ref eventInfo);

    PrintProgramType(programState);

    var gpuProgram = programState.GetGpuProgram<IPixGpuProgram>();
    PrintGlobalRootSignature(gpuProgram);
    PrintShaders(gpuProgram);

    // Pipeline state subobjects only apply to graphics/compute (generic)
    // pipelines; for raytracing/work-graph the pipeline is described by
    // state-object subobjects which surface here too via IPixPipelineState.
    if (gpuProgram is IPixGenericPipeline genericPipeline)
    {
        PrintPipelineSubobjects(genericPipeline);
    }

    return 0;
}
finally
{
    // Releasing the analysis interface alone does not tear down the
    // device-side session, which can surface as "analysis already
    // running" on back-to-back CI runs. Mirror the cleanup pattern used
    // by the other analysis-using samples.
    StopAnalysis(captureDocument);
}

// ---- helpers ----

static (ulong queueIndex, uint eventIndex, PIX_EVENT_INFO eventInfo)? FindFirstGpuProgramEvent(IPixGpuCaptureDocument captureDocument)
{
    // Match any event the program-state API supports: Draw, Dispatch (compute),
    // DispatchRays (raytracing), DispatchMesh (mesh shader). Per IPixProgramState's
    // contract, the API returns failure for anything outside this set.
    string[] programEventPrefixes = { "Draw", "Dispatch" };

    var queues = captureDocument.GetQueues();
    for (ulong queueIndex = 0; queueIndex < queues.GetCount(); queueIndex++)
    {
        var queueInfo = queues.Get<IPixGpuCaptureQueueInfo>(queueIndex);
        var queueType = queueInfo.GetType();
        if (queueType != PIX_QUEUE_TYPE.PIX_QUEUE_TYPE_GRAPHICS &&
            queueType != PIX_QUEUE_TYPE.PIX_QUEUE_TYPE_COMPUTE)
        {
            continue;
        }

        for (uint eventIndex = 0; eventIndex < queueInfo.GetEventCount(); eventIndex++)
        {
            PIX_EVENT_INFO eventInfo = queueInfo.GetEvent(eventIndex);
            string eventName = eventInfo.Name.ToString() ?? string.Empty;
            foreach (var prefix in programEventPrefixes)
            {
                if (eventName.StartsWith(prefix, StringComparison.Ordinal))
                {
                    Console.WriteLine($"Found program event \"{eventName}\" @QueueIndex {queueIndex}, EventIndex {eventIndex}");
                    return (queueIndex, eventIndex, eventInfo);
                }
            }
        }
    }
    return null;
}

static unsafe void StartAnalysis(IPixGpuCaptureDocument captureDocument)
{
    var analysis = captureDocument.GetAnalysisExperimental();
    Microsoft.PIX.PIX_CONNECTION_DESC_LOCAL localConnection = default;
    Microsoft.PIX.PIX_CONNECTION_DESC analysisConnectionDesc = default;
    analysisConnectionDesc.Type = Microsoft.PIX.PIX_CONNECTION_TYPE.PIX_CONNECTION_TYPE_LOCAL;
    analysisConnectionDesc.Anonymous.pLocal = &localConnection;
    Internal_IPixGpuCaptureAnalysisExperimental_Extensions.Connect(
        analysis, in analysisConnectionDesc, null);

    try
    {
        analysis.StartAnalysis();
    }
    catch (COMException ex)
    {
        Console.Error.WriteLine($"StartAnalysis failed (0x{ex.HResult:X8})");
        throw;
    }
}

static void StopAnalysis(IPixGpuCaptureDocument captureDocument)
{
    try
    {
        var analysis = captureDocument.GetAnalysisExperimental();
        analysis.StopAnalysis();
        analysis.Disconnect();
    }
    catch (COMException)
    {
        // Best-effort: if analysis is in a state that doesn't support stop/disconnect,
        // surface nothing -- the document is being torn down anyway.
    }
}

static void PrintProgramType(IPixProgramState programState)
{
    D3D12_PROGRAM_TYPE? programType = programState.GetGpuProgramType();
    if (programType is null)
    {
        Console.WriteLine("Program type: (unavailable)");
        return;
    }
    string typeName = programType switch
    {
        D3D12_PROGRAM_TYPE.D3D12_PROGRAM_TYPE_GENERIC_PIPELINE => "GenericPipeline",
        D3D12_PROGRAM_TYPE.D3D12_PROGRAM_TYPE_RAYTRACING_PIPELINE => "RaytracingPipeline",
        D3D12_PROGRAM_TYPE.D3D12_PROGRAM_TYPE_WORK_GRAPH => "WorkGraph",
        _ => $"Unknown ({(uint)programType})",
    };
    Console.WriteLine($"Program type: {typeName} ({(uint)programType})");
}

static void PrintGlobalRootSignature(IPixGpuProgram gpuProgram)
{
    IPixRootSignature? rootSignature;
    try
    {
        rootSignature = gpuProgram.GetGlobalRootSignature();
    }
    catch (COMException)
    {
        Console.WriteLine("Global root signature: (not bound)");
        return;
    }
    if (rootSignature is null)
    {
        Console.WriteLine("Global root signature: (not bound)");
        return;
    }
    string name = rootSignature.GetName().ToString() ?? string.Empty;
    Console.WriteLine($"Global root signature: {(string.IsNullOrEmpty(name) ? "(unnamed)" : name)}");
    Console.WriteLine($"  ApiObjectId: {rootSignature.GetApiObjectId()}");
}

static void PrintPipelineSubobjects(IPixGenericPipeline pipeline)
{
    IPixPipelineState? pipelineState;
    try
    {
        pipelineState = pipeline.GetPipelineState();
    }
    catch (COMException)
    {
        Console.WriteLine("Pipeline state: (not available)");
        return;
    }
    if (pipelineState is null)
    {
        Console.WriteLine("Pipeline state: (not available)");
        return;
    }

    ulong subobjectCount = pipelineState.GetSubobjectCount();
    Console.WriteLine($"Pipeline state subobjects ({subobjectCount}):");
    for (ulong i = 0; i < subobjectCount; i++)
    {
        try
        {
            D3D12_STATE_SUBOBJECT subobject = pipelineState.GetSubobject(i);
            Console.WriteLine($"  [{i}] {subobject.Type} ({(uint)subobject.Type})");
        }
        catch (COMException)
        {
            Console.WriteLine($"  [{i}] <retrieve failed>");
        }
    }
}

static void PrintShaders(IPixGpuProgram gpuProgram)
{
    IPixCollection? shaders;
    try
    {
        shaders = gpuProgram.GetShaders();
    }
    catch (COMException)
    {
        Console.WriteLine("Shaders: (not available)");
        return;
    }
    if (shaders is null)
    {
        Console.WriteLine("Shaders: (not available)");
        return;
    }

    ulong shaderCount = shaders.GetCount();
    Console.WriteLine($"Shaders ({shaderCount}):");
    for (ulong i = 0; i < shaderCount; i++)
    {
        try
        {
            var shader = shaders.Get<IPixShader>(i);
            Console.WriteLine($"  [{i}] ShaderId={shader.GetId()}, HashSizeBytes={shader.GetHashSizeBytes()}");
        }
        catch (COMException)
        {
            Console.WriteLine($"  [{i}] <get failed>");
        }
    }
}
