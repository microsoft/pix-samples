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
# Pipeline Inspection Sample (Python)
#
# Demonstrates how to inspect pipeline states and root signatures from an
# existing PIX GPU capture:
#   1. Create a PIX factory.
#   2. Open a GPU capture document.
#   3. Get the analysis interface.
#   4. Connect to the local GPU.
#   5. Start analysis (requires Windows Developer Mode).
#   6. Enumerate queue events.
#   7. For each draw or dispatch event, get the pipeline state.
#   8. Print pipeline state subobjects and root signature parameters.
#   9. Stop analysis and disconnect.
#
# Usage: python main.py <path-to-gpu-capture-file>
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


def get_string_or_default(value, default_value):
    text = str(value) if value is not None else ""
    return text if text else default_value


def unwrap_nullable(value):
    if value is None:
        return None

    has_value = getattr(value, "HasValue", None)
    if has_value is None:
        return value

    if not value.HasValue:
        return None

    return value.Value


def get_program_state(capture_document, event_info, pix_gpu_capture):
    result = pix_gpu_capture.PixApiExtensionsGpuCapture.GetProgramState(capture_document, event_info)
    if isinstance(result, tuple):
        return result[0]
    return result


def print_descriptor_ranges(descriptor_ranges):
    for range_index, descriptor_range in enumerate(descriptor_ranges):
        print(
            f"        Range {range_index}: {descriptor_range.RangeType}, "
            f"Register={descriptor_range.BaseShaderRegister}, "
            f"Space={descriptor_range.RegisterSpace}, "
            f"Descriptors={descriptor_range.NumDescriptors}")


def print_root_parameters(root_parameters):
    print(f"    Root parameters: {len(root_parameters)}")

    for parameter_index, root_parameter in enumerate(root_parameters):
        print(
            f"      [{parameter_index}] Type={root_parameter.Type}, "
            f"Visibility={root_parameter.ShaderVisibility}")

        descriptor_table = unwrap_nullable(root_parameter.DescriptorTable)
        if descriptor_table is not None:
            print(f"        Descriptor ranges: {descriptor_table.NumDescriptorRanges}")
            print_descriptor_ranges(descriptor_table.DescriptorRanges)
            continue

        root_descriptor = unwrap_nullable(root_parameter.Descriptor)
        if root_descriptor is not None:
            print(
                f"        Register={root_descriptor.ShaderRegister}, "
                f"Space={root_descriptor.RegisterSpace}")
            continue

        constants = unwrap_nullable(root_parameter.Constant)
        if constants is not None:
            print(
                f"        Register={constants.ShaderRegister}, "
                f"Space={constants.RegisterSpace}, "
                f"Values={constants.Num32BitValues}")


def print_root_signature(root_signature, pix_resources):
    root_signature_name = get_string_or_default(
        root_signature.GetName(),
        "(unnamed root signature)")
    print(f"  Global root signature: {root_signature_name}")
    print(f"    ApiObjectId: {root_signature.GetApiObjectId()}")

    versioned_desc = pix_resources.PixApiExtensionsGpuCaptureResources.GetVersionedDesc(root_signature)
    if versioned_desc.Version.name in ("D3D_ROOT_SIGNATURE_VERSION_1", "D3D_ROOT_SIGNATURE_VERSION_1_0"):
        print("    Version: 1.0")
        print_root_parameters(
            pix_resources.PixApiExtensionsGpuCaptureResources.GetRootParameters(versioned_desc))
        print(f"    Static samplers: {versioned_desc.Anonymous.Desc_1_0.NumStaticSamplers}")
    elif versioned_desc.Version.name == "D3D_ROOT_SIGNATURE_VERSION_1_1":
        print("    Version: 1.1")
        print_root_parameters(
            pix_resources.PixApiExtensionsGpuCaptureResources.GetRootParameters1(versioned_desc))
        print(f"    Static samplers: {versioned_desc.Anonymous.Desc_1_1.NumStaticSamplers}")
    elif versioned_desc.Version.name == "D3D_ROOT_SIGNATURE_VERSION_1_2":
        print("    Version: 1.2")
        print_root_parameters(
            pix_resources.PixApiExtensionsGpuCaptureResources.GetRootParameters1(versioned_desc))
        print(f"    Static samplers: {versioned_desc.Anonymous.Desc_1_2.NumStaticSamplers}")
    else:
        print(f"    Version: {versioned_desc.Version}")


def print_pipeline_state(pipeline_state, pix_resources):
    subobjects = pix_resources.PixApiExtensionsGpuCaptureResources.GetSubobjects(pipeline_state)
    print(f"  Pipeline state subobjects: {len(subobjects)}")

    for subobject_index, subobject in enumerate(subobjects):
        print(f"    [{subobject_index}] {subobject.Type}")


def inspect_pipeline_events(capture_document):
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Extension.GpuCapture as pix_gpu_capture
    import Microsoft.PIX.Extension.GpuCapture.Resources as pix_resources

    # Cap inspection so the sample finishes in seconds even on very large captures
    # (e.g. 37 MB VRS captures with hundreds of thousands of events).
    MAX_EVENTS_TO_INSPECT = 16

    queues = pix_gpu_capture.PixApiExtensionsGpuCapture.GetQueues(capture_document)
    inspected_event_count = 0

    for queue_index in range(queues.GetCount()):
        if inspected_event_count >= MAX_EVENTS_TO_INSPECT:
            break
        queue_info = pix_extension.PixApiExtensions.Get[pix.IPixGpuCaptureQueueInfo](queues, queue_index)

        for event_index in range(queue_info.GetEventCount()):
            if inspected_event_count >= MAX_EVENTS_TO_INSPECT:
                break
            event_info = pix_gpu_capture.PixApiExtensionsGpuCapture.GetEvent(queue_info, event_index)

            try:
                program_state = get_program_state(capture_document, event_info, pix_gpu_capture)
            except Exception:
                continue

            try:
                program_type = unwrap_nullable(
                    pix_resources.PixApiExtensionsGpuCaptureResources.GetGpuProgramType(program_state))
            except Exception:
                continue
            if program_type != pix.D3D12_PROGRAM_TYPE.D3D12_PROGRAM_TYPE_GENERIC_PIPELINE:
                continue

            try:
                generic_pipeline = pix_resources.PixApiExtensionsGpuCaptureResources.GetGpuProgram[
                    pix.IPixGenericPipeline](program_state)
                # GetPipelineType can fail for events that report a generic-pipeline
                # program type but whose underlying pipeline state is not yet bound
                # (seen in raytracing-only captures). Validate up-front and skip such
                # events instead of partially printing them.
                pipeline_type = pix_resources.PixApiExtensionsGpuCaptureResources.GetPipelineType(generic_pipeline)
                pipeline_state = pix_resources.PixApiExtensionsGpuCaptureResources.GetPipelineState(generic_pipeline)
            except Exception:
                continue

            inspected_event_count += 1
            queue_name = get_string_or_default(queue_info.GetName(), "(unnamed queue)")
            event_name = get_string_or_default(event_info.Name, "(unnamed event)")

            print(f"Queue {queue_info.GetId()} ({queue_name})")
            print(f"  Event {event_index}: {event_name}")
            print(f"  Pipeline type: {pipeline_type}")

            print_pipeline_state(pipeline_state, pix_resources)

            try:
                root_signature = pix_resources.PixApiExtensionsGpuCaptureResources.GetGlobalRootSignature(
                    generic_pipeline)
                print_root_signature(root_signature, pix_resources)
            except Exception as ex:
                hresult = getattr(ex, "HResult", getattr(ex, "hresult", None))
                if hresult is None:
                    print("  Global root signature: unavailable")
                else:
                    print(f"  Global root signature unavailable (0x{(hresult & 0xFFFFFFFF):08X}).")

            print()

    if inspected_event_count == 0:
        print("No draw or dispatch events with generic pipeline state were found.")
    elif inspected_event_count >= MAX_EVENTS_TO_INSPECT:
        print(f"(Stopped after inspecting {MAX_EVENTS_TO_INSPECT} events.)")


def main():
    if len(sys.argv) < 2:
        print("Usage: python main.py <path-to-gpu-capture-file>")
        return 1

    capture_file_path = sys.argv[1]
    if not os.path.isfile(capture_file_path):
        print(f"GPU capture file not found: {capture_file_path}", file=sys.stderr)
        return 1

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
        # Step 1: Create the PIX factory.
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

        # Steps 6-8: Enumerate queue events and inspect their pipeline state.
        inspect_pipeline_events(capture_document)
        return 0
    except Exception as ex:
        hresult = getattr(ex, "HResult", getattr(ex, "hresult", None))
        if hresult is not None:
            print_developer_mode_help_if_needed(hresult)
            print(f"COM error: 0x{(hresult & 0xFFFFFFFF):08X}", file=sys.stderr)
        print(str(ex), file=sys.stderr)
        return 1
    finally:
        # Step 9: Stop analysis and disconnect.
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
