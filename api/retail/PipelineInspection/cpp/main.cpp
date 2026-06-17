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
// Pipeline Inspection Sample (C++)
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
// Usage: PipelineInspection.exe <path-to-gpu-capture-file>
//

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include "d3d12.h"
#include "PixApi.h"

#include <cwchar>
#include <filesystem>

namespace
{
    constexpr HRESULT E_PIX_DEVELOPER_MODE_NOT_ENABLED = 0x8abc0000;
    constexpr HRESULT E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE = 0x8abc0001;

    void PrintDeveloperModeHelpIfNeeded(HRESULT hr)
    {
        if (hr == E_PIX_DEVELOPER_MODE_NOT_ENABLED || hr == E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE)
        {
            wprintf(L"ERROR: Windows Developer Mode is required for this operation.\n");
            wprintf(L"Enable it in Settings > Privacy & security > For developers,\n");
            wprintf(L"or run: reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock\" /v AllowDevelopmentWithoutDevLicense /t REG_DWORD /d 1 /f\n");
        }
    }

    bool CheckHr(HRESULT hr, LPCWSTR operationName)
    {
        if (FAILED(hr))
        {
            PrintDeveloperModeHelpIfNeeded(hr);
            wprintf(L"%ls failed.\n", operationName);
            wprintf(L"Error code: 0x%08X\n", static_cast<unsigned int>(hr));
            return false;
        }

        return true;
    }

    void PrintUsage()
    {
        wprintf(L"Usage: PipelineInspection.exe <path-to-gpu-capture-file>\n");
    }

    LPCWSTR GetWideStringOrDefault(LPCWSTR value, LPCWSTR defaultValue = L"(none)")
    {
        return (value && *value) ? value : defaultValue;
    }

    LPCSTR GetAnsiStringOrDefault(LPCSTR value, LPCSTR defaultValue = "(none)")
    {
        return (value && *value) ? value : defaultValue;
    }

    LPCWSTR GetQueueTypeName(PIX_QUEUE_TYPE queueType)
    {
        switch (queueType)
        {
        case PIX_QUEUE_TYPE_GRAPHICS:
            return L"Graphics";
        case PIX_QUEUE_TYPE_COMPUTE:
            return L"Compute";
        case PIX_QUEUE_TYPE_COPY:
            return L"Copy";
        default:
            return L"Unknown";
        }
    }

    LPCWSTR GetPipelineTypeName(PIX_GENERIC_PIPELINE_TYPE pipelineType)
    {
        switch (pipelineType)
        {
        case PIX_GENERIC_PIPELINE_TYPE_COMPUTE:
            return L"Compute";
        case PIX_GENERIC_PIPELINE_TYPE_GRAPHICS:
            return L"Graphics";
        case PIX_GENERIC_PIPELINE_TYPE_MESH:
            return L"Mesh";
        default:
            return L"Unknown";
        }
    }

    LPCWSTR GetSubobjectTypeName(D3D12_STATE_SUBOBJECT_TYPE subobjectType)
    {
        switch (subobjectType)
        {
        case D3D12_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
            return L"STREAM_OUTPUT";
        case D3D12_STATE_SUBOBJECT_TYPE_BLEND:
            return L"BLEND";
        case D3D12_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
            return L"SAMPLE_MASK";
        case D3D12_STATE_SUBOBJECT_TYPE_RASTERIZER:
            return L"RASTERIZER";
        case D3D12_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
            return L"DEPTH_STENCIL";
        case D3D12_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
            return L"INPUT_LAYOUT";
        case D3D12_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
            return L"IB_STRIP_CUT_VALUE";
        case D3D12_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
            return L"PRIMITIVE_TOPOLOGY";
        case D3D12_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
            return L"RENDER_TARGET_FORMATS";
        case D3D12_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
            return L"DEPTH_STENCIL_FORMAT";
        case D3D12_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
            return L"SAMPLE_DESC";
        case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
            return L"NODE_MASK";
        case D3D12_STATE_SUBOBJECT_TYPE_FLAGS:
            return L"FLAGS";
        default:
            break;
        }

#ifdef D3D12_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1
        if (subobjectType == D3D12_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1)
        {
            return L"DEPTH_STENCIL1";
        }
#endif

#ifdef D3D12_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING
        if (subobjectType == D3D12_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING)
        {
            return L"VIEW_INSTANCING";
        }
#endif

#ifdef D3D12_STATE_SUBOBJECT_TYPE_GENERIC_PROGRAM
        if (subobjectType == D3D12_STATE_SUBOBJECT_TYPE_GENERIC_PROGRAM)
        {
            return L"GENERIC_PROGRAM";
        }
#endif

#ifdef D3D12_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2
        if (subobjectType == D3D12_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2)
        {
            return L"DEPTH_STENCIL2";
        }
#endif

        return L"UNKNOWN";
    }

    void PrintDescriptorRanges(const D3D12_DESCRIPTOR_RANGE* descriptorRanges, UINT32 descriptorRangeCount)
    {
        for (UINT32 rangeIndex = 0; rangeIndex < descriptorRangeCount; ++rangeIndex)
        {
            const D3D12_DESCRIPTOR_RANGE& descriptorRange = descriptorRanges[rangeIndex];
            wprintf(
                L"        Range %u: Type=%u, Register=%u, Space=%u, Descriptors=%u\n",
                rangeIndex,
                static_cast<unsigned int>(descriptorRange.RangeType),
                descriptorRange.BaseShaderRegister,
                descriptorRange.RegisterSpace,
                descriptorRange.NumDescriptors);
        }
    }

    void PrintDescriptorRanges(const D3D12_DESCRIPTOR_RANGE1* descriptorRanges, UINT32 descriptorRangeCount)
    {
        for (UINT32 rangeIndex = 0; rangeIndex < descriptorRangeCount; ++rangeIndex)
        {
            const D3D12_DESCRIPTOR_RANGE1& descriptorRange = descriptorRanges[rangeIndex];
            wprintf(
                L"        Range %u: Type=%u, Register=%u, Space=%u, Descriptors=%u\n",
                rangeIndex,
                static_cast<unsigned int>(descriptorRange.RangeType),
                descriptorRange.BaseShaderRegister,
                descriptorRange.RegisterSpace,
                descriptorRange.NumDescriptors);
        }
    }

    void PrintRootParameters(const D3D12_ROOT_PARAMETER* rootParameters, UINT32 rootParameterCount)
    {
        wprintf(L"    Root parameters: %u\n", rootParameterCount);

        for (UINT32 parameterIndex = 0; parameterIndex < rootParameterCount; ++parameterIndex)
        {
            const D3D12_ROOT_PARAMETER& rootParameter = rootParameters[parameterIndex];
            wprintf(
                L"      [%u] Type=%u, Visibility=%u\n",
                parameterIndex,
                static_cast<unsigned int>(rootParameter.ParameterType),
                static_cast<unsigned int>(rootParameter.ShaderVisibility));

            switch (rootParameter.ParameterType)
            {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                wprintf(L"        Descriptor ranges: %u\n", rootParameter.DescriptorTable.NumDescriptorRanges);
                PrintDescriptorRanges(
                    rootParameter.DescriptorTable.pDescriptorRanges,
                    rootParameter.DescriptorTable.NumDescriptorRanges);
                break;
            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                wprintf(
                    L"        Register=%u, Space=%u, Values=%u\n",
                    rootParameter.Constants.ShaderRegister,
                    rootParameter.Constants.RegisterSpace,
                    rootParameter.Constants.Num32BitValues);
                break;
            case D3D12_ROOT_PARAMETER_TYPE_CBV:
            case D3D12_ROOT_PARAMETER_TYPE_SRV:
            case D3D12_ROOT_PARAMETER_TYPE_UAV:
                wprintf(
                    L"        Register=%u, Space=%u\n",
                    rootParameter.Descriptor.ShaderRegister,
                    rootParameter.Descriptor.RegisterSpace);
                break;
            default:
                break;
            }
        }
    }

    void PrintRootParameters(const D3D12_ROOT_PARAMETER1* rootParameters, UINT32 rootParameterCount)
    {
        wprintf(L"    Root parameters: %u\n", rootParameterCount);

        for (UINT32 parameterIndex = 0; parameterIndex < rootParameterCount; ++parameterIndex)
        {
            const D3D12_ROOT_PARAMETER1& rootParameter = rootParameters[parameterIndex];
            wprintf(
                L"      [%u] Type=%u, Visibility=%u\n",
                parameterIndex,
                static_cast<unsigned int>(rootParameter.ParameterType),
                static_cast<unsigned int>(rootParameter.ShaderVisibility));

            switch (rootParameter.ParameterType)
            {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                wprintf(L"        Descriptor ranges: %u\n", rootParameter.DescriptorTable.NumDescriptorRanges);
                PrintDescriptorRanges(
                    rootParameter.DescriptorTable.pDescriptorRanges,
                    rootParameter.DescriptorTable.NumDescriptorRanges);
                break;
            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                wprintf(
                    L"        Register=%u, Space=%u, Values=%u\n",
                    rootParameter.Constants.ShaderRegister,
                    rootParameter.Constants.RegisterSpace,
                    rootParameter.Constants.Num32BitValues);
                break;
            case D3D12_ROOT_PARAMETER_TYPE_CBV:
            case D3D12_ROOT_PARAMETER_TYPE_SRV:
            case D3D12_ROOT_PARAMETER_TYPE_UAV:
                wprintf(
                    L"        Register=%u, Space=%u\n",
                    rootParameter.Descriptor.ShaderRegister,
                    rootParameter.Descriptor.RegisterSpace);
                break;
            default:
                break;
            }
        }
    }

    void PrintRootSignature(IPixRootSignature* rootSignature)
    {
        if (rootSignature == nullptr)
        {
            wprintf(L"  Global root signature: (none)\n");
            return;
        }

        wprintf(L"  Global root signature: %ls\n", GetWideStringOrDefault(rootSignature->GetName(), L"(unnamed root signature)"));
        wprintf(L"    ApiObjectId: %llu\n", static_cast<unsigned long long>(rootSignature->GetApiObjectId()));

        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* versionedDesc = rootSignature->GetDesc();
        if (versionedDesc == nullptr)
        {
            wprintf(L"    Root signature description unavailable.\n");
            return;
        }

        switch (versionedDesc->Version)
        {
        case D3D_ROOT_SIGNATURE_VERSION_1_0:
            wprintf(L"    Version: 1.0\n");
            PrintRootParameters(
                versionedDesc->Desc_1_0.pParameters,
                versionedDesc->Desc_1_0.NumParameters);
            wprintf(L"    Static samplers: %u\n", versionedDesc->Desc_1_0.NumStaticSamplers);
            break;
        case D3D_ROOT_SIGNATURE_VERSION_1_1:
            wprintf(L"    Version: 1.1\n");
            PrintRootParameters(
                versionedDesc->Desc_1_1.pParameters,
                versionedDesc->Desc_1_1.NumParameters);
            wprintf(L"    Static samplers: %u\n", versionedDesc->Desc_1_1.NumStaticSamplers);
            break;
#ifdef D3D_ROOT_SIGNATURE_VERSION_1_2
        case D3D_ROOT_SIGNATURE_VERSION_1_2:
            wprintf(L"    Version: 1.2\n");
            PrintRootParameters(
                versionedDesc->Desc_1_2.pParameters,
                versionedDesc->Desc_1_2.NumParameters);
            wprintf(L"    Static samplers: %u\n", versionedDesc->Desc_1_2.NumStaticSamplers);
            break;
#endif
        default:
            wprintf(L"    Version: %u\n", static_cast<unsigned int>(versionedDesc->Version));
            break;
        }
    }

    void PrintPipelineState(IPixPipelineState* pipelineState)
    {
        const UINT64 subobjectCount = pipelineState->GetSubobjectCount();
        wprintf(L"  Pipeline state subobjects: %llu\n", static_cast<unsigned long long>(subobjectCount));

        for (UINT64 subobjectIndex = 0; subobjectIndex < subobjectCount; ++subobjectIndex)
        {
            D3D12_STATE_SUBOBJECT subobject = {};
            if (SUCCEEDED(pipelineState->GetSubobject(subobjectIndex, &subobject)))
            {
                wprintf(
                    L"    [%llu] %ls\n",
                    static_cast<unsigned long long>(subobjectIndex),
                    GetSubobjectTypeName(subobject.Type));
            }
            else
            {
                wprintf(L"    [%llu] Failed to retrieve subobject\n", static_cast<unsigned long long>(subobjectIndex));
            }
        }
    }
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    const std::filesystem::path captureFilePath = argv[1];
    if (!std::filesystem::exists(captureFilePath))
    {
        wprintf(L"GPU capture file not found: %ls\n", captureFilePath.c_str());
        return 1;
    }

    ComPtr<IPixFactory> factory;
    ComPtr<IPixGpuCaptureDocument> captureDocument;
    ComPtr<IPixGpuCaptureAnalysis> analysis;
    ComPtr<IPixCollection> queues;
    UINT64 inspectedEventCount = 0;
    bool analysisConnected = false;
    bool analysisStarted = false;

    // Cap inspection so the sample finishes in seconds even on very large
    // captures (e.g. 37 MB VRS captures with hundreds of thousands of events).
    // Declared here (rather than near its first use below) so the
    // `goto Cleanup` statements between this point and the loop do not
    // jump over a variable initialization (C2362).
    constexpr UINT64 maxEventsToInspect = 16;
    int returnCode = 1;

    // Step 1: Create the PIX factory.
    if (!CheckHr(PixCreateFactory(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())), L"PixCreateFactory"))
    {
        return 1;
    }

    // Step 2: Open the GPU capture document.
    if (!CheckHr(
        factory->OpenGpuCaptureDocument(
            captureFilePath.c_str(),
            IID_PPV_ARGS(captureDocument.ReleaseAndGetAddressOf())),
        L"OpenGpuCaptureDocument"))
    {
        return 1;
    }

    // Step 3: Get the analysis interface.
    if (!CheckHr(captureDocument->GetAnalysis(IID_PPV_ARGS(analysis.ReleaseAndGetAddressOf())), L"GetAnalysis"))
    {
        return 1;
    }

    // Step 4: Connect analysis to the local GPU.
    PIX_CONNECTION_DESC_LOCAL localConnection = {};
    PIX_CONNECTION_DESC analysisConnectionDesc = {};
    analysisConnectionDesc.Type = PIX_CONNECTION_TYPE_LOCAL;
    analysisConnectionDesc.pLocal = &localConnection;

    if (!CheckHr(analysis->Connect(&analysisConnectionDesc, nullptr), L"Connect"))
    {
        goto Cleanup;
    }
    analysisConnected = true;

    // Step 5: Start analysis (Windows Developer Mode is required).
    if (!CheckHr(analysis->StartAnalysis(nullptr, nullptr, nullptr), L"StartAnalysis"))
    {
        goto Cleanup;
    }
    analysisStarted = true;

    // Step 6: Get the queue list from the capture.
    if (!CheckHr(captureDocument->GetQueues(IID_PPV_ARGS(queues.ReleaseAndGetAddressOf())), L"GetQueues"))
    {
        goto Cleanup;
    }

    // Steps 7-8: Inspect pipeline state and root signature information for each event.
    // (maxEventsToInspect is declared at the top of the function so the
    // `goto Cleanup` jumps above don't skip its initialization — C2362.)
    for (UINT64 queueIndex = 0; queueIndex < queues->GetCount() && inspectedEventCount < maxEventsToInspect; ++queueIndex)
    {
        ComPtr<IPixGpuCaptureQueueInfo> queueInfo;
        if (FAILED(queues->Get(queueIndex, IID_PPV_ARGS(queueInfo.ReleaseAndGetAddressOf()))))
        {
            continue;
        }

        for (UINT32 eventIndex = 0; eventIndex < queueInfo->GetEventCount() && inspectedEventCount < maxEventsToInspect; ++eventIndex)
        {
            PIX_EVENT_INFO eventInfo = {};
            if (FAILED(queueInfo->GetEvent(eventIndex, &eventInfo)))
            {
                continue;
            }

            ComPtr<IPixProgramState> programState;
            if (FAILED(captureDocument->GetProgramState(&eventInfo, IID_PPV_ARGS(programState.ReleaseAndGetAddressOf()))))
            {
                continue;
            }

            D3D12_PROGRAM_TYPE programType = {};
            if (FAILED(programState->GetGpuProgramType(&programType)) || programType != D3D12_PROGRAM_TYPE_GENERIC_PIPELINE)
            {
                continue;
            }

            ComPtr<IPixGenericPipeline> genericPipeline;
            if (FAILED(programState->GetGpuProgram(IID_PPV_ARGS(genericPipeline.ReleaseAndGetAddressOf()))))
            {
                continue;
            }

            // GetPipelineType can fail for events that report a generic-pipeline
            // program type but whose underlying pipeline state is not yet bound
            // (seen in raytracing-only captures). Validate up-front and skip
            // such events instead of partially printing them.
            PIX_GENERIC_PIPELINE_TYPE pipelineType = {};
            if (FAILED(genericPipeline->GetPipelineType(&pipelineType)))
            {
                continue;
            }

            ++inspectedEventCount;
            wprintf(
                L"Queue %u (%ls, %ls)\n",
                queueInfo->GetId(),
                GetWideStringOrDefault(queueInfo->GetName(), L"(unnamed queue)"),
                GetQueueTypeName(queueInfo->GetType()));
            wprintf(L"  Event %u: %S\n", eventIndex, GetAnsiStringOrDefault(eventInfo.Name));
            wprintf(L"  Pipeline type: %ls\n", GetPipelineTypeName(pipelineType));

            ComPtr<IPixPipelineState> pipelineState;
            if (SUCCEEDED(genericPipeline->GetPipelineState(IID_PPV_ARGS(pipelineState.ReleaseAndGetAddressOf()))))
            {
                PrintPipelineState(pipelineState.Get());
            }
            else
            {
                wprintf(L"  Pipeline state unavailable.\n");
            }

            ComPtr<IPixRootSignature> rootSignature;
            if (SUCCEEDED(genericPipeline->GetGlobalRootSignature(IID_PPV_ARGS(rootSignature.ReleaseAndGetAddressOf()))))
            {
                PrintRootSignature(rootSignature.Get());
            }
            else
            {
                wprintf(L"  Global root signature unavailable.\n");
            }

            wprintf(L"\n");
        }
    }

    if (inspectedEventCount == 0)
    {
        wprintf(L"No draw or dispatch events with generic pipeline state were found.\n");
    }
    else if (inspectedEventCount >= maxEventsToInspect)
    {
        wprintf(L"(Stopped after inspecting %llu events.)\n", maxEventsToInspect);
    }

    returnCode = 0;

Cleanup:
    // Step 9: Stop analysis and disconnect.
    if (analysis != nullptr)
    {
        if (analysisStarted)
        {
            HRESULT cleanupHr = analysis->StopAnalysis();
            if (FAILED(cleanupHr))
            {
                wprintf(L"Warning: StopAnalysis failed during cleanup (0x%08X).\n", static_cast<unsigned int>(cleanupHr));
            }
        }

        if (analysisConnected)
        {
            HRESULT cleanupHr = analysis->Disconnect();
            if (FAILED(cleanupHr))
            {
                wprintf(L"Warning: Disconnect failed during cleanup (0x%08X).\n", static_cast<unsigned int>(cleanupHr));
            }
        }
    }

    return returnCode;
}
