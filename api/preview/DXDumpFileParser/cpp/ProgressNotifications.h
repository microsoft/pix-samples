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

struct ProgressNotification
{
    enum class Kind { Status, Progress } Kind;
    std::wstring Status = L"";
    float Progress = {};
};

// IPixProgressNotifications callback implementation
class ProgressNotificationsImpl : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IPixProgressNotifications>
{
    std::vector<ProgressNotification> m_notifications;

public:
    void OnStatus(_In_ LPCWSTR status) override;
    void OnProgress(_In_ float progress) override;
    void DumpNotifications() const;
};
