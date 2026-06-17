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
# Timing Capture Sample (Python)
#
# Demonstrates the PIX timing capture workflow from Python via pythonnet:
#   1. Create a PIX factory.
#   2. Open a timing capture document from a file path.
#   3. Print the capture path and PIX storage path.
#   4. Optionally resolve symbols when a PDB path is provided.
#   5. Enumerate visible system monitor counters.
#   6. Close the document.
#   7. Print success.
#

from collections import defaultdict
import inspect
import os
import sys


# Shared PIX-install discovery + pythonnet bootstrap. Sample lives at
# api/{preview,retail}/<sample>/python/main.py, so api/_pix_bootstrap.py
# is three directories up.
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
from _pix_bootstrap import find_pix_bin_directory, initialize_pythonnet

def get_string_or_default(value, default_value="(none)"):
    """Normalize PIX string values for display."""
    if value is None:
        return default_value

    resolved_value = str(value)
    return resolved_value if resolved_value else default_value


def build_symbol_settings():
    """Create the symbol resolution settings used by this sample."""
    import Microsoft.PIX as pix

    symbol_settings = pix.TimingCaptureSymbolSettings()
    symbol_settings.IncludeKernelSymbols = False
    symbol_settings.IncludeSourceData = True
    symbol_settings.IncludeTypeData = False
    symbol_settings.UseNTSymbolPath = True
    return symbol_settings


def resolve_symbols(timing_capture_document, full_pdb_path):
    """Resolve timing capture symbols using the managed extension method."""
    import Microsoft.PIX.Extension.TimingCapture as pix_timing_capture

    def handle_status_message(status_message):
        print(f"Status: {status_message}")

    def handle_progress_value(progress_value):
        print(f"Progress: {float(progress_value):.0%}")

    pix_timing_capture.PixApiExtensionsTimingCapture.ResolveSymbols(
        timing_capture_document,
        full_pdb_path,
        build_symbol_settings(),
        handle_status_message,
        handle_progress_value)


def build_system_monitor_counter_group_lookup(counter_descriptions):
    """Build a mapping from counter group ID to the group display name."""
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension.DeviceConnection as pix_device_connection

    counter_group_names_by_id = {}
    counter_group_count = int(counter_descriptions.GetNumCounterGroups())
    for counter_group_index in range(counter_group_count):
        counter_group = pix_device_connection.PixApiExtensionsDeviceConnectionResults.GetCounterGroupDescription[
            pix.IPixSystemMonitorCounterGroup](counter_descriptions, counter_group_index)
        counter_group_names_by_id[int(counter_group.GetCounterGroupId())] = get_string_or_default(
            counter_group.GetName(),
            "(unnamed group)")

    return counter_group_names_by_id


def try_print_system_monitor_counters(pix_factory):
    """Enumerate visible non-internal system monitor counters from a local PIX connection."""
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Extension.DeviceConnection as pix_device_connection

    try:
        connection_description = pix_device_connection.PIX_CONNECTION_DESC.CreateLocal()
        connection_notifications = pix_device_connection.PixConnectionNotifications()
        connection_document = pix_extension.PixApiExtensions.OpenConnectionDocument[
            pix.IPixConnectionDocument](pix_factory, connection_description, connection_notifications)
        counter_descriptions = pix_device_connection.PixApiExtensionsDeviceConnection.GetCounterDescriptions[
            pix.IPixGetCounterDescriptionsResults](connection_document)
        counter_group_names_by_id = build_system_monitor_counter_group_lookup(counter_descriptions)

        visible_counters_by_group_id = defaultdict(list)
        total_counter_count = int(counter_descriptions.GetNumCounters())
        visible_counter_count = 0

        for counter_index in range(total_counter_count):
            counter_description = pix_device_connection.PixApiExtensionsDeviceConnectionResults.GetCounterDescription[
                pix.IPixSystemMonitorCounter](counter_descriptions, counter_index)
            if not bool(counter_description.GetIsVisible()) or bool(counter_description.GetIsInternal()):
                continue

            counter_group_id = int(counter_description.GetCounterGroupId())
            visible_counters_by_group_id[counter_group_id].append((
                get_string_or_default(counter_description.GetDisplayName(), "(unnamed counter)"),
                get_string_or_default(counter_description.GetUnits()),
                float(counter_description.GetDefinedMin()),
                float(counter_description.GetDefinedMax())))
            visible_counter_count += 1

        print(f"System monitor counters: {visible_counter_count} visible of {total_counter_count} total.")
        if not visible_counters_by_group_id:
            print("No visible non-internal system monitor counters were found.")
            return

        for counter_group_id in sorted(visible_counters_by_group_id):
            counter_descriptions_for_group = visible_counters_by_group_id[counter_group_id]
            counter_group_name = counter_group_names_by_id.get(counter_group_id, "(group unavailable)")
            print(
                f"  Group {counter_group_id}: {counter_group_name} "
                f"({len(counter_descriptions_for_group)} counters)")

            for display_name, units, defined_min, defined_max in counter_descriptions_for_group:
                print(f"    {display_name}")
                print(f"      GroupId: {counter_group_id}")
                print(f"      Units: {units}")
                print(f"      Range: {defined_min:.3f} to {defined_max:.3f}")
    except NotImplementedError as exception:
        print(f"WARNING: System monitor counter enumeration is unavailable: {exception}")
    except Exception as exception:
        print(f"WARNING: System monitor counter enumeration is unavailable: {exception}")


def main():
    if len(sys.argv) < 2 or len(sys.argv) > 3:
        print("Usage: python main.py <capture-path> [full-pdb-path]")
        return 1

    capture_file_path = os.path.abspath(sys.argv[1])
    full_pdb_path = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else None

    if not os.path.isfile(capture_file_path):
        print(f"Capture file not found: {capture_file_path}")
        return 1

    if full_pdb_path is not None and not os.path.isfile(full_pdb_path):
        print(f"PDB file not found: {full_pdb_path}")
        return 1

    bin_directory = find_pix_bin_directory("PixApiCsExt.dll")
    if bin_directory is None:
        print("Could not locate PixApiCsExt.dll. Install PIX or set PIX_DIR.")
        return 1

    initialize_pythonnet(bin_directory, "PixApiCsExt.dll")

    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension

    timing_capture_document = None

    try:
        # Step 1: Create the PIX factory (entry point for all operations).
        pix_factory = pix_extension.PixApiExtensions.PixCreateFactory[pix.IPixFactory]()

        # Step 2: Open the timing capture document from disk.
        timing_capture_document = pix_extension.PixApiExtensions.OpenTimingCaptureDocument[
            pix.IPixTimingCaptureDocument](pix_factory, capture_file_path)

        # Step 3: Print basic information about the opened capture.
        print(f"CapturePath: {timing_capture_document.GetCapturePath()}")
        print(f"PixStoragePath: {timing_capture_document.GetPixStoragePath()}")

        # Step 4: Optionally resolve symbols using the provided PDB path.
        if full_pdb_path is not None:
            print(f"Resolving symbols using: {full_pdb_path}")
            resolve_symbols(timing_capture_document, full_pdb_path)
        else:
            print("Skipping symbol resolution.")

        # Step 5: Enumerate available system monitor counters.
        try_print_system_monitor_counters(pix_factory)

        # Step 6: Close the document.
        timing_capture_document.Close()
        timing_capture_document = None

        # Step 7: Print success.
        print("Timing capture sample completed successfully.")
        return 0
    except Exception as exception:
        hresult = getattr(exception, "HResult", getattr(exception, "hresult", None))
        if hresult is not None:
            print(f"PIX API call failed: 0x{(hresult & 0xFFFFFFFF):08X}")
        print(f"ERROR: {exception}")
        return 1
    finally:
        if timing_capture_document is not None:
            try:
                timing_capture_document.Close()
            except Exception as close_exception:
                print(f"WARNING: Failed to close timing capture document: {close_exception}")


if __name__ == "__main__":
    sys.exit(main())
