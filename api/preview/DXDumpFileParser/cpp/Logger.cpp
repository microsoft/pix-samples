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

#include "Logger.h"

#include <iostream>

#include "Helpers.h"

void LoggerImpl::ReportLog(_In_ PIX_LOGGING_MESSAGE log)
{
    m_logs.push_back({
        log.Severity,
        log.SourceType,
        log.SourceName ? log.SourceName : L"",
        log.Message ? log.Message : L"",
    });
}

void LoggerImpl::DumpLogs() const
{
    std::wcout << L"\n===== Logs (" << m_logs.size() << L" entries) =====" << std::endl;
    for (size_t i = 0uz; i < m_logs.size(); ++i)
    {
        auto const& entry = m_logs[i];
        std::wcout << L"\t[" << entry.Severity << L"][" << entry.SourceType << L"]";
        if (!entry.SourceName.empty())
        {
            std::wcout << L" [" << entry.SourceName << L"]";
        }
        std::wcout << L" " << entry.Message << std::endl;
    }
}
