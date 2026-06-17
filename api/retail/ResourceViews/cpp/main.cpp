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
// Resource Views Sample (C++)
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

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "d3d12.h"

#include "PixApi.h"

namespace
{
    constexpr HRESULT E_PIX_DEVELOPER_MODE_NOT_ENABLED = 0x8abc0000;
    constexpr HRESULT E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE = 0x8abc0001;

    struct DrawCallSelection
    {
        UINT32 QueueIndex = 0;
        UINT32 EventIndex = 0;
        PIX_EVENT_INFO EventInfo = {};
        std::wstring QueueName;
        std::wstring AdapterName;
    };

    struct ResourceInfo
    {
        ComPtr<IPixD3D12Resource> Resource;
        std::vector<std::wstring> EventViewDescriptions;
    };

    template <typename TInterface>
    bool QueryInterfaceTo(IUnknown* source, ComPtr<TInterface>& destination)
    {
        destination.Reset();
        return source != nullptr && SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(destination.ReleaseAndGetAddressOf())));
    }

    bool PrintFailure(HRESULT hresult, std::wstring_view errorMessage)
    {
        if (SUCCEEDED(hresult))
        {
            return true;
        }

        if (hresult == E_PIX_DEVELOPER_MODE_NOT_ENABLED || hresult == E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE)
        {
            wprintf(L"ERROR: Windows Developer Mode is required for this operation.\n");
            wprintf(L"Enable it in Settings > Privacy & security > For developers,\n");
            wprintf(L"or run: reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock\" ");
            wprintf(L"/v AllowDevelopmentWithoutDevLicense /t REG_DWORD /d 1 /f\n");
        }

        wprintf(L"%ls\n", errorMessage.data());
        wprintf(L"HRESULT: 0x%08X\n", static_cast<unsigned int>(hresult));
        return false;
    }

    const wchar_t* GetWideStringOrDefault(LPCWSTR value, const wchar_t* defaultValue = L"(none)")
    {
        return (value != nullptr && value[0] != L'\0') ? value : defaultValue;
    }

    std::wstring GetAnsiStringOrDefault(LPCSTR value, std::wstring_view defaultValue = L"")
    {
        if (value == nullptr || value[0] == '\0')
        {
            return std::wstring(defaultValue);
        }

        std::wstring wideValue;
        wideValue.reserve(std::strlen(value));
        for (const char* current = value; *current != '\0'; ++current)
        {
            wideValue.push_back(static_cast<wchar_t>(*current));
        }

        return wideValue;
    }

    std::wstring ToLowercase(std::wstring value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
        return value;
    }

    bool ContainsCaseInsensitive(std::wstring_view value, std::wstring_view searchValue)
    {
        return ToLowercase(std::wstring(value)).find(ToLowercase(std::wstring(searchValue))) != std::wstring::npos;
    }

    std::wstring GetResourceTypeName(PIX_D3D12_RESOURCE_TYPE resourceType)
    {
        switch (resourceType)
        {
        case PIX_D3D12_RESOURCE_COMMITTED:
            return L"Committed";
        case PIX_D3D12_RESOURCE_PLACED:
            return L"Placed";
        case PIX_D3D12_RESOURCE_RESERVED:
            return L"Reserved";
        default:
            return L"Unknown";
        }
    }

    std::wstring GetResourceDimensionName(D3D12_RESOURCE_DIMENSION dimension)
    {
        switch (dimension)
        {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            return L"Buffer";
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            return L"Texture1D";
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            return L"Texture2D";
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            return L"Texture3D";
        default:
            return L"Unknown";
        }
    }

    std::wstring GetViewTypeName(PIX_RESOURCE_VIEW_TYPE viewType)
    {
        switch (viewType)
        {
        case PIX_RESOURCE_CONSTANT_BUFFER_VIEW:
            return L"CBV";
        case PIX_RESOURCE_VERTEX_BUFFER_VIEW:
            return L"VBV";
        case PIX_RESOURCE_INDEX_BUFFER_VIEW:
            return L"IBV";
        case PIX_RESOURCE_STREAM_OUTPUT_VIEW:
            return L"SOV";
        case PIX_RESOURCE_SHADER_RESOURCE_VIEW:
            return L"SRV";
        case PIX_RESOURCE_RENDER_TARGET_VIEW:
            return L"RTV";
        case PIX_RESOURCE_UNORDERED_ACCESS_VIEW:
            return L"UAV";
        case PIX_RESOURCE_DEPTH_STENCIL_VIEW:
            return L"DSV";
        case PIX_RESOURCE_SHADING_RATE_IMAGE:
            return L"ShadingRateImage";
        case PIX_RESOURCE_SAMPLER:
            return L"Sampler";
        case PIX_RESOURCE_STATIC_SAMPLER:
            return L"StaticSampler";
        case PIX_RESOURCE_DESCRIPTOR_RANGE:
            return L"DescriptorRange";
        case PIX_RESOURCE_ROOT_CONSTANT:
            return L"RootConstant";
        case PIX_RESOURCE_BUFFER:
            return L"Buffer";
        case PIX_RESOURCE_TEXTURE:
            return L"Texture";
        case PIX_RESOURCE_GPU_DESCRIPTOR_HANDLE:
            return L"GpuDescriptorHandle";
        default:
            return L"Unknown";
        }
    }

    std::wstring GetFormatName(DXGI_FORMAT format)
    {
        return L"DXGI_FORMAT(" + std::to_wstring(static_cast<INT>(format)) + L")";
    }

    bool IsDrawCallEvent(PIX_EVENT_INFO const& eventInfo)
    {
        const std::wstring eventName = GetAnsiStringOrDefault(eventInfo.Name);
        const std::wstring apiCallData = GetAnsiStringOrDefault(eventInfo.ApiCallData);
        return ContainsCaseInsensitive(eventName, L"draw") || ContainsCaseInsensitive(apiCallData, L"draw");
    }

    bool TryFindFirstDrawCall(IPixCollection* queues, DrawCallSelection* drawCallSelection)
    {
        for (UINT64 queueIndex = 0; queueIndex < queues->GetCount(); ++queueIndex)
        {
            ComPtr<IPixGpuCaptureQueueInfo> queueInfo;
            if (FAILED(queues->Get(queueIndex, IID_PPV_ARGS(queueInfo.ReleaseAndGetAddressOf()))))
            {
                continue;
            }

            if (queueInfo->GetType() != PIX_QUEUE_TYPE_GRAPHICS)
            {
                continue;
            }

            for (UINT32 eventIndex = 0; eventIndex < queueInfo->GetEventCount(); ++eventIndex)
            {
                PIX_EVENT_INFO eventInfo = {};
                queueInfo->GetEvent(eventIndex, &eventInfo);
                if (!IsDrawCallEvent(eventInfo))
                {
                    continue;
                }

                drawCallSelection->QueueIndex = static_cast<UINT32>(queueIndex);
                drawCallSelection->EventIndex = eventIndex;
                drawCallSelection->EventInfo = eventInfo;
                drawCallSelection->QueueName = GetWideStringOrDefault(queueInfo->GetName());
                drawCallSelection->AdapterName = GetWideStringOrDefault(queueInfo->GetAdapterName());
                return true;
            }
        }

        return false;
    }

    std::wstring DescribeResourceView(IPixResourceView* resourceView)
    {
        const PIX_RESOURCE_VIEW_TYPE viewType = resourceView->GetType();
        std::wstring description = GetViewTypeName(viewType);

        switch (viewType)
        {
        case PIX_RESOURCE_CONSTANT_BUFFER_VIEW:
        {
            ComPtr<IPixConstantBufferView> constantBufferView;
            if (QueryInterfaceTo(resourceView, constantBufferView))
            {
                const D3D12_CONSTANT_BUFFER_VIEW_DESC* constantBufferDesc = nullptr;
                if (SUCCEEDED(constantBufferView->GetDesc(&constantBufferDesc)) && constantBufferDesc != nullptr)
                {
                    description += L" SizeInBytes=" + std::to_wstring(constantBufferDesc->SizeInBytes);
                    description += L", Offset=" + std::to_wstring(constantBufferView->GetBufferLocationOffset());
                }
            }
            break;
        }

        case PIX_RESOURCE_VERTEX_BUFFER_VIEW:
        {
            ComPtr<IPixVertexBufferView> vertexBufferView;
            if (QueryInterfaceTo(resourceView, vertexBufferView))
            {
                const D3D12_VERTEX_BUFFER_VIEW* vertexBufferDesc = vertexBufferView->GetDesc();
                if (vertexBufferDesc != nullptr)
                {
                    description += L" StrideInBytes=" + std::to_wstring(vertexBufferDesc->StrideInBytes);
                    description += L", SizeInBytes=" + std::to_wstring(vertexBufferDesc->SizeInBytes);
                    description += L", Offset=" + std::to_wstring(vertexBufferView->GetBufferLocationOffset());
                }
            }
            break;
        }

        case PIX_RESOURCE_INDEX_BUFFER_VIEW:
        {
            ComPtr<IPixIndexBufferView> indexBufferView;
            if (QueryInterfaceTo(resourceView, indexBufferView))
            {
                const D3D12_INDEX_BUFFER_VIEW* indexBufferDesc = indexBufferView->GetDesc();
                if (indexBufferDesc != nullptr)
                {
                    description += L" Format=" + GetFormatName(indexBufferDesc->Format);
                    description += L", SizeInBytes=" + std::to_wstring(indexBufferDesc->SizeInBytes);
                    description += L", Offset=" + std::to_wstring(indexBufferView->GetBufferLocationOffset());
                }
            }
            break;
        }

        case PIX_RESOURCE_STREAM_OUTPUT_VIEW:
        {
            ComPtr<IPixStreamOutputView> streamOutputView;
            if (QueryInterfaceTo(resourceView, streamOutputView))
            {
                const D3D12_STREAM_OUTPUT_BUFFER_VIEW* streamOutputDesc = streamOutputView->GetDesc();
                if (streamOutputDesc != nullptr)
                {
                    description += L" SizeInBytes=" + std::to_wstring(streamOutputDesc->SizeInBytes);
                    description += L", Offset=" + std::to_wstring(streamOutputView->GetBufferLocationOffset());
                }
            }
            break;
        }

        case PIX_RESOURCE_SHADER_RESOURCE_VIEW:
        {
            ComPtr<IPixShaderResourceView> shaderResourceView;
            if (QueryInterfaceTo(resourceView, shaderResourceView))
            {
                const D3D12_SHADER_RESOURCE_VIEW_DESC* shaderResourceDesc = nullptr;
                if (SUCCEEDED(shaderResourceView->GetDesc(&shaderResourceDesc)) && shaderResourceDesc != nullptr)
                {
                    description += L" Format=" + GetFormatName(shaderResourceDesc->Format);
                    description += L", ViewDimension=" + std::to_wstring(static_cast<UINT32>(shaderResourceDesc->ViewDimension));
                }

                UINT64 locationOffset = 0;
                if (SUCCEEDED(shaderResourceView->GetLocationOffset(&locationOffset)))
                {
                    description += L", LocationOffset=" + std::to_wstring(locationOffset);
                }
            }
            break;
        }

        case PIX_RESOURCE_RENDER_TARGET_VIEW:
        {
            ComPtr<IPixRenderTargetView> renderTargetView;
            if (QueryInterfaceTo(resourceView, renderTargetView))
            {
                const D3D12_RENDER_TARGET_VIEW_DESC* renderTargetDesc = renderTargetView->GetDesc();
                if (renderTargetDesc != nullptr)
                {
                    description += L" Format=" + GetFormatName(renderTargetDesc->Format);
                    description += L", ViewDimension=" + std::to_wstring(static_cast<UINT32>(renderTargetDesc->ViewDimension));
                }
            }
            break;
        }

        case PIX_RESOURCE_UNORDERED_ACCESS_VIEW:
        {
            ComPtr<IPixUnorderedAccessView> unorderedAccessView;
            if (QueryInterfaceTo(resourceView, unorderedAccessView))
            {
                const D3D12_UNORDERED_ACCESS_VIEW_DESC* unorderedAccessDesc = nullptr;
                if (SUCCEEDED(unorderedAccessView->GetDesc(&unorderedAccessDesc)) && unorderedAccessDesc != nullptr)
                {
                    description += L" Format=" + GetFormatName(unorderedAccessDesc->Format);
                    description += L", ViewDimension=" + std::to_wstring(static_cast<UINT32>(unorderedAccessDesc->ViewDimension));
                }
            }
            break;
        }

        case PIX_RESOURCE_DEPTH_STENCIL_VIEW:
        {
            ComPtr<IPixDepthStencilView> depthStencilView;
            if (QueryInterfaceTo(resourceView, depthStencilView))
            {
                const D3D12_DEPTH_STENCIL_VIEW_DESC* depthStencilDesc = depthStencilView->GetDesc();
                if (depthStencilDesc != nullptr)
                {
                    description += L" Format=" + GetFormatName(depthStencilDesc->Format);
                    description += L", ViewDimension=" + std::to_wstring(static_cast<UINT32>(depthStencilDesc->ViewDimension));
                }
            }
            break;
        }

        case PIX_RESOURCE_BUFFER:
        {
            ComPtr<IPixBufferResourceView> bufferResourceView;
            if (QueryInterfaceTo(resourceView, bufferResourceView))
            {
                description += L" SizeInBytes=" + std::to_wstring(bufferResourceView->GetSizeInBytes());
                description += L", Offset=" + std::to_wstring(bufferResourceView->GetBufferLocationOffset());
            }
            break;
        }

        case PIX_RESOURCE_TEXTURE:
        {
            ComPtr<IPixTextureResourceView> textureResourceView;
            if (QueryInterfaceTo(resourceView, textureResourceView))
            {
                D3D12_BARRIER_SUBRESOURCE_RANGE subresourceRange = {};
                if (SUCCEEDED(textureResourceView->GetSubresourceRange(&subresourceRange)))
                {
                    description += L" FirstMip=" + std::to_wstring(subresourceRange.IndexOrFirstMipLevel);
                    description += L", NumMips=" + std::to_wstring(subresourceRange.NumMipLevels);
                    description += L", FirstArraySlice=" + std::to_wstring(subresourceRange.FirstArraySlice);
                    description += L", NumArraySlices=" + std::to_wstring(subresourceRange.NumArraySlices);
                }
            }
            break;
        }
        }

        return description;
    }

    bool TryAddResourceFromView(IPixResourceView* resourceView, std::unordered_map<UINT64, ResourceInfo>* resourcesById)
    {
        ComPtr<IPixD3D12ResourceView> d3d12ResourceView;
        if (!QueryInterfaceTo(resourceView, d3d12ResourceView))
        {
            return false;
        }

        ComPtr<IPixD3D12Resource> resource;
        if (FAILED(d3d12ResourceView->GetD3D12Resource(IID_PPV_ARGS(resource.ReleaseAndGetAddressOf()))))
        {
            return false;
        }

        const UINT64 apiObjectId = resource->GetApiObjectId();
        auto [iterator, inserted] = resourcesById->try_emplace(apiObjectId);
        if (inserted)
        {
            iterator->second.Resource = resource;
        }

        iterator->second.EventViewDescriptions.push_back(DescribeResourceView(resourceView));
        return true;
    }

    bool PrintAssociatedViewsForResource(IPixD3D12Resource* resource)
    {
        ComPtr<IPixResourceViewsForResource> resourceViews;
        HRESULT getViewsHr = resource->GetViews(IID_PPV_ARGS(resourceViews.ReleaseAndGetAddressOf()));
        if (FAILED(getViewsHr))
        {
            wprintf(L"    AssociatedViews: (unavailable)\n");
            return true;
        }

        wprintf(L"    AssociatedViews: %u\n", resourceViews->GetCount());
        for (UINT32 viewIndex = 0; viewIndex < resourceViews->GetCount(); ++viewIndex)
        {
            ComPtr<IPixResourceView> resourceView;
            if (!PrintFailure(
                resourceViews->GetView(viewIndex, IID_PPV_ARGS(resourceView.ReleaseAndGetAddressOf())),
                L"GetView for a resource failed"))
            {
                return false;
            }

            std::wstring viewDescription = DescribeResourceView(resourceView.Get());
            wprintf(L"      - %ls\n", viewDescription.c_str());
        }

        return true;
    }

    bool PrintAccessedResources(IPixResourceViewsAtEvent* resourceViewsAtEvent)
    {
        std::unordered_map<UINT64, ResourceInfo> resourcesById;

        wprintf(L"Accessed resource views at the selected draw: %u\n", resourceViewsAtEvent->GetCount());
        for (UINT32 viewIndex = 0; viewIndex < resourceViewsAtEvent->GetCount(); ++viewIndex)
        {
            ComPtr<IPixResourceView> resourceView;
            if (!PrintFailure(
                resourceViewsAtEvent->GetView(viewIndex, IID_PPV_ARGS(resourceView.ReleaseAndGetAddressOf())),
                L"GetView at event failed"))
            {
                return false;
            }

            TryAddResourceFromView(resourceView.Get(), &resourcesById);
        }

        wprintf(L"Unique accessed D3D12 resources: %zu\n", resourcesById.size());
        for (auto const& [apiObjectId, resourceInfo] : resourcesById)
        {
            const D3D12_RESOURCE_DESC2* resourceDesc = resourceInfo.Resource->GetResourceDesc();
            wprintf(L"\nResource 0x%llX\n", apiObjectId);
            wprintf(L"    Name: %ls\n", GetWideStringOrDefault(resourceInfo.Resource->GetName(), L"(unnamed)"));
            wprintf(L"    Type: %ls\n", GetResourceTypeName(resourceInfo.Resource->GetType()).c_str());

            if (resourceDesc != nullptr)
            {
                wprintf(L"    Dimension: %ls\n", GetResourceDimensionName(resourceDesc->Dimension).c_str());
                wprintf(L"    Size: %llu x %u x %u\n", resourceDesc->Width, resourceDesc->Height, resourceDesc->DepthOrArraySize);
                wprintf(L"    MipLevels: %u\n", resourceDesc->MipLevels);
                wprintf(L"    Format: %ls\n", GetFormatName(resourceDesc->Format).c_str());
            }

            if (!resourceInfo.EventViewDescriptions.empty())
            {
                wprintf(L"    EventViews:\n");
                for (std::wstring const& eventViewDescription : resourceInfo.EventViewDescriptions)
                {
                    wprintf(L"      - %ls\n", eventViewDescription.c_str());
                }
            }

            if (!PrintAssociatedViewsForResource(resourceInfo.Resource.Get()))
            {
                return false;
            }
        }

        return true;
    }
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2)
    {
        wprintf(L"Usage: ResourceViews.exe <path-to-gpu-capture-file>\n");
        return 1;
    }

    const std::filesystem::path captureFilePath = std::filesystem::absolute(argv[1]);
    if (!std::filesystem::exists(captureFilePath))
    {
        wprintf(L"GPU capture file not found: %ls\n", captureFilePath.c_str());
        return 1;
    }

    int exitCode = 1;
    ComPtr<IPixFactory> factory;
    ComPtr<IPixGpuCaptureDocument> captureDocument;
    ComPtr<IPixD3D12Resources> resources;
    ComPtr<IPixCollection> queues;
    DrawCallSelection drawCallSelection = {};
    ComPtr<IPixGpuCaptureAnalysis> analysis;
    PIX_CONNECTION_DESC_LOCAL localConnection = {};
    PIX_CONNECTION_DESC connectionDesc = {};
    ComPtr<IPixProgramState> programState;
    ComPtr<IPixGenericPipeline> genericPipeline;
    ComPtr<IPixResourceViewsAtEvent> resourceViewsAtEvent;
    bool analysisConnected = false;
    bool analysisStarted = false;

    do
    {
        if (!PrintFailure(PixCreateFactory(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())), L"PixCreateFactory failed"))
        {
            break;
        }

        if (!PrintFailure(
            factory->OpenGpuCaptureDocument(captureFilePath.c_str(), IID_PPV_ARGS(captureDocument.ReleaseAndGetAddressOf())),
            L"OpenGpuCaptureDocument failed"))
        {
            break;
        }

        if (!PrintFailure(
            captureDocument->GetD3D12Resources(IID_PPV_ARGS(resources.ReleaseAndGetAddressOf())),
            L"GetD3D12Resources failed"))
        {
            break;
        }

        wprintf(L"Capture path: %ls\n", captureFilePath.c_str());
        wprintf(L"D3D12 resources in capture: %u\n", resources->GetCount());

        if (!PrintFailure(
            captureDocument->GetQueues(IID_PPV_ARGS(queues.ReleaseAndGetAddressOf())),
            L"GetQueues failed"))
        {
            break;
        }

        if (!TryFindFirstDrawCall(queues.Get(), &drawCallSelection))
        {
            wprintf(L"No draw call event was found in the capture.\n");
            break;
        }

        wprintf(L"Selected draw call: QueueIndex=%u, EventIndex=%u\n", drawCallSelection.QueueIndex, drawCallSelection.EventIndex);
        wprintf(L"Queue: %ls\n", drawCallSelection.QueueName.c_str());
        wprintf(L"Adapter: %ls\n", drawCallSelection.AdapterName.c_str());
        wprintf(L"EventName: %ls\n", GetAnsiStringOrDefault(drawCallSelection.EventInfo.Name, L"(unnamed event)").c_str());

        if (!PrintFailure(
            captureDocument->GetAnalysis(IID_PPV_ARGS(analysis.ReleaseAndGetAddressOf())),
            L"GetAnalysis failed"))
        {
            break;
        }

        connectionDesc.Type = PIX_CONNECTION_TYPE_LOCAL;
        connectionDesc.pLocal = &localConnection;
        if (!PrintFailure(analysis->Connect(&connectionDesc, nullptr), L"Connect failed"))
        {
            break;
        }
        analysisConnected = true;

        if (!PrintFailure(analysis->StartAnalysis(nullptr, nullptr, nullptr), L"StartAnalysis failed"))
        {
            break;
        }
        analysisStarted = true;

        if (!PrintFailure(
            captureDocument->GetProgramState(&drawCallSelection.EventInfo, IID_PPV_ARGS(programState.ReleaseAndGetAddressOf())),
            L"GetProgramState failed"))
        {
            break;
        }

        if (!PrintFailure(
            programState->GetGpuProgram(IID_PPV_ARGS(genericPipeline.ReleaseAndGetAddressOf())),
            L"GetGpuProgram failed"))
        {
            break;
        }

        if (!PrintFailure(
            genericPipeline->GetResourceViews(IID_PPV_ARGS(resourceViewsAtEvent.ReleaseAndGetAddressOf())),
            L"GetResourceViews failed"))
        {
            break;
        }

        if (!PrintFailure(analysis->GatherAccessedResources(), L"GatherAccessedResources failed"))
        {
            break;
        }

        if (!PrintFailure(analysis->GetAccessedResources(resourceViewsAtEvent.Get()), L"GetAccessedResources failed"))
        {
            break;
        }

        if (!PrintAccessedResources(resourceViewsAtEvent.Get()))
        {
            break;
        }

        wprintf(L"\nResource views sample completed successfully.\n");
        exitCode = 0;
    } while (false);

    if (analysis != nullptr && analysisStarted)
    {
        HRESULT stopAnalysisHr = analysis->StopAnalysis();
        if (FAILED(stopAnalysisHr))
        {
            PrintFailure(stopAnalysisHr, L"StopAnalysis failed during cleanup");
        }
    }

    if (analysis != nullptr && analysisConnected)
    {
        HRESULT disconnectHr = analysis->Disconnect();
        if (FAILED(disconnectHr))
        {
            PrintFailure(disconnectHr, L"Disconnect failed during cleanup");
        }
    }

    return exitCode;
}
