#*********************************************************
#
# Copyright (c) Microsoft. All rights reserved.
# This code is licensed under the MIT License (MIT).
# THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
# ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
# IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
# PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
#
#*********************************************************
#
# DX Dump File Parser Sample (Python)
#
# Demonstrates parsing DirectX dump files (.dxdmp)
# using the experimental PIX API via pythonnet. Extracts and prints:
#   - Dump metadata (app name, creation time, device error)
#   - Device error bucket and summary
#   - Command queue event history
#   - Page fault information
#   - Resource details
#
# Usage: python main.py <path-to-dxdmp-file>
#
# Requirements:
#   - 64-bit Python 3.9+
#   - pythonnet >= 3.0  (pip install pythonnet)
#   - .NET 10 runtime
#   - PIX Preview installed, or PIX_DIR environment variable set.
#

import os
import sys


# Shared PIX-install discovery + pythonnet bootstrap. Sample lives at
# api/{preview,retail}/<sample>/python/main.py, so api/_pix_bootstrap.py
# is three directories up.
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
from _pix_bootstrap import find_pix_bin_directory, initialize_pythonnet

def dump_metadata(document):
    """Print dump file metadata."""
    import Microsoft.PIX.Internal as pix_internal

    print("\n===== DirectX dump file metadata =====\n")
    print(f"Version: {document.GetVersion()}")
    print(f"Device Error Code: {document.GetDeviceErrorCode()}")

    creation_time = document.GetCreationTime()
    print(f"Creation Time (FILETIME high={creation_time.dwHighDateTime}, low={creation_time.dwLowDateTime})")

    app_desc = document.GetApplicationDescription()
    print(f"Application Name: {app_desc.D3DApplicationDesc.pName}")
    print(f"Executable: {app_desc.D3DApplicationDesc.pExeFilename}")
    print(f"Engine Name: {app_desc.D3DApplicationDesc.pEngineName}")


def dump_device_error_bucket(document):
    """Print the device error bucket and summary."""
    import Microsoft.PIX.Internal.Extension.PostmortemDump as pix_postmortem

    print("\n===== Device error bucket =====\n")
    print(f"Bucket Name: {document.GetDeviceErrorBucket()}")
    print(f"Documentation Link: {document.GetDocumentationLink()}")

    brief_summary = document.GetBriefSummary()
    print(f"Brief Summary: {brief_summary.GetString()}")

    detailed_summary = document.GetDetailedSummary()
    print(f"Detailed Summary: {detailed_summary.GetString()}")


def dump_queues(document):
    """Print command queue events."""
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Internal as pix_internal

    queues = document.GetQueues()
    print(f"\n===== Engine queues ({queues.GetCount()}) =====")

    for queue_index in range(queues.GetCount()):
        queue_info = pix_extension.PixApiExtensions.Get[
            pix_internal.IPixPostmortemQueueInfo](queues, queue_index)

        print(f"\n  [{queue_info.GetName()}]")
        print(f"    Type: {queue_info.GetType()}")
        print(f"    Status: {queue_info.GetStatus()}")

        # Print root-level events.
        events = queue_info.GetEvents()
        event_count = events.GetCount()
        print(f"    Events ({event_count}):")

        for event_index in range(min(event_count, 20)):  # Limit output
            event = pix_extension.PixApiExtensions.Get[
                pix_internal.IPixPostmortemEvent](events, event_index)
            dump_event(event, depth=3)


def _try_cast(event, interface_type):
    """Try to QI `event` to `interface_type`. Returns the interface or None.

    Pythonnet surfaces a failed QI as a CLR InvalidCastException, which gets
    wrapped as a Python.Runtime.PythonException, not a built-in TypeError.
    Catch the broad Exception base so any cast-related failure (CLR or
    Python) falls through cleanly to the next branch.
    """
    try:
        return interface_type(event)
    except Exception:
        return None


def dump_event(event, depth=0):
    """Print a single postmortem event.

    Mirrors the C# variant's branch list: D3D12 API event, string marker,
    custom marker, PIX marker, driver event. Falls back to status-only for
    anything else.
    """
    import Microsoft.PIX as pix
    import Microsoft.PIX.Internal as pix_internal

    indent = "  " * depth

    api_event = _try_cast(event, pix_internal.IPixPostmortemD3D12ApiEvent)
    if api_event is not None:
        print(f"{indent}{api_event.GetName()}")
        return

    string_marker = _try_cast(event, pix_internal.IPixPostmortemStringMarker)
    if string_marker is not None:
        print(f'{indent}"{string_marker.GetPayload()}"')
        return

    custom_marker = _try_cast(event, pix_internal.IPixPostmortemCustomMarker)
    if custom_marker is not None:
        print(f"{indent}Marker source: {custom_marker.GetSource()}")
        return

    pix_marker = _try_cast(event, pix_internal.IPixPostmortemPixMarker)
    if pix_marker is not None:
        print(f"{indent}[MARKER] {pix_marker.GetName()}")
        return

    driver_event = _try_cast(event, pix_internal.IPixPostmortemDriverEvent)
    if driver_event is not None:
        print(f"{indent}{driver_event.GetName()}")
        return

    # Fallback: just print the status.
    print(f"{indent}[Event] Status: {event.GetStatus()}")


def dump_page_faults(document):
    """Print page fault information."""
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Internal as pix_internal

    page_faults = document.GetPageFaults()
    page_fault_count = page_faults.GetCount()
    print(f"\n===== Page faults ({page_fault_count}) =====")

    for fault_index in range(page_fault_count):
        page_fault = pix_extension.PixApiExtensions.Get[
            pix_internal.IPixPostmortemPageFault](page_faults, fault_index)

        print(f"\n  [GPU VA: 0x{page_fault.GetGpuVirtualAddress():X}]")
        print(f"    Type: {page_fault.GetType()}")
        print(f"    Access: {page_fault.GetAccessType()}")
        print(f"    Timestamp: {page_fault.GetTimestampInNs()} ns")


def dump_resources(document):
    """Print resource information."""
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Internal as pix_internal

    resources = document.GetResources()
    resource_count = resources.GetCount()
    print(f"\n===== Resources ({resource_count}) =====")

    for resource_index in range(min(resource_count, 20)):  # Limit output
        resource = pix_extension.PixApiExtensions.Get[
            pix_internal.IPixPostmortemD3D12Resource](resources, resource_index)

        name = resource.GetName() or "(unnamed)"
        print(f"\n  {name}")
        print(f"    GPU VA: 0x{resource.GetGpuVirtualAddress():X}")
        print(f"    Size: {resource.GetSizeBytes()} bytes")


def main():
    if len(sys.argv) < 2:
        print("Usage: python main.py <path-to-dxdmp-file>")
        sys.exit(1)

    dump_file_path = sys.argv[1]
    if not os.path.isfile(dump_file_path):
        print(f"File not found: {dump_file_path}")
        sys.exit(1)

    bin_directory = find_pix_bin_directory("PixApiCsExt.experimental.dll")
    if bin_directory is None:
        print("Could not locate PixApiCsExt.experimental.dll. Install PIX Preview or set PIX_DIR.")
        sys.exit(1)
    print(f"Using PIX binaries from: {bin_directory}")

    initialize_pythonnet(bin_directory, "PixApiCsExt.experimental.dll")

    # Open the postmortem dump document.
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Internal as pix_internal
    import Microsoft.PIX.Internal.Extension.PostmortemDump as pix_postmortem

    factory = pix_extension.PixApiExtensions.PixCreateFactory[pix_internal.IPixFactoryExperimental]()
    # IPixFactoryExperimental.OpenPostmortemDumpDocument takes a Win32 PCWSTR,
    # which pythonnet cannot construct from a Python str. Call the generated
    # extension method, which exposes a System.String overload.
    document = pix_postmortem.PixApiExtensionsPostmortemDump.OpenPostmortemDumpDocument(
        factory, dump_file_path, None, None, None)

    # Dump all sections.
    dump_metadata(document)
    dump_device_error_bucket(document)
    dump_queues(document)
    dump_page_faults(document)
    dump_resources(document)

    print("\nDone.")


if __name__ == "__main__":
    main()
