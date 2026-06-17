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
# Python sample that demonstrates calling the PIX API from Python via
# pythonnet. The script:
#   1. Loads the PixApiCsExt .NET assembly (which wraps the native PIX COM API).
#   2. Launches a command-line-provided D3D12 app under GPU capture and takes a capture.
#   3. Opens the resulting capture and walks queues looking for the
#      "Hello PixApi!!!" event emitted by the target app.
#   4. Connects analysis to localhost and starts it.
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

def take_gpu_capture_of_sample(factory, target_executable_path):
    """Launch the target app under GPU capture and return (capture_path, document).

    Mirrors the C# sample's flow: take the capture, fetch the result for its
    filename, detach, then open the resulting .wpix as a capture document.
    """
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Extension.DeviceConnection as pix_device_connection
    import Windows.Win32.Foundation as wf

    # Use the local-connection helper so this matches start_analysis below and
    # the C++ / C# variants of this sample, which all build the desc with the
    # explicit local form (Type = PIX_CONNECTION_TYPE_LOCAL). Setting IpAddress
    # on a default-constructed desc happens to work today but is the remote
    # union arm and could break if PIX hardens its desc validation.
    connection_desc = pix_device_connection.PIX_CONNECTION_DESC.CreateLocal()

    notifications = pix_device_connection.PixConnectionNotifications()
    connection_document = pix_extension.PixApiExtensions.OpenConnectionDocument[
        pix.IPixConnectionDocument](factory, connection_desc, notifications)

    # Build the launch desc as a ref-passed value type. pythonnet returns
    # (result, refArg) for ref parameters, so we unpack the tuple.
    launch_desc = pix_device_connection.PIX_LAUNCH_PROCESS_DESC()
    launch_desc.launchFlags = pix.PIX_APPLICATION_LAUNCH_FLAGS.PIX_APPLICATION_LAUNCH_FLAG_UNDER_GPU_CAPTURE
    launch_desc.launchMode = pix.PIX_APPLICATION_LAUNCH_MODE.PIX_APPLICATION_LAUNCH_MODE_WIN32_EXECUTABLE

    win32_info = pix_device_connection.PIX_LAUNCH_PROCESS_DESC._launchInfo_e__UnionExt._win32_e__StructExt()
    win32_info.exePath = target_executable_path
    win32_info.commandLineArgs = ""
    win32_info.initialWorkingDirectory = os.path.dirname(target_executable_path)

    launch_info = pix_device_connection.PIX_LAUNCH_PROCESS_DESC._launchInfo_e__UnionExt()
    launch_info.win32 = win32_info
    launch_desc.launchInfo = launch_info

    launch_results, _ = pix_device_connection.PixApiExtensionsDeviceConnection.LaunchProcess[
        pix.IPixLaunchProcessResults](connection_document, launch_desc)

    unsupported_reason = launch_results.GetUnsupportedReason()
    if unsupported_reason == pix.PIX_PROCESS_UNSUPPORTED_REASON.PIX_PROCESS_UNSUPPORTED_REASON_NONE:
        pass
    elif unsupported_reason == pix.PIX_PROCESS_UNSUPPORTED_REASON.PIX_PROCESS_UNSUPPORTED_REASON_NOT_USING_D3D12:
        # Process hasn't started using D3D12 yet - that's expected immediately
        # after launch.
        pass
    elif unsupported_reason == pix.PIX_PROCESS_UNSUPPORTED_REASON.PIX_PROCESS_UNSUPPORTED_REASON_WRONG_ARCHITECTURE:
        print("Launched process is unsupported: PIX_PROCESS_UNSUPPORTED_REASON_WRONG_ARCHITECTURE")
        sys.exit(1)
    elif unsupported_reason == pix.PIX_PROCESS_UNSUPPORTED_REASON.PIX_PROCESS_UNSUPPORTED_REASON_TERMINATED:
        print("Launched process is unsupported: PIX_PROCESS_UNSUPPORTED_REASON_TERMINATED")
        sys.exit(1)

    # Give the launched process a moment to initialize D3D12.
    import time
    time.sleep(2)

    # TakeGpuCaptureResult is an extension method that internally calls
    # TakeGpuCapture (async) then GetResult (blocking wait).
    capture_result = pix_device_connection.PixApiExtensionsDeviceConnection.TakeGpuCaptureResult(
        connection_document, launch_results.GetProcessId())

    capture_path = str(capture_result.GetFilename())
    if not capture_path:
        print("Failed to take GPU capture (empty filename).")
        sys.exit(1)
    print(f"GpuCapture saved to: {capture_path}")

    connection_document.DetachFromAllProcesses(wf.BOOL(True))

    capture_document = pix_extension.PixApiExtensions.OpenGpuCaptureDocument[
        pix.IPixGpuCaptureDocument](factory, capture_path)
    return capture_path, capture_document


def open_and_parse_gpu_capture(capture_document):
    """Walk the capture's queues looking for the 'Hello PixApi!!!' event."""
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Extension.GpuCapture as pix_gpu_capture

    queues = pix_gpu_capture.PixApiExtensionsGpuCapture.GetQueues(capture_document)

    for queue_index in range(queues.GetCount()):
        queue_info = pix_extension.PixApiExtensions.Get[pix.IPixGpuCaptureQueueInfo](
            queues, queue_index)

        if queue_info.GetType() != pix.PIX_QUEUE_TYPE.PIX_QUEUE_TYPE_GRAPHICS:
            continue

        for event_index in range(queue_info.GetEventCount()):
            event_info = pix_gpu_capture.PixApiExtensionsGpuCapture.GetEvent(
                queue_info, event_index)
            event_name = str(event_info.Name)
            if "Hello PixApi!!!" in event_name:
                print(
                    f'Found "Hello PixApi!!!" @QueueIndex {queue_index}, EventIndex {event_index}')
                return

    print('"Hello PixApi!!!" event not found')


def start_analysis(capture_document):
    """Connect analysis to the local device and start it."""
    import Microsoft.PIX.Extension.DeviceConnection as pix_device_connection
    import Microsoft.PIX.Extension.GpuCapture as pix_gpu_capture
    import Microsoft.PIX.Extension.GpuCapture.Analysis as pix_gpu_capture_analysis

    analysis = pix_gpu_capture.PixApiExtensionsGpuCapture.GetAnalysis(capture_document)
    analysis_connection_desc = pix_device_connection.PIX_CONNECTION_DESC.CreateLocal()
    pix_gpu_capture_analysis.PixApiExtensionsGpuCaptureAnalysis.Connect(
        analysis, analysis_connection_desc, None)

    try:
        pix_gpu_capture_analysis.PixApiExtensionsGpuCaptureAnalysis.StartAnalysis(analysis)
    except Exception as ex:
        E_PIX_DEVELOPER_MODE_NOT_ENABLED = -0x7543FFFF - 1  # 0x8abc0000
        E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE = -0x7543FFFF  # 0x8abc0001
        hresult = getattr(ex, "HResult", getattr(ex, "hresult", None))
        if hresult in (E_PIX_DEVELOPER_MODE_NOT_ENABLED, E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE):
            print("ERROR: Windows Developer Mode is required for this operation.")
            print("Enable it in Settings > Privacy & security > For developers,")
            print('or run: reg add "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\'
                  'AppModelUnlock" /v AllowDevelopmentWithoutDevLicense /t REG_DWORD /d 1 /f')
        raise
    finally:
        try:
            analysis.StopAnalysis()
        except Exception:
            pass
        try:
            analysis.Disconnect()
        except Exception:
            pass


def main():
    if len(sys.argv) < 2:
        print("Usage: python main.py <path-to-d3d12-exe>")
        sys.exit(1)

    target_executable_path = sys.argv[1]
    if not os.path.isfile(target_executable_path):
        print(f"File not found: {target_executable_path}")
        sys.exit(1)

    # Step 0: Find PIX binaries and initialize pythonnet.
    bin_directory = find_pix_bin_directory("PixApiCsExt.dll")
    if bin_directory is None:
        print("Could not locate PixApiCsExt.dll. Install PIX or set PIX_DIR.")
        sys.exit(1)
    print(f"Using PIX binaries from: {bin_directory}")

    initialize_pythonnet(bin_directory, "PixApiCsExt.dll")

    # Step 1: Create the PIX factory (entry point for all operations).
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension

    factory = pix_extension.PixApiExtensions.PixCreateFactory[pix.IPixFactory]()

    # Steps 2-3: Launch under GPU capture and take a capture.
    capture_path, capture_document = take_gpu_capture_of_sample(factory, target_executable_path)

    # Step 4: Parse the capture and look for the marker event.
    open_and_parse_gpu_capture(capture_document)

    # Step 5: Start analysis (requires Developer Mode).
    start_analysis(capture_document)


if __name__ == "__main__":
    main()
