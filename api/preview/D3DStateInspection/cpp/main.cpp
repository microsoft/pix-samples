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
// Program State Inspection Sample (C++)
//
// Opens a .wpix GPU capture, starts analysis, finds the first
// Draw / Dispatch / DispatchRays / DispatchMesh event, and prints the
// program / pipeline state at that event:
//   - Program type (Graphics, Compute, Mesh, Raytracing, etc.)
//   - Global root signature (name, ApiObjectId)
//   - Pipeline state subobjects (count + per-subobject types)
//   - Shaders bound to the program (per-stage IDs)
//
// Uses IPixGpuCaptureDocument::GetProgramState(EVENT_INFO, ...) -- the
// public, retail-shippable entry point that returns IPixProgramState ->
// IPixGpuProgram / IPixGenericPipeline (root signature, pipeline state,
// shaders). This works against any GPU capture that contains at least
// one supported program-driven event, including non-graphics events
// (Dispatch/DispatchRays/DispatchMesh).
//
// Runtime D3D state (viewports, scissor rects, vertex/index buffers,
// root parameter values bound at the time of the draw) is not part of
// program/pipeline state and lives on a separate IPixD3DState API path
// reachable from postmortem dumps; it is intentionally outside this
// sample's scope.
//

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "PixApi.h"
#include "PixApiExperimental.h"

constexpr HRESULT E_PIX_DEVELOPER_MODE_NOT_ENABLED = 0x8abc0000;
constexpr HRESULT E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE = 0x8abc0001;

struct SelectedEvent
{
    UINT QueueIndex = 0;
    UINT EventIndex = 0;
    PIX_EVENT_INFO EventInfo = {};
};

void CheckHr(HRESULT hr, std::wstring_view errorMessage)
{
    if (FAILED(hr))
    {
        if (hr == E_PIX_DEVELOPER_MODE_NOT_ENABLED || hr == E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE)
        {
            std::wcout << L"ERROR: Windows Developer Mode is required for this operation." << std::endl;
            std::wcout << L"Enable it in Settings > Privacy & security > For developers." << std::endl;
        }

        std::wcout << errorMessage << std::endl;
        std::wcout << L"Error code: 0x" << std::hex << hr << std::endl;
        // Throw instead of exit() so the caller's StopAnalysis cleanup
        // (gated by an RAII guard in wmain) runs before we unwind.
        throw HRESULT_FROM_WIN32(hr);
    }
}

wchar_t const* ProgramTypeToString(D3D12_PROGRAM_TYPE programType)
{
    switch (programType)
    {
    case D3D12_PROGRAM_TYPE_GENERIC_PIPELINE:        return L"GenericPipeline";
    case D3D12_PROGRAM_TYPE_RAYTRACING_PIPELINE:     return L"RaytracingPipeline";
    case D3D12_PROGRAM_TYPE_WORK_GRAPH:              return L"WorkGraph";
    case D3D12_PROGRAM_TYPE_MLIR_PROGRAM:            return L"MlirProgram";
    default:                                         return L"Unknown";
    }
}

wchar_t const* SubobjectTypeToString(D3D12_STATE_SUBOBJECT_TYPE type)
{
    switch (type)
    {
    case D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG:                return L"STATE_OBJECT_CONFIG";
    case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:              return L"GLOBAL_ROOT_SIGNATURE";
    case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:               return L"LOCAL_ROOT_SIGNATURE";
    case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:                          return L"NODE_MASK";
    case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:                       return L"DXIL_LIBRARY";
    case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:                return L"EXISTING_COLLECTION";
    case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:   return L"SUBOBJECT_TO_EXPORTS_ASSOCIATION";
    case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION: return L"DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION";
    case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:           return L"RAYTRACING_SHADER_CONFIG";
    case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:         return L"RAYTRACING_PIPELINE_CONFIG";
    case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:                          return L"HIT_GROUP";
    case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1:        return L"RAYTRACING_PIPELINE_CONFIG1";
    case D3D12_STATE_SUBOBJECT_TYPE_WORK_GRAPH:                         return L"WORK_GRAPH";
    case D3D12_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:                      return L"STREAM_OUTPUT";
    case D3D12_STATE_SUBOBJECT_TYPE_BLEND:                              return L"BLEND";
    case D3D12_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:                        return L"SAMPLE_MASK";
    case D3D12_STATE_SUBOBJECT_TYPE_RASTERIZER:                         return L"RASTERIZER";
    case D3D12_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:                      return L"DEPTH_STENCIL";
    case D3D12_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:                       return L"INPUT_LAYOUT";
    case D3D12_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:                 return L"IB_STRIP_CUT_VALUE";
    case D3D12_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:                 return L"PRIMITIVE_TOPOLOGY";
    case D3D12_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:              return L"RENDER_TARGET_FORMATS";
    case D3D12_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:               return L"DEPTH_STENCIL_FORMAT";
    case D3D12_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:                        return L"SAMPLE_DESC";
    case D3D12_STATE_SUBOBJECT_TYPE_FLAGS:                              return L"FLAGS";
    case D3D12_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:                     return L"DEPTH_STENCIL1";
    case D3D12_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:                    return L"VIEW_INSTANCING";
    case D3D12_STATE_SUBOBJECT_TYPE_GENERIC_PROGRAM:                    return L"GENERIC_PROGRAM";
    case D3D12_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2:                     return L"DEPTH_STENCIL2";
    case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_SERIALIZED_ROOT_SIGNATURE:   return L"GLOBAL_SERIALIZED_ROOT_SIGNATURE";
    case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_SERIALIZED_ROOT_SIGNATURE:    return L"LOCAL_SERIALIZED_ROOT_SIGNATURE";
    default:                                                            return L"<other>";
    }
}

ComPtr<IPixGpuCaptureDocument> OpenCapture(std::wstring const& capturePath)
{
    ComPtr<IPixFactory> factory;
    CheckHr(PixCreateFactory(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())), L"PixCreateFactory failed");

    ComPtr<IPixGpuCaptureDocument> captureDocument;
    CheckHr(
        factory->OpenGpuCaptureDocument(
            capturePath.c_str(),
            IID_PPV_ARGS(captureDocument.ReleaseAndGetAddressOf())),
        L"OpenGpuCaptureDocument failed");

    return captureDocument;
}

void StartAnalysis(ComPtr<IPixGpuCaptureDocument> const& captureDocument)
{
    ComPtr<IPixGpuCaptureAnalysisExperimental> analysis;
    CheckHr(captureDocument->GetAnalysis(IID_PPV_ARGS(analysis.ReleaseAndGetAddressOf())), L"GetAnalysis failed");

    PIX_CONNECTION_DESC_LOCAL local = {};
    PIX_CONNECTION_DESC analysisConnectionDesc = {};
    analysisConnectionDesc.Type = PIX_CONNECTION_TYPE_LOCAL;
    analysisConnectionDesc.pLocal = &local;

    CheckHr(analysis->Connect(&analysisConnectionDesc, nullptr), L"Analysis Connect failed");
    CheckHr(analysis->StartAnalysis(nullptr, nullptr, nullptr), L"StartAnalysis failed");
}

void StopAnalysis(ComPtr<IPixGpuCaptureDocument> const& captureDocument)
{
    ComPtr<IPixGpuCaptureAnalysisExperimental> analysis;
    if (SUCCEEDED(captureDocument->GetAnalysis(IID_PPV_ARGS(analysis.ReleaseAndGetAddressOf()))))
    {
        analysis->StopAnalysis();
        analysis->Disconnect();
    }
}

std::optional<SelectedEvent> FindFirstGpuProgramEvent(ComPtr<IPixGpuCaptureDocument> const& captureDocument)
{
    ComPtr<IPixCollection> queues;
    CheckHr(captureDocument->GetQueues(IID_PPV_ARGS(queues.ReleaseAndGetAddressOf())), L"GetQueues failed");

    // Match any event the program-state API supports: Draw, Dispatch (compute),
    // DispatchRays (raytracing), DispatchMesh (mesh shader). DispatchGraph
    // (work graphs) also qualifies. Per IPixProgramState's contract, the API
    // returns failure for anything outside this set.
    static constexpr std::string_view kProgramEventPrefixes[] = {
        "Draw", "Dispatch"
    };

    for (UINT queueIndex = 0; queueIndex < queues->GetCount(); queueIndex++)
    {
        ComPtr<IPixGpuCaptureQueueInfo> queueInfo;
        CheckHr(queues->Get(queueIndex, IID_PPV_ARGS(queueInfo.ReleaseAndGetAddressOf())), L"Get<IPixGpuCaptureQueueInfo> failed");

        if (queueInfo->GetType() != PIX_QUEUE_TYPE_GRAPHICS && queueInfo->GetType() != PIX_QUEUE_TYPE_COMPUTE)
        {
            continue;
        }

        for (UINT eventIndex = 0; eventIndex < queueInfo->GetEventCount(); eventIndex++)
        {
            PIX_EVENT_INFO eventInfo = {};
            queueInfo->GetEvent(eventIndex, &eventInfo);

            std::string eventName = eventInfo.Name == nullptr ? std::string() : std::string(eventInfo.Name);
            for (auto prefix : kProgramEventPrefixes)
            {
                if (eventName.compare(0, prefix.size(), prefix) == 0)
                {
                    std::cout << "Found program event \"" << eventName << "\" @QueueIndex " << queueIndex
                              << ", EventIndex " << eventIndex << std::endl;
                    return SelectedEvent{ queueIndex, eventIndex, eventInfo };
                }
            }
        }
    }

    return std::nullopt;
}

void PrintProgramType(IPixProgramState* programState)
{
    D3D12_PROGRAM_TYPE programType = {};
    if (FAILED(programState->GetGpuProgramType(&programType)))
    {
        std::wcout << L"Program type: (unavailable)" << std::endl;
        return;
    }
    std::wcout << L"Program type: " << ProgramTypeToString(programType)
               << L" (" << static_cast<UINT32>(programType) << L")" << std::endl;
}

void PrintGlobalRootSignature(IPixGpuProgram* gpuProgram)
{
    ComPtr<IPixRootSignature> rootSignature;
    if (FAILED(gpuProgram->GetGlobalRootSignature(IID_PPV_ARGS(rootSignature.ReleaseAndGetAddressOf())))
        || rootSignature == nullptr)
    {
        std::wcout << L"Global root signature: (not bound)" << std::endl;
        return;
    }

    LPCWSTR name = rootSignature->GetName();
    PIX_API_OBJECT_ID apiObjectId = rootSignature->GetApiObjectId();

    std::wcout << L"Global root signature: "
               << (name != nullptr && name[0] != L'\0' ? name : L"(unnamed)") << std::endl;
    std::wcout << L"  ApiObjectId: " << apiObjectId << std::endl;
}

void PrintPipelineSubobjects(IPixGenericPipeline* pipeline)
{
    ComPtr<IPixPipelineState> pipelineState;
    if (FAILED(pipeline->GetPipelineState(IID_PPV_ARGS(pipelineState.ReleaseAndGetAddressOf())))
        || pipelineState == nullptr)
    {
        std::wcout << L"Pipeline state: (not available)" << std::endl;
        return;
    }

    UINT64 subobjectCount = pipelineState->GetSubobjectCount();
    std::wcout << L"Pipeline state subobjects (" << subobjectCount << L"):" << std::endl;

    for (UINT64 i = 0; i < subobjectCount; ++i)
    {
        D3D12_STATE_SUBOBJECT subobject = {};
        if (FAILED(pipelineState->GetSubobject(i, &subobject)))
        {
            std::wcout << L"  [" << i << L"] <retrieve failed>" << std::endl;
            continue;
        }
        std::wcout << L"  [" << i << L"] " << SubobjectTypeToString(subobject.Type)
                   << L" (" << static_cast<UINT32>(subobject.Type) << L")" << std::endl;
    }
}

void PrintShaders(IPixGpuProgram* gpuProgram)
{
    ComPtr<IPixCollection> shaders;
    if (FAILED(gpuProgram->GetShaders(IID_PPV_ARGS(shaders.ReleaseAndGetAddressOf())))
        || shaders == nullptr)
    {
        std::wcout << L"Shaders: (not available)" << std::endl;
        return;
    }

    UINT64 shaderCount = shaders->GetCount();
    std::wcout << L"Shaders (" << shaderCount << L"):" << std::endl;

    for (UINT64 i = 0; i < shaderCount; ++i)
    {
        ComPtr<IPixShader> shader;
        if (FAILED(shaders->Get(i, IID_PPV_ARGS(shader.ReleaseAndGetAddressOf()))) || shader == nullptr)
        {
            std::wcout << L"  [" << i << L"] <get failed>" << std::endl;
            continue;
        }

        PIX_SHADER_ID shaderId = shader->GetId();
        UINT32 hashSize = shader->GetHashSizeBytes();
        std::wcout << L"  [" << i << L"] ShaderId=" << shaderId
                   << L", HashSizeBytes=" << hashSize << std::endl;
    }
}


int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2)
    {
        std::wcout << L"Usage: D3DStateInspection <path-to-capture.wpix>" << std::endl;
        return 1;
    }

    std::filesystem::path capturePath = std::filesystem::absolute(argv[1]);
    if (!std::filesystem::exists(capturePath))
    {
        std::wcout << L"File not found: " << capturePath << std::endl;
        return 1;
    }

    ComPtr<IPixGpuCaptureDocument> captureDocument = OpenCapture(capturePath.wstring());
    StartAnalysis(captureDocument);

    // Lambda-based scope guard so StopAnalysis runs whether the body
    // returns normally, hits an early exit, or throws (CheckHr throws
    // on FAILED). Releasing the analysis interface alone does not tear
    // down the device-side session, which can surface as "analysis
    // already running" on back-to-back CI runs.
    struct AnalysisScope
    {
        ComPtr<IPixGpuCaptureDocument>& document;
        ~AnalysisScope() { StopAnalysis(document); }
    } analysisScope{ captureDocument };

    try
    {
        auto selectedEvent = FindFirstGpuProgramEvent(captureDocument);
        if (!selectedEvent.has_value())
        {
            std::wcout << L"No Draw or Dispatch event was found in the GPU capture." << std::endl;
            return 1;
        }

        // GetProgramState returns IPixProgramState for any Draw / Dispatch /
        // DispatchRays / DispatchMesh event. From there, IPixGpuProgram exposes
        // the global root signature and bound shaders, and IPixGenericPipeline
        // (queryable from IPixGpuProgram for graphics/compute pipelines) gives
        // us the pipeline state object's subobject list.
        ComPtr<IPixProgramState> programState;
        CheckHr(
            captureDocument->GetProgramState(
                &selectedEvent->EventInfo,
                IID_PPV_ARGS(programState.ReleaseAndGetAddressOf())),
            L"GetProgramState failed");

        PrintProgramType(programState.Get());

        ComPtr<IPixGpuProgram> gpuProgram;
        CheckHr(
            programState->GetGpuProgram(IID_PPV_ARGS(gpuProgram.ReleaseAndGetAddressOf())),
            L"GetGpuProgram failed");

        PrintGlobalRootSignature(gpuProgram.Get());
        PrintShaders(gpuProgram.Get());

        // Pipeline state subobjects only apply to graphics/compute (generic)
        // pipelines; for raytracing/work-graph the pipeline is described by
        // state-object subobjects which surface here too via IPixPipelineState.
        ComPtr<IPixGenericPipeline> genericPipeline;
        if (SUCCEEDED(gpuProgram.As(&genericPipeline)) && genericPipeline != nullptr)
        {
            PrintPipelineSubobjects(genericPipeline.Get());
        }

        return 0;
    }
    catch (HRESULT)
    {
        // Error message already printed by CheckHr; StopAnalysis runs in
        // ~AnalysisScope.
        return 1;
    }
}
