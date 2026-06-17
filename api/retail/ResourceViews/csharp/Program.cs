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
// Resource Views Sample (C#)
//
// Demonstrates inspecting D3D12 resources accessed by a draw call in a PIX GPU
// capture:
//   1. Create a PIX factory.
//   2. Open a GPU capture document.
//   3. Get analysis, connect, and start analysis.
//   4. Find a draw call event on a graphics queue.
//   5. Gather accessed resources for that event.
//   6. Print resource name, dimensions, format, and type.
//   7. Print associated resource view information.
//   8. Stop analysis and clean up.
//
// Usage: ResourceViews <path-to-gpu-capture-file>
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
    Console.Error.WriteLine("Usage: ResourceViews <path-to-gpu-capture-file>");
    return 1;
}

string captureFilePath = Path.GetFullPath(args[0]);
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
    IPixFactory factory = PixApiExtensions.PixCreateFactory<IPixFactory>();

    // Step 2: Open the GPU capture document.
    IPixGpuCaptureDocument captureDocument = factory.OpenGpuCaptureDocument<IPixGpuCaptureDocument>(captureFilePath);

    // Step 3: Get analysis, connect, and start analysis.
    IPixD3D12Resources resources = PixApiExtensionsGpuCapture.GetD3D12Resources(captureDocument);
    Console.WriteLine("Capture path: {0}", captureFilePath);
    Console.WriteLine("D3D12 resources in capture: {0}", resources.GetCount());

    var drawCallSelection = FindFirstDrawCallEvent(captureDocument.GetQueues());
    Console.WriteLine("Selected draw call: QueueIndex={0}, EventIndex={1}", drawCallSelection.QueueIndex, drawCallSelection.EventIndex);
    Console.WriteLine("Queue: {0}", drawCallSelection.QueueName);
    Console.WriteLine("Adapter: {0}", drawCallSelection.AdapterName);
    Console.WriteLine("EventName: {0}", GetAnsiString(drawCallSelection.EventInfo.Name));

    analysis = captureDocument.GetAnalysis();
    analysis.Connect(Microsoft.PIX.Extension.DeviceConnection.PIX_CONNECTION_DESC.CreateLocal());
    analysisConnected = true;

    analysis.StartAnalysis();
    analysisStarted = true;

    // Step 4-5: Resolve the draw call's bound views, then gather accessed resources.
    PIX_EVENT_INFO drawEventInfo = drawCallSelection.EventInfo;
    IPixProgramState programState = PixApiExtensionsGpuCapture.GetProgramState(captureDocument, ref drawEventInfo);

    Guid genericPipelineGuid = typeof(IPixGenericPipeline).GUID;
    Microsoft.PIX._IPixProgramState_Extensions.GetGpuProgram(programState, in genericPipelineGuid, out object genericPipelineObject);
    IPixGenericPipeline genericPipeline = (IPixGenericPipeline)genericPipelineObject;

    IPixResourceViewsAtEvent resourceViewsAtEvent = PixApiExtensionsGpuCaptureResources.GetResourceViews(genericPipeline);
    analysis.GatherAccessedResources();
    analysis.GetAccessedResources(resourceViewsAtEvent);

    // Step 6-7: Print accessed resource details and associated view info.
    PrintAccessedResources(resourceViewsAtEvent);

    Console.WriteLine();
    Console.WriteLine("Resource views sample completed successfully.");
    return 0;
}
catch (COMException exception)
{
    WriteDeveloperModeHelpIfNeeded(exception.HResult);
    Console.Error.WriteLine("COM error: 0x{0:X8}", exception.HResult);
    Console.Error.WriteLine(exception.Message);
    return 1;
}
catch (Exception exception)
{
    Console.Error.WriteLine("ERROR: {0}", exception.Message);
    return 1;
}
finally
{
    // Step 8: Stop analysis and clean up.
    if (analysis != null)
    {
        if (analysisStarted)
        {
            try
            {
                analysis.StopAnalysis();
            }
            catch (COMException cleanupException)
            {
                Console.Error.WriteLine("Warning: StopAnalysis failed during cleanup (0x{0:X8}).", cleanupException.HResult);
            }
        }

        if (analysisConnected)
        {
            try
            {
                analysis.Disconnect();
            }
            catch (COMException cleanupException)
            {
                Console.Error.WriteLine("Warning: Disconnect failed during cleanup (0x{0:X8}).", cleanupException.HResult);
            }
        }
    }
}

static DrawCallSelection FindFirstDrawCallEvent(IPixCollection queues)
{
    for (ulong queueIndex = 0; queueIndex < queues.GetCount(); queueIndex++)
    {
        IPixGpuCaptureQueueInfo queueInfo = queues.Get<IPixGpuCaptureQueueInfo>(queueIndex);
        if (queueInfo.GetType() != PIX_QUEUE_TYPE.PIX_QUEUE_TYPE_GRAPHICS)
        {
            continue;
        }

        for (uint eventIndex = 0; eventIndex < queueInfo.GetEventCount(); eventIndex++)
        {
            PIX_EVENT_INFO eventInfo = queueInfo.GetEvent(eventIndex);
            if (!IsDrawCallEvent(eventInfo))
            {
                continue;
            }

            return new DrawCallSelection(
                queueIndex,
                eventIndex,
                GetWideString(queueInfo.GetName(), "(unnamed queue)"),
                GetWideString(queueInfo.GetAdapterName(), "(unknown adapter)"),
                eventInfo);
        }
    }

    throw new InvalidOperationException("No draw call event was found in the capture.");
}

static bool IsDrawCallEvent(PIX_EVENT_INFO eventInfo)
{
    string eventName = GetAnsiString(eventInfo.Name);
    string apiCallData = GetAnsiString(eventInfo.ApiCallData);

    return eventName.Contains("draw", StringComparison.OrdinalIgnoreCase) ||
        apiCallData.Contains("draw", StringComparison.OrdinalIgnoreCase);
}

static unsafe void PrintAccessedResources(IPixResourceViewsAtEvent resourceViewsAtEvent)
{
    Dictionary<ulong, ResourceDetails> resourcesById = new();

    Console.WriteLine("Accessed resource views at the selected draw: {0}", resourceViewsAtEvent.GetCount());
    for (uint viewIndex = 0; viewIndex < resourceViewsAtEvent.GetCount(); viewIndex++)
    {
        IPixResourceView view = PixApiExtensionsGpuCaptureResources.GetResourceView<IPixResourceView>(resourceViewsAtEvent, viewIndex);
        IPixD3D12Resource? resource = TryGetResourceFromView(resourceViewsAtEvent, viewIndex, view.GetType(), out string eventViewDescription);
        if (resource == null)
        {
            continue;
        }

        ulong apiObjectId = resource.GetApiObjectId();
        if (!resourcesById.TryGetValue(apiObjectId, out ResourceDetails? resourceDetails))
        {
            resourceDetails = new ResourceDetails(resource);
            resourcesById.Add(apiObjectId, resourceDetails);
        }

        resourceDetails.EventViewDescriptions.Add(eventViewDescription);
    }

    Console.WriteLine("Unique accessed D3D12 resources: {0}", resourcesById.Count);
    foreach ((ulong apiObjectId, ResourceDetails resourceDetails) in resourcesById)
    {
        D3D12_RESOURCE_DESC2* resourceDescription = resourceDetails.Resource.GetResourceDesc();

        Console.WriteLine();
        Console.WriteLine("Resource 0x{0:X}", apiObjectId);
        Console.WriteLine("    Name: {0}", GetWideString(resourceDetails.Resource.GetName(), "(unnamed)"));
        Console.WriteLine("    Type: {0}", GetResourceTypeName(resourceDetails.Resource.GetType()));

        if (resourceDescription != null)
        {
            Console.WriteLine("    Dimension: {0}", resourceDescription->Dimension);
            Console.WriteLine(
                "    Size: {0} x {1} x {2}",
                resourceDescription->Width,
                resourceDescription->Height,
                resourceDescription->DepthOrArraySize);
            Console.WriteLine("    MipLevels: {0}", resourceDescription->MipLevels);
            Console.WriteLine("    Format: {0}", resourceDescription->Format);
        }

        if (resourceDetails.EventViewDescriptions.Count > 0)
        {
            Console.WriteLine("    EventViews:");
            foreach (string eventViewDescription in resourceDetails.EventViewDescriptions)
            {
                Console.WriteLine("      - {0}", eventViewDescription);
            }
        }

        PrintAssociatedViews(resourceDetails.Resource);
    }
}

static unsafe void PrintAssociatedViews(IPixD3D12Resource resource)
{
    IPixResourceViewsForResource resourceViews = PixApiExtensionsGpuCaptureResources.GetD3D12ResourceViews(resource);
    Console.WriteLine("    AssociatedViews: {0}", resourceViews.GetCount());

    for (uint viewIndex = 0; viewIndex < resourceViews.GetCount(); viewIndex++)
    {
        IPixResourceView view = PixApiExtensionsGpuCaptureResources.GetResourceView<IPixResourceView>(resourceViews, viewIndex);
        string viewDescription = DescribeView(resourceViews, viewIndex, view.GetType());
        Console.WriteLine("      - {0}", viewDescription);
    }
}

static unsafe IPixD3D12Resource? TryGetResourceFromView(
    IPixResourceViews resourceViews,
    uint viewIndex,
    PIX_RESOURCE_VIEW_TYPE viewType,
    out string viewDescription)
{
    viewDescription = DescribeView(resourceViews, viewIndex, viewType);

    IPixD3D12ResourceView? d3d12ResourceView = viewType switch
    {
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_CONSTANT_BUFFER_VIEW => PixApiExtensionsGpuCaptureResources.GetResourceView<IPixConstantBufferView>(resourceViews, viewIndex),
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_VERTEX_BUFFER_VIEW => PixApiExtensionsGpuCaptureResources.GetResourceView<IPixVertexBufferView>(resourceViews, viewIndex),
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_INDEX_BUFFER_VIEW => PixApiExtensionsGpuCaptureResources.GetResourceView<IPixIndexBufferView>(resourceViews, viewIndex),
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_STREAM_OUTPUT_VIEW => PixApiExtensionsGpuCaptureResources.GetResourceView<IPixStreamOutputView>(resourceViews, viewIndex),
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_SHADER_RESOURCE_VIEW => PixApiExtensionsGpuCaptureResources.GetResourceView<IPixShaderResourceView>(resourceViews, viewIndex),
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_RENDER_TARGET_VIEW => PixApiExtensionsGpuCaptureResources.GetResourceView<IPixRenderTargetView>(resourceViews, viewIndex),
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_UNORDERED_ACCESS_VIEW => PixApiExtensionsGpuCaptureResources.GetResourceView<IPixUnorderedAccessView>(resourceViews, viewIndex),
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_DEPTH_STENCIL_VIEW => PixApiExtensionsGpuCaptureResources.GetResourceView<IPixDepthStencilView>(resourceViews, viewIndex),
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_SHADING_RATE_IMAGE => PixApiExtensionsGpuCaptureResources.GetResourceView<IPixD3D12ResourceView>(resourceViews, viewIndex),
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_BUFFER => PixApiExtensionsGpuCaptureResources.GetResourceView<IPixBufferResourceView>(resourceViews, viewIndex),
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_TEXTURE => PixApiExtensionsGpuCaptureResources.GetResourceView<IPixTextureResourceView>(resourceViews, viewIndex),
        _ => null
    };

    if (d3d12ResourceView == null)
    {
        return null;
    }

    Guid resourceGuid = typeof(IPixD3D12Resource).GUID;
    Microsoft.PIX._IPixD3D12ResourceView_Extensions.GetD3D12Resource(d3d12ResourceView, in resourceGuid, out object resourceObject);
    return (IPixD3D12Resource)resourceObject;
}

static unsafe string DescribeView(IPixResourceViews resourceViews, uint viewIndex, PIX_RESOURCE_VIEW_TYPE viewType)
{
    switch (viewType)
    {
        case PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_CONSTANT_BUFFER_VIEW:
        {
            IPixConstantBufferView constantBufferView = PixApiExtensionsGpuCaptureResources.GetResourceView<IPixConstantBufferView>(resourceViews, viewIndex);
            D3D12_CONSTANT_BUFFER_VIEW_DESC* constantBufferDescription = null;
            constantBufferView.GetDesc(&constantBufferDescription);
            return constantBufferDescription == null
                ? "CBV"
                : $"CBV SizeInBytes={constantBufferDescription->SizeInBytes}, Offset={constantBufferView.GetBufferLocationOffset()}";
        }

        case PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_VERTEX_BUFFER_VIEW:
        {
            IPixVertexBufferView vertexBufferView = PixApiExtensionsGpuCaptureResources.GetResourceView<IPixVertexBufferView>(resourceViews, viewIndex);
            D3D12_VERTEX_BUFFER_VIEW* vertexBufferDescription = vertexBufferView.GetDesc();
            return vertexBufferDescription == null
                ? "VBV"
                : $"VBV StrideInBytes={vertexBufferDescription->StrideInBytes}, SizeInBytes={vertexBufferDescription->SizeInBytes}, Offset={vertexBufferView.GetBufferLocationOffset()}";
        }

        case PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_INDEX_BUFFER_VIEW:
        {
            IPixIndexBufferView indexBufferView = PixApiExtensionsGpuCaptureResources.GetResourceView<IPixIndexBufferView>(resourceViews, viewIndex);
            D3D12_INDEX_BUFFER_VIEW* indexBufferDescription = indexBufferView.GetDesc();
            return indexBufferDescription == null
                ? "IBV"
                : $"IBV Format={indexBufferDescription->Format}, SizeInBytes={indexBufferDescription->SizeInBytes}, Offset={indexBufferView.GetBufferLocationOffset()}";
        }

        case PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_STREAM_OUTPUT_VIEW:
        {
            IPixStreamOutputView streamOutputView = PixApiExtensionsGpuCaptureResources.GetResourceView<IPixStreamOutputView>(resourceViews, viewIndex);
            D3D12_STREAM_OUTPUT_BUFFER_VIEW* streamOutputDescription = streamOutputView.GetDesc();
            return streamOutputDescription == null
                ? "SOV"
                : $"SOV SizeInBytes={streamOutputDescription->SizeInBytes}, Offset={streamOutputView.GetBufferLocationOffset()}";
        }

        case PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_SHADER_RESOURCE_VIEW:
        {
            IPixShaderResourceView shaderResourceView = PixApiExtensionsGpuCaptureResources.GetResourceView<IPixShaderResourceView>(resourceViews, viewIndex);
            D3D12_SHADER_RESOURCE_VIEW_DESC* shaderResourceDescription = null;
            shaderResourceView.GetDesc(&shaderResourceDescription);
            string description = shaderResourceDescription == null
                ? "SRV"
                : $"SRV Format={shaderResourceDescription->Format}, ViewDimension={shaderResourceDescription->ViewDimension}";

            ulong locationOffset = 0;
            try
            {
                shaderResourceView.GetLocationOffset(ref locationOffset);
                description += $", LocationOffset={locationOffset}";
            }
            catch (COMException)
            {
            }

            return description;
        }

        case PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_RENDER_TARGET_VIEW:
        {
            IPixRenderTargetView renderTargetView = PixApiExtensionsGpuCaptureResources.GetResourceView<IPixRenderTargetView>(resourceViews, viewIndex);
            D3D12_RENDER_TARGET_VIEW_DESC* renderTargetDescription = renderTargetView.GetDesc();
            return renderTargetDescription == null
                ? "RTV"
                : $"RTV Format={renderTargetDescription->Format}, ViewDimension={renderTargetDescription->ViewDimension}";
        }

        case PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_UNORDERED_ACCESS_VIEW:
        {
            IPixUnorderedAccessView unorderedAccessView = PixApiExtensionsGpuCaptureResources.GetResourceView<IPixUnorderedAccessView>(resourceViews, viewIndex);
            D3D12_UNORDERED_ACCESS_VIEW_DESC* unorderedAccessDescription = null;
            unorderedAccessView.GetDesc(&unorderedAccessDescription);
            return unorderedAccessDescription == null
                ? "UAV"
                : $"UAV Format={unorderedAccessDescription->Format}, ViewDimension={unorderedAccessDescription->ViewDimension}";
        }

        case PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_DEPTH_STENCIL_VIEW:
        {
            IPixDepthStencilView depthStencilView = PixApiExtensionsGpuCaptureResources.GetResourceView<IPixDepthStencilView>(resourceViews, viewIndex);
            D3D12_DEPTH_STENCIL_VIEW_DESC* depthStencilDescription = depthStencilView.GetDesc();
            return depthStencilDescription == null
                ? "DSV"
                : $"DSV Format={depthStencilDescription->Format}, ViewDimension={depthStencilDescription->ViewDimension}";
        }

        case PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_BUFFER:
        {
            IPixBufferResourceView bufferResourceView = PixApiExtensionsGpuCaptureResources.GetResourceView<IPixBufferResourceView>(resourceViews, viewIndex);
            return $"Buffer SizeInBytes={bufferResourceView.GetSizeInBytes()}, Offset={bufferResourceView.GetBufferLocationOffset()}";
        }

        case PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_TEXTURE:
        {
            IPixTextureResourceView textureResourceView = PixApiExtensionsGpuCaptureResources.GetResourceView<IPixTextureResourceView>(resourceViews, viewIndex);
            D3D12_BARRIER_SUBRESOURCE_RANGE subresourceRange = default;
            textureResourceView.GetSubresourceRange(&subresourceRange);
            return $"Texture FirstMip={subresourceRange.IndexOrFirstMipLevel}, NumMips={subresourceRange.NumMipLevels}, FirstArraySlice={subresourceRange.FirstArraySlice}, NumArraySlices={subresourceRange.NumArraySlices}";
        }

        default:
            return GetViewTypeName(viewType);
    }
}

static string GetResourceTypeName(PIX_D3D12_RESOURCE_TYPE resourceType)
{
    return resourceType switch
    {
        PIX_D3D12_RESOURCE_TYPE.PIX_D3D12_RESOURCE_COMMITTED => "Committed",
        PIX_D3D12_RESOURCE_TYPE.PIX_D3D12_RESOURCE_PLACED => "Placed",
        PIX_D3D12_RESOURCE_TYPE.PIX_D3D12_RESOURCE_RESERVED => "Reserved",
        _ => "Unknown"
    };
}

static string GetViewTypeName(PIX_RESOURCE_VIEW_TYPE viewType)
{
    return viewType switch
    {
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_CONSTANT_BUFFER_VIEW => "CBV",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_VERTEX_BUFFER_VIEW => "VBV",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_INDEX_BUFFER_VIEW => "IBV",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_STREAM_OUTPUT_VIEW => "SOV",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_SHADER_RESOURCE_VIEW => "SRV",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_RENDER_TARGET_VIEW => "RTV",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_UNORDERED_ACCESS_VIEW => "UAV",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_DEPTH_STENCIL_VIEW => "DSV",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_SHADING_RATE_IMAGE => "ShadingRateImage",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_SAMPLER => "Sampler",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_STATIC_SAMPLER => "StaticSampler",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_DESCRIPTOR_RANGE => "DescriptorRange",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_ROOT_CONSTANT => "RootConstant",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_BUFFER => "Buffer",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_TEXTURE => "Texture",
        PIX_RESOURCE_VIEW_TYPE.PIX_RESOURCE_GPU_DESCRIPTOR_HANDLE => "GpuDescriptorHandle",
        _ => "Unknown"
    };
}

static string GetWideString(Windows.Win32.Foundation.PCWSTR value, string defaultValue)
{
    string resolvedValue = value.ToString();
    return string.IsNullOrWhiteSpace(resolvedValue) ? defaultValue : resolvedValue;
}

static string GetAnsiString(Windows.Win32.Foundation.PCSTR value)
{
    return value.ToString() ?? string.Empty;
}

static void WriteDeveloperModeHelpIfNeeded(int hresult)
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

sealed class ResourceDetails
{
    public ResourceDetails(IPixD3D12Resource resource)
    {
        Resource = resource;
    }

    public IPixD3D12Resource Resource { get; }

    public List<string> EventViewDescriptions { get; } = new();
}

readonly record struct DrawCallSelection(
    ulong QueueIndex,
    uint EventIndex,
    string QueueName,
    string AdapterName,
    PIX_EVENT_INFO EventInfo);
