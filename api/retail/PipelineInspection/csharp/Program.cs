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
// Pipeline Inspection Sample (C#)
//
// Demonstrates how to inspect pipeline states and root signatures from an
// existing PIX GPU capture:
//   1. Create a PIX factory.
//   2. Open a GPU capture document.
//   3. Get the analysis interface.
//   4. Connect to the local GPU.
//   5. Start analysis (requires Windows Developer Mode).
//   6. Enumerate queue events.
//   7. For each draw or dispatch event, get the pipeline state.
//   8. Print pipeline state subobjects and root signature parameters.
//   9. Stop analysis and disconnect.
//
// Usage: PipelineInspection <path-to-gpu-capture-file>
//

using Microsoft.PIX;
using Microsoft.PIX.Extension;
using Microsoft.PIX.Extension.DeviceConnection;
using Microsoft.PIX.Extension.GpuCapture;
using Microsoft.PIX.Extension.GpuCapture.Analysis;
using Microsoft.PIX.Extension.GpuCapture.Resources;
using System.Runtime.InteropServices;

if (args.Length < 1)
{
    Console.Error.WriteLine("Usage: PipelineInspection <path-to-gpu-capture-file>");
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
    // Step 1: Create the PIX factory.
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

    // Steps 6-8: Enumerate queue events and inspect pipeline state.
    InspectPipelineEvents(captureDocument);
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
    // Step 9: Stop analysis and disconnect.
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

void InspectPipelineEvents(IPixGpuCaptureDocument captureDocument)
{
    // Cap inspection so the sample finishes in seconds even on very large
    // captures (e.g. 37 MB VRS captures with hundreds of thousands of events).
    const int maxEventsToInspect = 16;

    var queues = captureDocument.GetQueues();
    int inspectedEventCount = 0;

    for (ulong queueIndex = 0; queueIndex < queues.GetCount() && inspectedEventCount < maxEventsToInspect; ++queueIndex)
    {
        var queueInfo = queues.Get<IPixGpuCaptureQueueInfo>(queueIndex);

        for (uint eventIndex = 0; eventIndex < queueInfo.GetEventCount() && inspectedEventCount < maxEventsToInspect; ++eventIndex)
        {
            PIX_EVENT_INFO eventInfo = queueInfo.GetEvent(eventIndex);
            IPixProgramState programState;

            try
            {
                programState = captureDocument.GetProgramState(ref eventInfo);
            }
            catch (COMException)
            {
                continue;
            }

            D3D12_PROGRAM_TYPE? programType;
            try
            {
                programType = programState.GetGpuProgramType();
            }
            catch (COMException)
            {
                continue;
            }
            if (programType != D3D12_PROGRAM_TYPE.D3D12_PROGRAM_TYPE_GENERIC_PIPELINE)
            {
                continue;
            }

            IPixGenericPipeline genericPipeline;
            PIX_GENERIC_PIPELINE_TYPE pipelineType;
            IPixPipelineState pipelineState;
            try
            {
                genericPipeline = programState.GetGpuProgram<IPixGenericPipeline>();
                // GetPipelineType can fail for events that report a generic-
                // pipeline program type but whose underlying pipeline state
                // is not yet bound (seen in raytracing-only captures).
                // Validate up-front and skip such events instead of partially
                // printing them.
                pipelineType = genericPipeline.GetPipelineType();
                pipelineState = genericPipeline.GetPipelineState();
            }
            catch (COMException)
            {
                continue;
            }

            ++inspectedEventCount;

            Console.WriteLine(
                $"Queue {queueInfo.GetId()} ({GetDisplayString(queueInfo.GetName().ToString(), "(unnamed queue)")}, {queueInfo.GetType()})");
            Console.WriteLine($"  Event {eventIndex}: {GetDisplayString(eventInfo.Name.ToString(), "(unnamed event)")}");
            Console.WriteLine($"  Pipeline type: {pipelineType}");

            PrintPipelineState(pipelineState);

            try
            {
                var rootSignature = genericPipeline.GetGlobalRootSignature();
                PrintRootSignature(rootSignature);
            }
            catch (COMException ex)
            {
                Console.WriteLine($"  Global root signature unavailable (0x{ex.HResult:X8}).");
            }

            Console.WriteLine();
        }
    }

    if (inspectedEventCount == 0)
    {
        Console.WriteLine("No draw or dispatch events with generic pipeline state were found.");
    }
    else if (inspectedEventCount >= maxEventsToInspect)
    {
        Console.WriteLine($"(Stopped after inspecting {maxEventsToInspect} events.)");
    }
}

void PrintPipelineState(IPixPipelineState pipelineState)
{
    var subobjects = pipelineState.GetSubobjects();
    Console.WriteLine($"  Pipeline state subobjects: {subobjects.Count}");

    for (int subobjectIndex = 0; subobjectIndex < subobjects.Count; ++subobjectIndex)
    {
        Console.WriteLine($"    [{subobjectIndex}] {subobjects[subobjectIndex].Type}");
    }
}

void PrintRootSignature(IPixRootSignature rootSignature)
{
    Console.WriteLine(
        $"  Global root signature: {GetDisplayString(rootSignature.GetName().ToString(), "(unnamed root signature)")}");
    Console.WriteLine($"    ApiObjectId: {rootSignature.GetApiObjectId()}");

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = rootSignature.GetVersionedDesc();
    switch (versionedDesc.Version)
    {
        case D3D_ROOT_SIGNATURE_VERSION.D3D_ROOT_SIGNATURE_VERSION_1_0:
            Console.WriteLine("    Version: 1.0");
            PrintRootParameters(versionedDesc.GetRootParameters());
            Console.WriteLine($"    Static samplers: {versionedDesc.Anonymous.Desc_1_0.NumStaticSamplers}");
            break;
        case D3D_ROOT_SIGNATURE_VERSION.D3D_ROOT_SIGNATURE_VERSION_1_1:
            Console.WriteLine("    Version: 1.1");
            PrintRootParameters1(versionedDesc.GetRootParameters1());
            Console.WriteLine($"    Static samplers: {versionedDesc.Anonymous.Desc_1_1.NumStaticSamplers}");
            break;
        case D3D_ROOT_SIGNATURE_VERSION.D3D_ROOT_SIGNATURE_VERSION_1_2:
            Console.WriteLine("    Version: 1.2");
            PrintRootParameters1(versionedDesc.GetRootParameters1());
            Console.WriteLine($"    Static samplers: {versionedDesc.Anonymous.Desc_1_2.NumStaticSamplers}");
            break;
        default:
            Console.WriteLine($"    Version: {versionedDesc.Version}");
            break;
    }
}

void PrintRootParameters(IReadOnlyList<RootParameter> rootParameters)
{
    Console.WriteLine($"    Root parameters: {rootParameters.Count}");

    for (int parameterIndex = 0; parameterIndex < rootParameters.Count; ++parameterIndex)
    {
        RootParameter rootParameter = rootParameters[parameterIndex];
        Console.WriteLine(
            $"      [{parameterIndex}] Type={rootParameter.Type}, Visibility={rootParameter.ShaderVisibility}");

        if (rootParameter.DescriptorTable.HasValue)
        {
            RootParameterDescriptorTable descriptorTable = rootParameter.DescriptorTable.Value;
            Console.WriteLine($"        Descriptor ranges: {descriptorTable.NumDescriptorRanges}");
            PrintDescriptorRanges(descriptorTable.DescriptorRanges);
            continue;
        }

        if (rootParameter.Descriptor.HasValue)
        {
            var rootDescriptor = rootParameter.Descriptor.Value;
            Console.WriteLine(
                $"        Register={rootDescriptor.ShaderRegister}, Space={rootDescriptor.RegisterSpace}");
            continue;
        }

        if (rootParameter.Constant.HasValue)
        {
            var constants = rootParameter.Constant.Value;
            Console.WriteLine(
                $"        Register={constants.ShaderRegister}, Space={constants.RegisterSpace}, Values={constants.Num32BitValues}");
        }
    }
}

void PrintRootParameters1(IReadOnlyList<RootParameter1> rootParameters)
{
    Console.WriteLine($"    Root parameters: {rootParameters.Count}");

    for (int parameterIndex = 0; parameterIndex < rootParameters.Count; ++parameterIndex)
    {
        RootParameter1 rootParameter = rootParameters[parameterIndex];
        Console.WriteLine(
            $"      [{parameterIndex}] Type={rootParameter.Type}, Visibility={rootParameter.ShaderVisibility}");

        if (rootParameter.DescriptorTable.HasValue)
        {
            RootParameterDescriptorTable1 descriptorTable = rootParameter.DescriptorTable.Value;
            Console.WriteLine($"        Descriptor ranges: {descriptorTable.NumDescriptorRanges}");
            PrintDescriptorRanges1(descriptorTable.DescriptorRanges);
            continue;
        }

        if (rootParameter.Descriptor.HasValue)
        {
            var rootDescriptor = rootParameter.Descriptor.Value;
            Console.WriteLine(
                $"        Register={rootDescriptor.ShaderRegister}, Space={rootDescriptor.RegisterSpace}");
            continue;
        }

        if (rootParameter.Constant.HasValue)
        {
            var constants = rootParameter.Constant.Value;
            Console.WriteLine(
                $"        Register={constants.ShaderRegister}, Space={constants.RegisterSpace}, Values={constants.Num32BitValues}");
        }
    }
}

void PrintDescriptorRanges(IReadOnlyList<D3D12_DESCRIPTOR_RANGE> descriptorRanges)
{
    for (int rangeIndex = 0; rangeIndex < descriptorRanges.Count; ++rangeIndex)
    {
        D3D12_DESCRIPTOR_RANGE descriptorRange = descriptorRanges[rangeIndex];
        Console.WriteLine(
            $"        Range {rangeIndex}: {descriptorRange.RangeType}, Register={descriptorRange.BaseShaderRegister}, Space={descriptorRange.RegisterSpace}, Descriptors={descriptorRange.NumDescriptors}");
    }
}

void PrintDescriptorRanges1(IReadOnlyList<D3D12_DESCRIPTOR_RANGE1> descriptorRanges)
{
    for (int rangeIndex = 0; rangeIndex < descriptorRanges.Count; ++rangeIndex)
    {
        D3D12_DESCRIPTOR_RANGE1 descriptorRange = descriptorRanges[rangeIndex];
        Console.WriteLine(
            $"        Range {rangeIndex}: {descriptorRange.RangeType}, Register={descriptorRange.BaseShaderRegister}, Space={descriptorRange.RegisterSpace}, Descriptors={descriptorRange.NumDescriptors}");
    }
}

string GetDisplayString(string? value, string fallbackValue)
{
    return string.IsNullOrWhiteSpace(value) ? fallbackValue : value;
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
