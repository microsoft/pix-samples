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

#include "Helpers.h"

#include <cstdlib>
#include <iostream>

LPCWSTR GetString(LPCWSTR str, LPCWSTR defaultStr)
{
    return (str && *str) ? str : defaultStr;
}

namespace
{
    LPCWSTR HResultToString(HRESULT hr)
    {
        switch (hr)
        {
        case E_NOTIMPL: return L"E_NOTIMPL";
        case E_NOINTERFACE: return L"E_NOINTERFACE";
        case E_POINTER: return L"E_POINTER";
        case E_ABORT: return L"E_ABORT";
        case E_FAIL: return L"E_FAIL";
        case E_UNEXPECTED: return L"E_UNEXPECTED";
        case E_ACCESSDENIED: return L"E_ACCESSDENIED";
        case E_HANDLE: return L"E_HANDLE";
        case E_OUTOFMEMORY: return L"E_OUTOFMEMORY";
        case E_INVALIDARG: return L"E_INVALIDARG";
        case E_BOUNDS: return L"E_BOUNDS";
        case E_PENDING: return L"E_PENDING";
        default: return nullptr;
        }
    }
}

HRESULT CheckHr(HRESULT hr, std::wstring_view msg)
{
    if (FAILED(hr))
    {
        std::wcout << msg << std::endl;
        std::wcout << L"Error code: 0x" << std::hex << hr << std::dec;
        if (LPCWSTR name = HResultToString(hr))
        {
            std::wcout << L" (" << name << L")";
        }
        std::wcout << std::endl;
    }
    return hr;
}

void AssertHr(HRESULT hr, std::wstring_view msg)
{
    if (FAILED(CheckHr(hr, msg)))
    {
        exit(1);
    }
}

void DumpPixValue(const PIX_VALUE& value)
{
    if (value.ValueType == PIX_VALUE_TYPE::PIX_VALUE_STRING)
    {
        std::wcout << value.Value.ValueString;
    }
    else
    {
        std::wcout << L"0x" << std::hex << value.Value.ValueNumeric.Bits << std::dec;
    }
}
