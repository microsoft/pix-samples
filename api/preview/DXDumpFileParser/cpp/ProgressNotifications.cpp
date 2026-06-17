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

#include "ProgressNotifications.h"

#include <iostream>

void ProgressNotificationsImpl::OnStatus(_In_ LPCWSTR status)
{
    m_notifications.push_back({ ProgressNotification::Kind::Status, status ? status : L"" });
}

void ProgressNotificationsImpl::OnProgress(_In_ float progress)
{
    m_notifications.push_back({ ProgressNotification::Kind::Progress, L"", progress });
}

void ProgressNotificationsImpl::DumpNotifications() const
{
    std::wcout << L"\n===== Open notifications (" << m_notifications.size() << L" entries) =====" << std::endl;
    for (size_t i = 0uz; i < m_notifications.size(); ++i)
    {
        auto const& notification = m_notifications[i];
        if (notification.Kind == ProgressNotification::Kind::Status)
        {
            std::wcout << L">> " << notification.Status << std::endl;
        }
        else
        {
            std::wcout << L" Progress: " << (notification.Progress * 100.0f) << L"%" << std::endl;
        }
    }
}
