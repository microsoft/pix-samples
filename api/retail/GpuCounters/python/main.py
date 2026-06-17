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
# GPU Counters Sample (Python)
#
# Demonstrates the PIX GPU performance analysis workflow for an existing GPU capture:
#   1. Create a PIX factory.
#   2. Open a GPU capture document.
#   3. Get the analysis interface.
#   4. Connect to the local GPU.
#   5. Start analysis (requires Windows Developer Mode).
#   6. Get the GPU counters interface.
#   7. Enumerate available counters and counter groups.
#   8. Print counter display name, group, and units/data type.
#   9. Collect GPU occupancy data.
#   10. Collect high-frequency counters.
#   11. Read per-event GPU timing data.
#   12. Stop analysis and disconnect.
#
# Usage: python main.py <path-to-gpu-capture-file>
#
# Requirements:
#   - 64-bit Python 3.9+
#   - pythonnet >= 3.0  (pip install pythonnet)
#   - .NET 10 runtime
#   - PIX installed (Preview or Retail), or PIX_DIR environment variable set.
#

import inspect
import os
import sys


# Shared PIX-install discovery + pythonnet bootstrap. Sample lives at
# api/{preview,retail}/<sample>/python/main.py, so api/_pix_bootstrap.py
# is three directories up.
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
from _pix_bootstrap import find_pix_bin_directory, initialize_pythonnet

E_PIX_DEVELOPER_MODE_NOT_ENABLED = -1967390720
E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE = -1967390719
PIX_EVENT_TIMING_NONE = (1 << 64) - 1


def print_developer_mode_help_if_needed(hresult):
    if hresult in (
            E_PIX_DEVELOPER_MODE_NOT_ENABLED,
            E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE):
        print("ERROR: Windows Developer Mode is required for this operation.", file=sys.stderr)
        print("Enable it in Settings > Privacy & security > For developers,", file=sys.stderr)
        print(
            'or run: reg add "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock" '
            '/v AllowDevelopmentWithoutDevLicense /t REG_DWORD /d 1 /f',
            file=sys.stderr)


def print_optional_feature_unavailable(feature_name, exception):
    hresult = getattr(exception, "HResult", getattr(exception, "hresult", None))
    if hresult is None:
        print(f"{feature_name} is not supported or unavailable for this capture on the current hardware.")
    else:
        print(
            f"{feature_name} is not supported or unavailable for this capture on the current hardware "
            f"(0x{(hresult & 0xFFFFFFFF):08X}).")


def get_clr_guid(type_object):
    import clr
    return clr.GetClrType(type_object).GUID


def build_counter_group_lookup(gpu_counters):
    """Build a mapping from counter ID to the group names that contain it."""
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Extension.GpuCapture.Analysis.Counters as pix_gpu_capture_counters

    counter_group_names_by_id = {}
    counter_groups = pix_gpu_capture_counters.PixApiExtensionsGpuCaptureCounters.GetCounterGroups(gpu_counters)
    print(f"Available GPU counter groups: {counter_groups.GetCount()}")

    for group_index in range(counter_groups.GetCount()):
        counter_group_description = pix_extension.PixApiExtensions.Get[
            pix.IPixGpuCounterGroupDescription](counter_groups, group_index)
        group_name = str(counter_group_description.GetName() or "(unnamed group)")
        group_counters = pix_gpu_capture_counters.PixApiExtensionsGpuCaptureCounters.GetCounters(
            counter_group_description)

        print(f"  - {group_name} ({group_counters.GetCount()} counters)")

        for counter_index in range(group_counters.GetCount()):
            counter_description = pix_extension.PixApiExtensions.Get[
                pix.IPixGpuCounterDescription](group_counters, counter_index)
            counter_id = int(counter_description.GetId())
            counter_group_names = counter_group_names_by_id.setdefault(counter_id, [])
            if group_name not in counter_group_names:
                counter_group_names.append(group_name)

    return {
        counter_id: ", ".join(counter_group_names)
        for counter_id, counter_group_names in counter_group_names_by_id.items()
    }


def print_available_counters(gpu_counters):
    """Enumerate and print counter display name, group, and units/data type."""
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Extension.GpuCapture.Analysis.Counters as pix_gpu_capture_counters

    counter_group_names_by_id = build_counter_group_lookup(gpu_counters)
    counters = pix_gpu_capture_counters.PixApiExtensionsGpuCaptureCounters.GetCounters(gpu_counters)

    print(f"Available GPU counters: {counters.GetCount()}")
    print()

    for counter_index in range(counters.GetCount()):
        counter_description = pix_extension.PixApiExtensions.Get[
            pix.IPixGpuCounterDescription](counters, counter_index)
        display_name = str(counter_description.GetName() or "(unnamed counter)")
        group_name = counter_group_names_by_id.get(
            int(counter_description.GetId()),
            "(group unavailable)")

        # The retail GPU counter description exposes a format/data-type enum rather
        # than a separate units string, so print that value in the Units field.
        units = str(counter_description.GetDataType())

        print(f"[{counter_index}] {display_name}")
        print(f"    Group: {group_name}")
        print(f"    Units: {units}")


def find_preferred_queue(capture_document):
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Extension.GpuCapture as pix_gpu_capture

    preferred_queue = None
    queues = pix_gpu_capture.PixApiExtensionsGpuCapture.GetQueues(capture_document)
    for queue_index in range(queues.GetCount()):
        queue_info = pix_extension.PixApiExtensions.Get[pix.IPixGpuCaptureQueueInfo](queues, queue_index)
        if queue_info.GetType() == pix.PIX_QUEUE_TYPE.PIX_QUEUE_TYPE_GRAPHICS:
            return queue_info

        if preferred_queue is None and queue_info.GetType() != pix.PIX_QUEUE_TYPE.PIX_QUEUE_TYPE_CPU:
            preferred_queue = queue_info

    return preferred_queue


def print_occupancy_data(analysis):
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension

    print()
    print("GPU occupancy:")

    try:
        occupancy = pix._IPixGpuCaptureAnalysis_Extensions.GetOccupancy(
            analysis,
            get_clr_guid(pix.IPixGpuCaptureOccupancy))
        occupancy_types = pix._IPixGpuCaptureOccupancy_Extensions.GetOccupancyTypes(
            occupancy,
            get_clr_guid(pix.IPixCollection))
        occupancy_stages = pix._IPixGpuCaptureOccupancy_Extensions.GetOccupancyStages(
            occupancy,
            get_clr_guid(pix.IPixCollection))

        print(f"Available occupancy types: {occupancy_types.GetCount()}")
        for type_index in range(occupancy_types.GetCount()):
            occupancy_type = pix_extension.PixApiExtensions.Get[
                pix.IPixGpuCaptureOccupancyType](occupancy_types, type_index)
            print(
                f"  [{type_index}] {str(occupancy_type.GetName() or '(unnamed occupancy type)')} "
                f"(max slots: {occupancy_type.GetMaxSlots()})")

        print(f"Available occupancy stages: {occupancy_stages.GetCount()}")
        for stage_index in range(occupancy_stages.GetCount()):
            occupancy_stage = pix_extension.PixApiExtensions.Get[
                pix.IPixGpuCaptureOccupancyStage](occupancy_stages, stage_index)
            print(
                f"  [{stage_index}] {str(occupancy_stage.GetName() or '(unnamed occupancy stage)')} "
                f"({str(occupancy_stage.GetAbbreviation() or '?')})")

        if occupancy_types.GetCount() == 0 or occupancy_stages.GetCount() == 0:
            print("No occupancy types or stages were reported for this capture analysis.")
            return

        occupancy_data = pix._IPixGpuCaptureOccupancy_Extensions.CollectOccupancy(
            occupancy,
            get_clr_guid(pix.IPixGpuCaptureOccupancyData))

        for type_index in range(occupancy_types.GetCount()):
            occupancy_type = pix_extension.PixApiExtensions.Get[
                pix.IPixGpuCaptureOccupancyType](occupancy_types, type_index)
            type_name = str(occupancy_type.GetName() or "(unnamed occupancy type)")

            for stage_index in range(occupancy_stages.GetCount()):
                occupancy_stage = pix_extension.PixApiExtensions.Get[
                    pix.IPixGpuCaptureOccupancyStage](occupancy_stages, stage_index)
                stage_name = str(occupancy_stage.GetName() or "(unnamed occupancy stage)")
                point_count, _ = pix._IPixGpuCaptureOccupancyData_Extensions.GetPoints(
                    occupancy_data,
                    occupancy_type,
                    occupancy_stage)
                if point_count > 0:
                    print(f"Collected {point_count} occupancy points for {type_name} / {stage_name}.")
                    return

        print("Occupancy data was collected, but no sample points were returned.")
    except Exception as ex:
        print_optional_feature_unavailable("GPU occupancy", ex)


def print_high_frequency_counter_data(analysis):
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension

    print()
    print("High-frequency counters:")

    try:
        high_frequency_counters = pix._IPixGpuCaptureAnalysis_Extensions.GetHighFrequencyCounters(
            analysis,
            get_clr_guid(pix.IPixGpuCaptureHighFrequencyCounters))
        counters = pix._IPixGpuCaptureHighFrequencyCounters_Extensions.GetCounters(
            high_frequency_counters,
            get_clr_guid(pix.IPixCollection))
        counter_groups = pix._IPixGpuCaptureHighFrequencyCounters_Extensions.GetCounterGroups(
            high_frequency_counters,
            get_clr_guid(pix.IPixCollection))
        counter_sets = pix._IPixGpuCaptureHighFrequencyCounters_Extensions.GetGpuCounterSets(
            high_frequency_counters,
            get_clr_guid(pix.IPixCollection))

        print(f"Available high-frequency counters: {counters.GetCount()}")
        for counter_index in range(min(counters.GetCount(), 8)):
            counter = pix_extension.PixApiExtensions.Get[
                pix.IPixGpuCaptureHighFrequencyCounter](counters, counter_index)
            print(f"  [{counter_index}] {str(counter.GetName() or '(unnamed high-frequency counter)')}")

        print(f"Available high-frequency counter groups: {counter_groups.GetCount()}")
        for group_index in range(counter_groups.GetCount()):
            counter_group = pix_extension.PixApiExtensions.Get[
                pix.IPixGpuCaptureCounterCollection](counter_groups, group_index)
            print(
                f"  [{group_index}] {str(counter_group.GetName() or '(unnamed high-frequency counter group)')} "
                f"({counter_group.GetCount()} counters)")

        print(f"Available high-frequency counter sets: {counter_sets.GetCount()}")
        for set_index in range(counter_sets.GetCount()):
            counter_set = pix_extension.PixApiExtensions.Get[
                pix.IPixGpuCaptureCounterCollection](counter_sets, set_index)
            print(
                f"  [{set_index}] {str(counter_set.GetName() or '(unnamed high-frequency counter set)')} "
                f"({counter_set.GetCount()} counters)")

        if counter_sets.GetCount() == 0:
            print("No high-frequency counter sets were reported for this capture analysis.")
            return

        counter_data = pix._IPixGpuCaptureHighFrequencyCounters_Extensions.CollectCounterData(
            high_frequency_counters,
            counter_sets,
            get_clr_guid(pix.IPixGpuCaptureHighFrequencyCounterData))
        first_counter_set = pix_extension.PixApiExtensions.Get[
            pix.IPixGpuCaptureCounterCollection](counter_sets, 0)
        if first_counter_set.GetCount() == 0:
            print("The first high-frequency counter set is empty.")
            return

        first_counter = pix_extension.PixApiExtensions.Get[
            pix.IPixGpuCaptureHighFrequencyCounter](first_counter_set, 0)
        batch_id, sample_count, _, _ = pix._IPixGpuCaptureHighFrequencyCounterData_Extensions.GetSamples(
            counter_data,
            first_counter_set,
            first_counter)
        print(
            f"Collected {sample_count} high-frequency samples for set "
            f"'{str(first_counter_set.GetName() or '(unnamed high-frequency counter set)')}' and counter "
            f"'{str(first_counter.GetName() or '(unnamed high-frequency counter)')}' (batch {batch_id}).")

        if sample_count == 0:
            print("No high-frequency samples were returned for the first counter set.")
    except Exception as ex:
        print_optional_feature_unavailable("High-frequency counters", ex)


def format_timing_value(value):
    if value == PIX_EVENT_TIMING_NONE:
        return "(none)"

    return str(value)


def print_per_event_timing_data(capture_document, analysis):
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension.GpuCapture.Analysis as pix_gpu_capture_analysis
    import Microsoft.PIX.Extension.GpuCapture.Analysis.Timing as pix_gpu_capture_timing

    print()
    print("Per-event GPU timing:")

    try:
        timing_data = pix_gpu_capture_analysis.PixApiExtensionsGpuCaptureAnalysis.CollectTiming[
            pix.IPixGpuCaptureTiming](analysis)
        preferred_queue = find_preferred_queue(capture_document)
        if preferred_queue is None:
            print("No GPU queue was found in the capture, so timing values were not collected.")
            return

        queue_name = str(preferred_queue.GetName() or "(unnamed queue)")
        queue_timing_count = pix_gpu_capture_timing.PixApiExtensionsGpuCaptureTiming.GetQueueDataCount(
            timing_data,
            preferred_queue)
        print(f"Queue '{queue_name}' has {queue_timing_count} timing records.")

        printed_timing_events = 0
        for event_index in range(preferred_queue.GetEventCount()):
            if printed_timing_events >= 5:
                break

            event_info = preferred_queue.GetEvent(event_index)
            if not pix_gpu_capture_timing.PixApiExtensionsGpuCaptureTiming.HasEventData(timing_data, event_info):
                continue

            event_timing = pix_gpu_capture_timing.PixApiExtensionsGpuCaptureTiming.GetEventData(
                timing_data,
                event_info)
            top_end = (
                PIX_EVENT_TIMING_NONE
                if event_timing.TopStart == PIX_EVENT_TIMING_NONE or event_timing.TopDuration == PIX_EVENT_TIMING_NONE
                else event_timing.TopStart + event_timing.TopDuration)
            eop_end = (
                PIX_EVENT_TIMING_NONE
                if event_timing.EopStart == PIX_EVENT_TIMING_NONE or event_timing.EopDuration == PIX_EVENT_TIMING_NONE
                else event_timing.EopStart + event_timing.EopDuration)

            print(f"  [{event_index}] {str(event_info.Name or '(unnamed event)')}")
            print(
                f"      Top: start={format_timing_value(event_timing.TopStart)} ns "
                f"duration={format_timing_value(event_timing.TopDuration)} ns "
                f"end={format_timing_value(top_end)} ns")
            print(
                f"      EOP: start={format_timing_value(event_timing.EopStart)} ns "
                f"duration={format_timing_value(event_timing.EopDuration)} ns "
                f"end={format_timing_value(eop_end)} ns")
            printed_timing_events += 1

        if printed_timing_events == 0:
            print("No per-event GPU timing data was reported for the selected queue.")
    except Exception as ex:
        print_optional_feature_unavailable("Per-event GPU timing", ex)


def main():
    if len(sys.argv) < 2:
        print("Usage: python main.py <path-to-gpu-capture-file>")
        return 1

    capture_file_path = sys.argv[1]
    if not os.path.isfile(capture_file_path):
        print(f"GPU capture file not found: {capture_file_path}", file=sys.stderr)
        return 1

    # Step 0: Find PIX binaries and initialize pythonnet.
    bin_directory = find_pix_bin_directory("PixApiCsExt.dll")
    if bin_directory is None:
        print("Could not locate PixApiCsExt.dll. Install PIX or set PIX_DIR.", file=sys.stderr)
        return 1
    print(f"Using PIX binaries from: {bin_directory}")

    initialize_pythonnet(bin_directory, "PixApiCsExt.dll")

    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Extension.DeviceConnection as pix_device_connection
    import Microsoft.PIX.Extension.GpuCapture as pix_gpu_capture
    import Microsoft.PIX.Extension.GpuCapture.Analysis as pix_gpu_capture_analysis

    analysis = None
    analysis_connected = False
    analysis_started = False

    try:
        # Step 1: Create the PIX factory (entry point for all operations).
        factory = pix_extension.PixApiExtensions.PixCreateFactory[pix.IPixFactory]()

        # Step 2: Open the GPU capture document.
        capture_document = pix_extension.PixApiExtensions.OpenGpuCaptureDocument[
            pix.IPixGpuCaptureDocument](factory, capture_file_path)

        # Step 3: Get the analysis interface.
        analysis = pix_gpu_capture.PixApiExtensionsGpuCapture.GetAnalysis(capture_document)

        # Step 4: Connect analysis to the local GPU.
        analysis_connection_desc = pix_device_connection.PIX_CONNECTION_DESC.CreateLocal()
        pix_gpu_capture_analysis.PixApiExtensionsGpuCaptureAnalysis.Connect(
            analysis,
            analysis_connection_desc,
            None)
        analysis_connected = True

        # Step 5: Start analysis (Windows Developer Mode is required).
        pix_gpu_capture_analysis.PixApiExtensionsGpuCaptureAnalysis.StartAnalysis(analysis)
        analysis_started = True

        # Step 6: Get the GPU counters interface.
        gpu_counters = pix_gpu_capture_analysis.PixApiExtensionsGpuCaptureAnalysis.GetGpuCounters(analysis)

        # Steps 7-8: Enumerate counters and print their details.
        print_available_counters(gpu_counters)

        # Step 9: Collect GPU occupancy.
        print_occupancy_data(analysis)

        # Step 10: Collect high-frequency counters.
        print_high_frequency_counter_data(analysis)

        # Step 11: Read per-event GPU timing.
        print_per_event_timing_data(capture_document, analysis)

        print()
        print("Finished collecting GPU counters, occupancy, high-frequency counters, and timing data.")
        return 0
    except Exception as ex:
        hresult = getattr(ex, "HResult", getattr(ex, "hresult", None))
        if hresult is not None:
            print_developer_mode_help_if_needed(hresult)
            print(f"COM error: 0x{(hresult & 0xFFFFFFFF):08X}", file=sys.stderr)
        print(str(ex), file=sys.stderr)
        return 1
    finally:
        # Step 12: Stop analysis and disconnect.
        if analysis is not None:
            if analysis_started:
                try:
                    analysis.StopAnalysis()
                except Exception as ex:
                    cleanup_hresult = getattr(ex, "HResult", getattr(ex, "hresult", None))
                    if cleanup_hresult is None:
                        print("Warning: StopAnalysis failed during cleanup.", file=sys.stderr)
                    else:
                        print(
                            f"Warning: StopAnalysis failed during cleanup (0x{(cleanup_hresult & 0xFFFFFFFF):08X}).",
                            file=sys.stderr)

            if analysis_connected:
                try:
                    analysis.Disconnect()
                except Exception as ex:
                    cleanup_hresult = getattr(ex, "HResult", getattr(ex, "hresult", None))
                    if cleanup_hresult is None:
                        print("Warning: Disconnect failed during cleanup.", file=sys.stderr)
                    else:
                        print(
                            f"Warning: Disconnect failed during cleanup (0x{(cleanup_hresult & 0xFFFFFFFF):08X}).",
                            file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())