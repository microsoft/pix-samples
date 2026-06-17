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

#pragma once

#include <wrl.h>
#include <string>
#include <vector>

#include "PixApi.h"
#include "PixApiExperimental.h"

struct LogEntry
{
    PIX_SEVERITY_LEVEL Severity;
    PIX_LOGGING_SOURCE SourceType;
    std::wstring SourceName;
    std::wstring Message;
};

// IPixLogging callback implementation
class LoggerImpl : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IPixLogging>
{
    std::vector<LogEntry> m_logs;

public:
    void ReportLog(_In_ PIX_LOGGING_MESSAGE log) override;
    void DumpLogs() const;
};
