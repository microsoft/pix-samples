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
// DX Dump File Parser Sample (C++)
//
// Demonstrates parsing DirectX dump files (.dxdmp)
// using the experimental PIX APIs. Extracts and
// prints metadata, command queue events, page faults,
// resources, GPU state, and shader data.
//
// Usage: DXDumpFileParser <path-to-dxdmp-file>
//

#include <wrl.h>
using Microsoft::WRL::ComPtr;
#include <string>
#include <iostream>
#include <vector>
#include <sstream>
#include <filesystem>
#include <iomanip>

#include "Helpers.h"
#include "Logger.h"
#include "ProgressNotifications.h"

namespace
{
    std::wstring GetAnnotationLink(ComPtr<IPixStringAnnotation> annotation)
    {
        switch (annotation->GetContextType())
        {
        case PIX_STRING_ANNOTATION_CONTEXT_TYPE::PIX_STRING_ANNOTATION_SHADER:
            return L"Shader";
        case PIX_STRING_ANNOTATION_CONTEXT_TYPE::PIX_STRING_ANNOTATION_QUEUE:
        {
            ComPtr<IPixPostmortemQueueInfo> queue;
            AssertHr(annotation->GetContext(IID_PPV_ARGS(queue.ReleaseAndGetAddressOf())), L"Expected IPixPostmortemQueueInfo");
            return queue->GetName();
        }
        case PIX_STRING_ANNOTATION_CONTEXT_TYPE::PIX_STRING_ANNOTATION_EVENT:
        {
            std::wstringstream stream;
            ComPtr<IPixPostmortemEvent> event;
            AssertHr(annotation->GetContext(IID_PPV_ARGS(event.ReleaseAndGetAddressOf())), L"Expected IPixPostmortemEvent");
            switch (event->GetType())
            {
            case PIX_EVENT_D3D_API:
            {
                ComPtr<IPixPostmortemD3D12ApiEvent> apiEvent;
                AssertHr(event->QueryInterface(IID_PPV_ARGS(apiEvent.ReleaseAndGetAddressOf())), L"Expected IPixPostmortemD3D12ApiEvent");
                stream << L"[D3D API] " << apiEvent->GetName();
                break;
            }
            case PIX_EVENT_CUSTOM_MARKER:
            {
                ComPtr<IPixPostmortemCustomMarker> customMarker;
                AssertHr(event->QueryInterface(IID_PPV_ARGS(customMarker.ReleaseAndGetAddressOf())), L"Expected IPixPostmortemCustomMarker");
                stream << L"[MARKER] " << L"Source: " << customMarker->GetSource();
                break;
            }
            case PIX_EVENT_PIX_MARKER:
            {
                ComPtr<IPixPostmortemPixMarker> pixMarker;
                AssertHr(event->QueryInterface(IID_PPV_ARGS(pixMarker.ReleaseAndGetAddressOf())), L"Expected IPixPostmortemPixMarker");
                stream << L"[PIX MARKER] " << pixMarker->GetName();
                break;
            }
            default:
            {
                ComPtr<IPixPostmortemDriverEvent> driverEvent;
                AssertHr(event->QueryInterface(IID_PPV_ARGS(driverEvent.ReleaseAndGetAddressOf())), L"Expected IPixPostmortemDriverEvent");
                stream << L"[DRIVER] " << driverEvent->GetName();
            }
            }
            return stream.str();
        }
        default:
            return L"";
        }
    }

    std::wstring GetAnnotatedString(ComPtr<IPixAnnotatedString> annotatedString)
    {
        std::wstring rawString = annotatedString->GetString();

        ComPtr<IPixCollection> annotations;
        if (FAILED(annotatedString->GetAnnotations(IID_PPV_ARGS(annotations.ReleaseAndGetAddressOf()))))
        {
            return rawString;
        }

        UINT64 annotationCount = annotations->GetCount();
        if (annotationCount == 0ull)
        {
            return rawString;
        }

        std::wstringstream stream;
        PIX_STRING_ANNOTATION_RANGE currRange;
        UINT32 currIndex = 0u;
        for (auto i = 0ull; i < annotationCount; ++i)
        {
            ComPtr<IPixStringAnnotation> annotation;
            if (SUCCEEDED(annotations->Get(i, IID_PPV_ARGS(annotation.ReleaseAndGetAddressOf()))))
            {
                AssertHr(annotation->GetRange(&currRange), L"GetRange failed");
                if (currRange.StartIndex < currIndex)
                {
                    // Ignore overlapping links
                    continue;
                }
                if (currRange.StartIndex > currIndex)
                {
                    stream << rawString.substr(currIndex, currRange.StartIndex - currIndex);
                }
                stream << L"[" << rawString.substr(currRange.StartIndex, currRange.Length) << L"](" << GetAnnotationLink(annotation) << L")";
                currIndex = currRange.StartIndex + currRange.Length;
            }
        }

        UINT32 rawStrSize = static_cast<UINT32>(rawString.size());
        if (currIndex < rawStrSize)
        {
            stream << rawString.substr(currIndex, rawStrSize - currIndex);
        }
        return stream.str();
    }

    void DumpMetadata(ComPtr<IPixPostmortemDocument> document)
    {
        std::wcout << L"\n===== DirectX dump file metadata =====\n" << std::endl;

        std::wcout << L"Version: " << document->GetVersion() << std::endl;

        std::wcout << L"Device Error Code: " << document->GetDeviceErrorCode() << std::endl;

        FILETIME creationTime = document->GetCreationTime();
        SYSTEMTIME systemTime{};
        FileTimeToSystemTime(&creationTime, &systemTime);
        std::wcout << L"Creation Time: "
            << systemTime.wYear << L"-"
            << std::setw(2) << std::setfill(L'0') << systemTime.wMonth << L"-"
            << std::setw(2) << std::setfill(L'0') << systemTime.wDay << L" "
            << std::setw(2) << std::setfill(L'0') << systemTime.wHour << L":"
            << std::setw(2) << std::setfill(L'0') << systemTime.wMinute << L":"
            << std::setw(2) << std::setfill(L'0') << systemTime.wSecond << std::endl;

        PIX_APPLICATION_DESC appDesc = document->GetApplicationDescription();
        std::wcout << L"Application Name: " << GetString(appDesc.D3DApplicationDesc.pName) << std::endl;
        std::wcout << L"Executable Name: " << GetString(appDesc.D3DApplicationDesc.pExeFilename) << std::endl;
        std::wcout << L"Version: " << appDesc.D3DApplicationDesc.Version << std::endl;
        std::wcout << L"Engine Name: " << GetString(appDesc.D3DApplicationDesc.pEngineName) << std::endl;
        std::wcout << L"Engine Version: " << appDesc.D3DApplicationDesc.EngineVersion << std::endl;
    }

    void DumpDeviceErrorBucket(ComPtr<IPixPostmortemDocument> document)
    {
        std::wcout << L"\n===== Device error bucket =====\n" << std::endl;
        std::wcout << L"Bucket Name: " << document->GetDeviceErrorBucket() << std::endl;
        std::wcout << L"Documentation Link: " << GetString(document->GetDocumentationLink()) << std::endl;

        ComPtr<IPixAnnotatedString> briefSummary;
        if (SUCCEEDED(CheckHr(document->GetBriefSummary(IID_PPV_ARGS(briefSummary.ReleaseAndGetAddressOf())), L"Expected IPixAnnotatedString!")))
        {
            std::wcout << L"Brief Summary: " << GetAnnotatedString(std::move(briefSummary)) << std::endl;
        }

        ComPtr<IPixAnnotatedString> detailedSummary;
        if (SUCCEEDED(CheckHr(document->GetDetailedSummary(IID_PPV_ARGS(detailedSummary.ReleaseAndGetAddressOf())), L"Expected IPixAnnotatedString!")))
        {
            std::wcout << L"Detailed Summary: " << GetAnnotatedString(std::move(detailedSummary)) << std::endl;
        }
    }

    void DumpResource(ComPtr<IPixPostmortemD3D12Resource> resource, UINT32 numTabs)
    {
        std::wstring tabs(numTabs, L'\t');

        std::wcout << tabs << L"Resource: " << GetString(resource->GetName(), L"(unnamed)") << std::endl;
        std::wcout << tabs << L"\tGPU VA: 0x" << std::hex << resource->GetGpuVirtualAddress() << std::dec << std::endl;
        std::wcout << tabs << L"\tSize: " << resource->GetSizeBytes() << L" bytes" << std::endl;

        const D3D12_RESOURCE_DESC2* pDesc = resource->GetResourceDesc();
        if (pDesc)
        {
            std::wcout << tabs << L"\tDimension: " << pDesc->Dimension << std::endl;
            std::wcout << tabs << L"\tWidth: " << pDesc->Width << L", Height: " << pDesc->Height << std::endl;
        }

        UINT64 attrCount = resource->GetAttributeCount();
        for (auto i = 0ull; i < attrCount; ++i)
        {
            PIX_POSTMORTEM_RESOURCE_ATTRIBUTE attr{};
            if (SUCCEEDED(resource->GetAttribute(i, &attr)))
            {
                std::wcout << tabs << L"\tAttribute[" << i << L"]: " << attr.Name << L" (" << attr.Description << L") = ";
                DumpPixValue(attr.Value);
                std::wcout << std::endl;
            }
        }

        ComPtr<IPixCollection> resourceEvents;
        if (FAILED(CheckHr(resource->GetEvents(IID_PPV_ARGS(resourceEvents.ReleaseAndGetAddressOf())), L"GetEvents failed")))
        {
            return;
        }
        UINT64 eventCount = resourceEvents->GetCount();
        if (eventCount > 0)
        {
            std::wcout << tabs << L"\tResource events (" << eventCount << L"):" << std::endl;
            for (auto i = 0ull; i < eventCount; ++i)
            {
                ComPtr<IPixPostmortemResourceEvent> resourceEvent;
                AssertHr(resourceEvents->Get(i, IID_PPV_ARGS(resourceEvent.ReleaseAndGetAddressOf())), L"Get resource event failed");
                UINT64 ts = resourceEvent->GetTimestampInNs();
                std::wcout << tabs << L"\t\t[" << i << L"] " << resourceEvent->GetType()
                    << L" @ " << ts << L" ns" << std::endl;
            }
        }
    }

    void DumpResources(ComPtr<IPixPostmortemDocument> document)
    {
        ComPtr<IPixCollection> resources;
        if (FAILED(CheckHr(document->GetResources(IID_PPV_ARGS(resources.ReleaseAndGetAddressOf())), L"GetResources failed")))
        {
            return;
        }

        UINT64 resourceCount = resources->GetCount();
        std::wcout << L"\n===== Resources =====" << std::endl;

        for (auto i = 0ull; i < resourceCount; ++i)
        {
            ComPtr<IPixPostmortemD3D12Resource> resource;
            AssertHr(resources->Get(i, IID_PPV_ARGS(resource.ReleaseAndGetAddressOf())), L"Get resource failed");
            std::wcout << std::endl;
            DumpResource(resource, 0u);
        }
    }

    void DumpPageFaults(ComPtr<IPixPostmortemDocument> document)
    {
        ComPtr<IPixCollection> pageFaults;
        if (FAILED(CheckHr(document->GetPageFaults(IID_PPV_ARGS(pageFaults.ReleaseAndGetAddressOf())), L"GetPageFaults failed")))
        {
            return;
        }

        UINT64 pageFaultCount = pageFaults->GetCount();
        std::wcout << L"\n===== Page faults =====" << std::endl;

        for (auto i = 0ull; i < pageFaultCount; ++i)
        {
            ComPtr<IPixPostmortemPageFault> pageFault;
            AssertHr(pageFaults->Get(i, IID_PPV_ARGS(pageFault.ReleaseAndGetAddressOf())), L"Get page fault failed");

            UINT64 ts = pageFault->GetTimestampInNs();
            std::wcout << L"\t[GPU VA: 0x" << std::hex << pageFault->GetGpuVirtualAddress() << std::dec << L" @ " << ts << L" ns" << L"] " << std::endl;
            std::wcout << L"\t\tType: " << pageFault->GetType() << std::endl;
            std::wcout << L"\t\tAccess: " << pageFault->GetAccessType() << std::endl;

            ComPtr<IPixPostmortemQueueInfo> queue;
            if (SUCCEEDED(pageFault->GetQueue(IID_PPV_ARGS(queue.ReleaseAndGetAddressOf()))))
            {
                std::wcout << L"\tQueue: " << queue->GetName() << std::endl;
            }
            else
            {
                std::wcout << L"\t(unknown)" << std::endl;
            }

            ComPtr<IPixCollection> resources;
            if (SUCCEEDED(CheckHr(pageFault->GetResources(IID_PPV_ARGS(resources.ReleaseAndGetAddressOf())), L"GetResources failed")))
            {
                UINT64 resourceCount = resources->GetCount();
                std::wcout << L"\tResources:" << std::endl;
                for (auto j = 0ull; j < resourceCount; ++j)
                {
                    ComPtr<IPixPostmortemD3D12Resource> resource;
                    AssertHr(resources->Get(j, IID_PPV_ARGS(resource.ReleaseAndGetAddressOf())), L"Get resource failed");
                    DumpResource(resource, 2u);
                }
            }

            ComPtr<IPixCollection> resourceEvents;
            if (SUCCEEDED(CheckHr(pageFault->GetResourceEvents(IID_PPV_ARGS(resourceEvents.ReleaseAndGetAddressOf())), L"GetResourceEvents failed")))
            {
                UINT64 eventCount = resourceEvents->GetCount();
                std::wcout << L"\tResource events (" << eventCount << L"):" << std::endl;
                for (auto j = 0ull; j < eventCount; ++j)
                {
                    ComPtr<IPixPostmortemResourceEvent> resourceEvent;
                    if (FAILED(CheckHr(resourceEvents->Get(j, IID_PPV_ARGS(resourceEvent.ReleaseAndGetAddressOf())), L"Get resource event failed")))
                    {
                        continue;
                    }
                    UINT64 ts = resourceEvent->GetTimestampInNs();
                    std::wcout << L"\t\t[" << j << L"] " << resourceEvent->GetType()
                        << L" @ " << ts << L" ns" << std::endl;

                    ComPtr<IPixPostmortemD3D12Resource> resource;
                    AssertHr(resourceEvent->GetResource(IID_PPV_ARGS(resource.ReleaseAndGetAddressOf())), L"Get resource failed");
                    std::wcout << L"\t\tResource: " << GetString(resource->GetName(), L"(unnamed)") << std::endl;
                }
            }
        }
    }

    void DumpEvent(ComPtr<IPixPostmortemEvent> event, UINT32 numTabs)
    {
        std::wstring tabs(numTabs, L'\t');

        if (event->GetType() == PIX_EVENT_D3D_API)
        {
            ComPtr<IPixPostmortemD3D12ApiEvent> apiEvent;
            AssertHr(event->QueryInterface(IID_PPV_ARGS(apiEvent.ReleaseAndGetAddressOf())), L"Expected IPixPostmortemD3D12ApiEvent");
            std::wcout << tabs << apiEvent->GetName();
        }
        else if (event->GetType() == PIX_EVENT_CUSTOM_MARKER)
        {
            ComPtr<IPixPostmortemCustomMarker> customMarker;
            AssertHr(event->QueryInterface(IID_PPV_ARGS(customMarker.ReleaseAndGetAddressOf())), L"Expected IPixPostmortemCustomMarker");
            ComPtr<IPixPostmortemStringMarker> stringMarker;
            if (SUCCEEDED(event->QueryInterface(IID_PPV_ARGS(stringMarker.ReleaseAndGetAddressOf()))))
            {
                std::wcout << tabs << L"\"" << GetString(stringMarker->GetPayload()) << L"\"";
            }
            else
            {
                std::wcout << tabs << L"Marker source: " << customMarker->GetSource();
            }
        }
        else if (event->GetType() == PIX_EVENT_PIX_MARKER)
        {
            ComPtr<IPixPostmortemPixMarker> pixMarker;
            AssertHr(event->QueryInterface(IID_PPV_ARGS(pixMarker.ReleaseAndGetAddressOf())), L"Expected IPixPostmortemPixMarker");
            std::wcout << tabs << pixMarker->GetName();
        }
        else
        {
            ComPtr<IPixPostmortemDriverEvent> driverEvent;
            AssertHr(event->QueryInterface(IID_PPV_ARGS(driverEvent.ReleaseAndGetAddressOf())), L"Expected IPixPostmortemDriverEvent");
            std::wcout << tabs << driverEvent->GetName();
        }

        PIX_EVENT_STATUS status = event->GetStatus();
        if (status == PIX_EVENT_STATUS_IN_PROGRESS)
        {
            std::wcout << L" << [PIX_EVENT_STATUS_IN_PROGRESS]";
        }
        else if (status == PIX_EVENT_STATUS_POSSIBLY_COMPLETED)
        {
            std::wcout << L" << [PIX_EVENT_STATUS_POSSIBLY_COMPLETED]";
        }
        std::wcout << std::endl;

        ComPtr<IPixCollection> shaders;
        if (SUCCEEDED(event->GetCorrelatedShaders(IID_PPV_ARGS(shaders.ReleaseAndGetAddressOf()))))
        {
            UINT64 shaderCount = shaders->GetCount();
            if (shaderCount > 0ull)
            {
                std::wcout << tabs << L"  Correlated shaders (" << shaderCount << L"):" << std::endl;
                for (auto i = 0ull; i < shaderCount; ++i)
                {
                    ComPtr<IPixShader> shader;
                    if (SUCCEEDED(shaders->Get(i, IID_PPV_ARGS(shader.ReleaseAndGetAddressOf()))))
                    {
                        std::wcout << tabs << L"\tShader";
                        UINT32 hashSize = shader->GetHashSizeBytes();
                        if (hashSize > 0u)
                        {
                            std::vector<BYTE> hashBytes(hashSize);
                            if (SUCCEEDED(shader->GetHash(hashSize, hashBytes.data())))
                            {
                                std::wcout << L" Hash=";
                                for (BYTE b : hashBytes)
                                {
                                    std::wcout << std::hex << std::setw(2) << std::setfill(L'0') << b;
                                }
                                std::wcout << std::dec;
                            }
                        }
                        else
                        {
                            std::wcout << L" ID=" << shader->GetId();
                        }
                        std::wcout << L" Stage=" << shader->GetStage() << std::endl;
                    }
                }
            }
        }

        // Correlated resources
        ComPtr<IPixCollection> resources;
        if (SUCCEEDED(event->GetCorrelatedResources(IID_PPV_ARGS(resources.ReleaseAndGetAddressOf()))))
        {
            UINT64 resourceCount = resources->GetCount();
            if (resourceCount > 0ull)
            {
                std::wcout << tabs << L"  Correlated resources(" << resourceCount << L") :" << std::endl;
                for (auto i = 0ull; i < resourceCount; ++i)
                {
                    ComPtr<IPixPostmortemD3D12Resource> resource;
                    if (SUCCEEDED(resources->Get(i, IID_PPV_ARGS(resource.ReleaseAndGetAddressOf()))))
                    {
                        std::wcout << tabs << L"\t" << GetString(resource->GetName()) << std::endl;
                    }
                }
            }
        }
    }

    void DumpChildEvents(ComPtr<IPixPostmortemEvent> parentEvent, UINT32 numTabs)
    {
        ComPtr<IPixCollection> childEvents;
        if (FAILED(CheckHr(parentEvent->GetChildEvents(IID_PPV_ARGS(childEvents.ReleaseAndGetAddressOf())), L"GetChildEvents failed")))
        {
            return;
        }

        UINT64 numChildEvents = childEvents->GetCount();

        for (auto i = 0ull; i < numChildEvents; i++)
        {
            ComPtr<IPixPostmortemEvent> event;
            AssertHr(childEvents->Get(i, IID_PPV_ARGS(event.ReleaseAndGetAddressOf())), L"GetChildEvent failed");
            DumpEvent(event, numTabs);
            DumpChildEvents(std::move(event), numTabs + 1u);
        }
    }

    void DumpQueueEvents(ComPtr<IPixPostmortemQueueInfo> queue, UINT32 numTabs)
    {
        ComPtr<IPixCollection> events;
        if (FAILED(CheckHr(queue->GetEvents(IID_PPV_ARGS(events.ReleaseAndGetAddressOf())), L"GetEvents failed")))
        {
            return;
        }
        UINT64 numEvents = events->GetCount();

        for (auto i = 0ull; i < numEvents; i++)
        {
            ComPtr<IPixPostmortemEvent> event;
            AssertHr(events->Get(i, IID_PPV_ARGS(event.ReleaseAndGetAddressOf())), L"GetEvent failed");
            DumpEvent(event, numTabs);
            DumpChildEvents(std::move(event), numTabs + 1u);
        }
    }

    void DumpEngineQueues(ComPtr<IPixPostmortemDocument> document)
    {
        ComPtr<IPixCollection> queues;
        if (FAILED(CheckHr(document->GetQueues(IID_PPV_ARGS(queues.ReleaseAndGetAddressOf())), L"GetQueues failed")))
        {
            return;
        }

        std::wcout << L"\n===== Engine queues =====" << std::endl;
        for (auto i = 0ull; i < queues->GetCount(); i++)
        {
            ComPtr<IPixPostmortemQueueInfo> queueInfo;
            AssertHr(queues->Get(i, IID_PPV_ARGS(queueInfo.ReleaseAndGetAddressOf())), L"GetQueue failed");

            std::wcout << L"\n===== [" << queueInfo->GetName() << L"] =====" << std::endl;
            std::wcout << L"\tType: " << queueInfo->GetType() << std::endl;
            std::wcout << L"\tStatus: " << queueInfo->GetStatus() << std::endl;

            UINT32 numStatusFields = queueInfo->GetHardwareStatusCount();
            if (numStatusFields > 0u)
            {
                std::wcout << L"\n\tHardware status fields:" << std::endl;
                for (auto j = 0u; j < numStatusFields; j++)
                {
                    PIX_POSTMORTEM_QUEUE_HW_STATUS queueStatus;
                    if (FAILED(CheckHr(queueInfo->GetHardwareStatus(j, &queueStatus), L"GetHardwareStatus failed")))
                    {
                        continue;
                    }

                    std::wcout << L"\t  [" << j << L"] " << queueStatus.Name;
                    std::wcout << L" (" << queueStatus.Description << L")";
                    std::wcout << L" Severity: " << queueStatus.SeverityLevel;
                    std::wcout << L" Value: ";
                    DumpPixValue(queueStatus.Value);
                    std::wcout << std::endl;
                }
            }

            ComPtr<IPixCollection> pageFaults;
            if (SUCCEEDED(CheckHr(queueInfo->GetPageFaults(IID_PPV_ARGS(pageFaults.ReleaseAndGetAddressOf())), L"GetPageFaults failed")))
            {
                UINT64 pageFaultCount = pageFaults->GetCount();
                if (pageFaultCount > 0ull)
                {
                    std::wcout << L"\n\tPage faults:" << std::endl;
                    for (auto j = 0ull; j < pageFaultCount; ++j)
                    {
                        ComPtr<IPixPostmortemPageFault> pageFault;
                        AssertHr(pageFaults->Get(j, IID_PPV_ARGS(pageFault.ReleaseAndGetAddressOf())), L"Get page fault failed");
                        std::wcout << L"\t  [GPU VA: 0x" << std::hex << pageFault->GetGpuVirtualAddress() << std::dec << L"] " << std::endl;
                        std::wcout << L"\t\tType: " << pageFault->GetType() << std::endl;
                        std::wcout << L"\t\tAccess Type: " << pageFault->GetAccessType() << std::endl;
                    }
                }
            }

            std::wcout << L"\n\t===== Events =====\n" << std::endl;
            DumpQueueEvents(queueInfo.Get(), 1u);
        }
    }

    void DumpGpuStateRow(ComPtr<IPixGpuStateTableRow> row, UINT32 numColumns, UINT32 depth)
    {
        std::wstring tabs(depth, L'\t');
        std::wcout << tabs << row->GetName();

        LPCWSTR description = row->GetDescription();
        if (description && description[0] != L'\0')
        {
            std::wcout << L" (" << description << L")";
        }

        for (auto i = 0u; i < numColumns; i++)
        {
            PIX_VALUE value;
            if (FAILED(CheckHr(row->GetValue(i, &value), L"GetValue failed")))
            {
                continue;
            }
            std::wcout << L"\t\t";
            DumpPixValue(value);
        }
        std::wcout << std::endl;

        ComPtr<IPixCollection> childRows;
        if (FAILED(CheckHr(row->GetChildRows(IID_PPV_ARGS(childRows.ReleaseAndGetAddressOf())), L"GetChildRows failed")))
        {
            return;
        }
        UINT64 childCount = childRows->GetCount();

        if (childCount == 0ull)
        {
            return;
        }

        for (auto j = 0ull; j < childCount; j++)
        {
            ComPtr<IPixGpuStateTableRow> childRow;
            AssertHr(childRows->Get(j, IID_PPV_ARGS(childRow.ReleaseAndGetAddressOf())), L"GetChildRow failed");
            DumpGpuStateRow(std::move(childRow), numColumns, depth + 1u);
        }
        std::wcout << std::endl;
    }

    void DumpGpuStateAtDumpTime(ComPtr<IPixPostmortemDocument> document)
    {
        std::wcout << L"\n===== GPU state (at dump time) =====" << std::endl;

        ComPtr<IPixCollection> gpuStateTables;
        if (FAILED(CheckHr(document->GetGpuStateTables(IID_PPV_ARGS(gpuStateTables.ReleaseAndGetAddressOf())), L"GetGpuStateTables failed")))
        {
            return;
        }

        UINT64 numTables = gpuStateTables->GetCount();
        for (auto i = 0ull; i < numTables; i++)
        {
            ComPtr<IPixGpuStateTable> gpuStateTable;
            AssertHr(gpuStateTables->Get(i, IID_PPV_ARGS(gpuStateTable.ReleaseAndGetAddressOf())), L"GetTable failed");

            std::wcout << L"\n===== [" << gpuStateTable->GetName() << L"] =====\n" << std::endl;

            LPCWSTR tableDescription = gpuStateTable->GetDescription();
            if (tableDescription && tableDescription[0] != L'\0')
            {
                std::wcout << L"Description: " << tableDescription << std::endl;
            }

            std::wcout << L"[Columns]\t";
            UINT32 numColumns = gpuStateTable->GetColumnCount();
            for (auto j = 0u; j < numColumns; j++)
            {
                LPCWSTR columnName;
                if (FAILED(CheckHr(gpuStateTable->GetColumnName(j, &columnName), L"GetColumnName failed")))
                {
                    continue;
                }
                std::wcout << L"\t\t\t\t" << columnName;
            }
            std::wcout << L"\n" << std::endl;

            ComPtr<IPixCollection> rows;
            if (FAILED(CheckHr(gpuStateTable->GetRows(IID_PPV_ARGS(rows.ReleaseAndGetAddressOf())), L"GetRows failed")))
            {
                continue;
            }
            UINT64 numRootRows = rows->GetCount();
            for (auto k = 0ull; k < numRootRows; k++)
            {
                ComPtr<IPixGpuStateTableRow> row;
                AssertHr(rows->Get(k, IID_PPV_ARGS(row.ReleaseAndGetAddressOf())), L"GetRow failed");
                DumpGpuStateRow(std::move(row), numColumns, 0u);
            }
            std::wcout << std::endl;
        }
    }

    void DumpApplicationBlobs(ComPtr<IPixPostmortemDocument> document)
    {
        ComPtr<IPixCollection> blobs;
        if (FAILED(CheckHr(document->GetApplicationBlobs(IID_PPV_ARGS(blobs.ReleaseAndGetAddressOf())), L"GetApplicationBlobs failed")))
        {
            return;
        }

        UINT64 blobCount = blobs->GetCount();
        std::wcout << L"\n===== Application blobs (" << blobCount << L") =====" << std::endl;

        for (auto i = 0ull; i < blobCount; ++i)
        {
            ComPtr<IPixApplicationBlob> blob;
            AssertHr(blobs->Get(i, IID_PPV_ARGS(blob.ReleaseAndGetAddressOf())), L"Get blob failed");

            std::wcout << L"\n  Blob [" << i << L"]" << std::endl;
            std::wcout << L"  Metadata: 0x" << std::hex << blob->GetMetadata() << std::dec << std::endl;
            std::wcout << L"  Size: " << blob->GetSizeBytes() << L" bytes" << std::endl;
        }
    }

    void DumpShaderDebuggingData(ComPtr<IPixPostmortemDocument> document)
    {
        ComPtr<IPixShaderDebuggingData> shaderDebuggingData;
        if (FAILED(document->GetShaderDebuggingData(IID_PPV_ARGS(shaderDebuggingData.ReleaseAndGetAddressOf()))))
        {
            std::wcout << L"\n===== Shader debugging data: (none) =====" << std::endl;
            return;
        }

        ComPtr<IPixCollection> waves;
        if (FAILED(CheckHr(shaderDebuggingData->GetWaves(IID_PPV_ARGS(waves.ReleaseAndGetAddressOf())), L"GetWaves failed")))
        {
            return;
        }

        UINT64 waveCount = waves->GetCount();
        std::wcout << L"\n===== Shader debugging data (" << waveCount << L" waves) =====" << std::endl;

        for (auto i = 0ull; i < waveCount; ++i)
        {
            ComPtr<IPixShaderWave> wave;
            AssertHr(waves->Get(i, IID_PPV_ARGS(wave.ReleaseAndGetAddressOf())), L"Get wave failed");

            std::wcout << L"\n  Wave [" << i << L"]" << std::endl;
            std::wcout << L"  Status: " << wave->GetStatus() << std::endl;
            std::wcout << L"  Stage: " << wave->GetStage() << std::endl;

            ComPtr<IPixCollection> lanes;
            if (FAILED(CheckHr(wave->GetLanes(IID_PPV_ARGS(lanes.ReleaseAndGetAddressOf())), L"GetLanes failed")))
            {
                continue;
            }
            std::wcout << L"  Lane count: " << lanes->GetCount() << std::endl;
        }
    }

    void OpenDXDumpFile(const std::wstring& dumpFile)
    {
        ComPtr<IPixFactoryExperimental> factory;
        AssertHr(PixCreateFactory(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())), L"PixCreateFactory failed");

        auto logger = Microsoft::WRL::Make<LoggerImpl>();
        auto notifications = Microsoft::WRL::Make<ProgressNotificationsImpl>();

        ComPtr<IPixPostmortemDocument> postmortemDocument;
        if (FAILED(CheckHr(factory->OpenPostmortemDumpDocument(dumpFile.c_str(), logger.Get(), notifications.Get(), /*pCancellationToken*/ nullptr, IID_PPV_ARGS(postmortemDocument.ReleaseAndGetAddressOf())), L"OpenPostmortemDumpDocument failed")))
        {
            logger->DumpLogs();
            return;
        }

        notifications->DumpNotifications();

        DumpMetadata(postmortemDocument.Get());
        DumpDeviceErrorBucket(postmortemDocument.Get());
        DumpEngineQueues(postmortemDocument.Get());
        DumpGpuStateAtDumpTime(postmortemDocument.Get());
        DumpPageFaults(postmortemDocument.Get());
        DumpResources(postmortemDocument.Get());
        DumpApplicationBlobs(postmortemDocument.Get());
        DumpShaderDebuggingData(postmortemDocument.Get());

        logger->DumpLogs();
    }
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2)
    {
        std::wcerr << L"Usage: DXDumpFileParser <path-to-dxdmp-file>" << std::endl;
        return 1;
    }

    std::wstring dumpFile(argv[1]);
    OpenDXDumpFile(dumpFile);
    return 0;
}
