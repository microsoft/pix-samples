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
# DrPix Sample (Python)
#
# Demonstrates the PIX Dr. PIX workflow from Python via pythonnet
# using an existing GPU capture:
#   1. Load the PixApiCsExt .NET assembly.
#   2. Create a PIX factory and open the GPU capture provided on the command line.
#   3. Connect analysis and start it (requires Windows Developer Mode).
#   4. Get the Dr. PIX interface.
#   5. Enumerate the available experiments.
#   6. Register a callback that prints when experiment results arrive.
#   7. Run all experiments and wait for completion.
#   8. Print a final summary.
#
# Requirements:
#   - 64-bit Python 3.9+
#   - pythonnet >= 3.0  (pip install pythonnet)
#   - .NET 10 runtime
#   - PIX installed (Preview or Retail), or PIX_DIR environment variable set.
#   - Windows Developer Mode enabled for GPU analysis and Dr. PIX experiments.
#

import ctypes
import inspect
import os
import sys


# Shared PIX-install discovery + pythonnet bootstrap. Sample lives at
# api/{preview,retail}/<sample>/python/main.py, so api/_pix_bootstrap.py
# is three directories up.
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
from _pix_bootstrap import find_pix_bin_directory, initialize_pythonnet

class NativeGuid(ctypes.Structure):
    _fields_ = [
        ("Data1", ctypes.c_uint32),
        ("Data2", ctypes.c_uint16),
        ("Data3", ctypes.c_uint16),
        ("Data4", ctypes.c_ubyte * 8),
    ]

    def to_guid_string(self):
        return (
            f"{{{self.Data1:08x}-{self.Data2:04x}-{self.Data3:04x}-"
            f"{self.Data4[0]:02x}{self.Data4[1]:02x}-"
            f"{self.Data4[2]:02x}{self.Data4[3]:02x}{self.Data4[4]:02x}"
            f"{self.Data4[5]:02x}{self.Data4[6]:02x}{self.Data4[7]:02x}}}"
        )


class NativeExperimentDescription(ctypes.Structure):
    _fields_ = [
        ("Guid", NativeGuid),
        ("Name", ctypes.c_wchar_p),
        ("Category", ctypes.c_wchar_p),
        ("HelpText", ctypes.c_wchar_p),
        ("Source", ctypes.c_int),
    ]


class ExperimentCallback:
    """Python-side callback wrapper for Dr. PIX experiment notifications.

    pythonnet can keep a managed delegate alive for us, so this wrapper uses the
    DelegateExperimentCallback helper from PixApiCsExt and forwards each result
    to a Python instance method that prints a concise notification.
    """

    def __init__(self, pix_module, drpix_module, experiments_by_guid):
        import System

        self._pix = pix_module
        self._drpix = drpix_module
        self._experiments_by_guid = experiments_by_guid
        self._callback_count = 0
        self._implementation = drpix_module.DelegateExperimentCallback()
        self._managed_handler = System.Action[pix_module.IPixGpuCaptureExperimentResult](
            self._on_result_available)
        self._implementation.OnResultAvailable = self._managed_handler

    @property
    def callback_count(self):
        return self._callback_count

    @property
    def implementation(self):
        return self._implementation

    def _on_result_available(self, experiment_result):
        run_parameters = self._drpix.PixApiExtensionsDrPix.GetExperimentRunParams(
            experiment_result)
        experiment_guid = str(run_parameters.ExperimentGuid)
        experiment_info = self._experiments_by_guid.get(experiment_guid)
        experiment_name = (
            experiment_info["name"]
            if experiment_info is not None
            else experiment_guid)

        self._callback_count += 1
        experiment_status = self._drpix.PixApiExtensionsDrPix.GetExperimentStatus(
            experiment_result)

        print(
            f"[Callback {self._callback_count}] {experiment_name}: "
            f"status={format_hresult(experiment_status)}, "
            f"metrics={int(experiment_result.GetExperimentMetricCount())}, "
            f"messages={int(experiment_result.GetExperimentMessageCount())}")


def connect_analysis(capture_document):
    """Get the analysis interface and connect it to the local device."""
    import Microsoft.PIX.Extension.DeviceConnection as pix_device_connection
    import Microsoft.PIX.Extension.GpuCapture as pix_gpu_capture
    import Microsoft.PIX.Extension.GpuCapture.Analysis as pix_gpu_capture_analysis

    analysis = pix_gpu_capture.PixApiExtensionsGpuCapture.GetAnalysis(capture_document)
    analysis_connection_description = pix_device_connection.PIX_CONNECTION_DESC.CreateLocal()
    pix_gpu_capture_analysis.PixApiExtensionsGpuCaptureAnalysis.Connect(
        analysis,
        analysis_connection_description,
        None)
    return analysis


def start_analysis(analysis):
    """Start the analysis session. Developer Mode is required."""
    import Microsoft.PIX.Extension.GpuCapture.Analysis as pix_gpu_capture_analysis

    try:
        pix_gpu_capture_analysis.PixApiExtensionsGpuCaptureAnalysis.StartAnalysis(analysis)
    except Exception as exception:
        developer_mode_not_enabled = -0x75440000  # 0x8abc0000
        feature_requires_developer_mode = -0x7543FFFF  # 0x8abc0001
        hresult = getattr(exception, "HResult", getattr(exception, "hresult", None))
        if hresult in (developer_mode_not_enabled, feature_requires_developer_mode):
            print("ERROR: Windows Developer Mode is required for this operation.")
            print("Enable it in Settings > Privacy & security > For developers,")
            print(
                'or run: reg add "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\'
                'AppModelUnlock" /v AllowDevelopmentWithoutDevLicense /t REG_DWORD /d 1 /f')
        raise


def find_experiment_event_range(capture_document):
    """Return the first and last valid GPU events in the capture."""
    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Extension.GpuCapture as pix_gpu_capture

    queues = pix_gpu_capture.PixApiExtensionsGpuCapture.GetQueues(capture_document)
    invalid_gpu_id = 0xFFFFFFFF
    first_event = pix.PIX_EVENT_INFO()
    last_event = pix.PIX_EVENT_INFO()
    first_event.GpuId = invalid_gpu_id
    found_valid_event = False

    for queue_index in range(int(queues.GetCount())):
        queue_info = pix_extension.PixApiExtensions.Get[pix.IPixGpuCaptureQueueInfo](
            queues,
            queue_index)

        for event_index in range(int(queue_info.GetEventCount())):
            event_info = pix_gpu_capture.PixApiExtensionsGpuCapture.GetEvent(
                queue_info,
                event_index)
            if event_info.GpuId == invalid_gpu_id:
                continue

            if not found_valid_event or event_info.GpuId < first_event.GpuId:
                first_event = event_info

            if not found_valid_event or event_info.GpuId > last_event.GpuId:
                last_event = event_info

            found_valid_event = True

    if not found_valid_event:
        raise RuntimeError(
            "The capture does not contain any valid GPU events for Dr. PIX experiments.")

    return first_event, last_event


def get_experiment_description(dr_pix, pix_module, experiment_index):
    """Call IPixGpuCaptureDrPix::GetExperiment via the COM vtable.

    pythonnet does not project this pointer-based method into a Python-friendly
    signature, so this sample uses ctypes to invoke the COM method directly.
    """
    import clr
    from System.Runtime.InteropServices import Marshal

    interface_type = clr.GetClrType(pix_module.IPixGpuCaptureDrPix)
    interface_pointer = Marshal.GetComInterfaceForObject(dr_pix, interface_type)
    try:
        native_pointer = ctypes.c_void_p(interface_pointer.ToInt64())
        vtable_pointer = ctypes.cast(
            native_pointer,
            ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents

        get_experiment = ctypes.WINFUNCTYPE(
            ctypes.c_long,
            ctypes.c_void_p,
            ctypes.c_ulonglong,
            ctypes.POINTER(NativeExperimentDescription))(vtable_pointer[4])

        experiment_description = NativeExperimentDescription()
        hresult = get_experiment(
            native_pointer,
            experiment_index,
            ctypes.byref(experiment_description))
        if hresult < 0:
            raise RuntimeError(
                f"GetExperiment({experiment_index}) failed: {format_hresult(hresult)}")

        return experiment_description
    finally:
        Marshal.Release(interface_pointer)


def enumerate_experiments(dr_pix, pix_module):
    """Enumerate the Dr. PIX experiments that are available for the capture."""
    import System

    experiment_count = int(dr_pix.GetExperimentCount())
    print(f"Found {experiment_count} Dr. PIX experiment(s):")

    experiments = []
    experiments_by_guid = {}

    for experiment_index in range(experiment_count):
        experiment_description = get_experiment_description(
            dr_pix,
            pix_module,
            experiment_index)
        experiment_guid_string = experiment_description.Guid.to_guid_string()

        try:
            experiment_source = str(pix_module.PIX_EXPERIMENT_SOURCE(
                experiment_description.Source))
        except Exception:
            experiment_source = str(experiment_description.Source)

        experiment_info = {
            "guid": System.Guid(experiment_guid_string),
            "name": experiment_description.Name or "",
            "category": experiment_description.Category or "",
            "helpText": experiment_description.HelpText or "",
            "source": experiment_source,
        }

        experiments.append(experiment_info)
        experiments_by_guid[str(experiment_info["guid"])] = experiment_info

        print(
            f"  [{experiment_index}] {experiment_info['name']} "
            f"({experiment_guid_string})")
        print(f"       Category: {experiment_info['category']}")
        print(f"       Source: {experiment_info['source']}")
        print(f"       Description: {experiment_info['helpText']}")

    return experiments, experiments_by_guid


def build_run_parameters(pix_module, experiments, first_event, last_event):
    """Create PIX_EXPERIMENT_RUN_PARAMS for each available experiment."""
    run_parameters = []
    for experiment in experiments:
        run_parameter = pix_module.PIX_EXPERIMENT_RUN_PARAMS()
        run_parameter.ExperimentGuid = experiment["guid"]
        run_parameter.FirstEvent = first_event
        run_parameter.LastEvent = last_event
        run_parameters.append(run_parameter)
    return run_parameters


def print_summary(run_results, experiments_by_guid, pix_module, pix_extension, drpix_module,
                  callback_count):
    """Print a final summary after all experiment results have been collected."""
    print()
    print("Experiment summary:")
    print(f"  Results returned: {int(run_results.GetCount())}")
    print(f"  Callback count: {callback_count}")

    for result_index in range(int(run_results.GetCount())):
        experiment_result = pix_extension.PixApiExtensions.Get[
            pix_module.IPixGpuCaptureExperimentResult](run_results, result_index)
        run_parameters = drpix_module.PixApiExtensionsDrPix.GetExperimentRunParams(
            experiment_result)
        experiment_info = experiments_by_guid[str(run_parameters.ExperimentGuid)]
        experiment_status = drpix_module.PixApiExtensionsDrPix.GetExperimentStatus(
            experiment_result)

        print(
            f"  - {experiment_info['name']}: "
            f"status={format_hresult(experiment_status)}, "
            f"metrics={int(experiment_result.GetExperimentMetricCount())}, "
            f"messages={int(experiment_result.GetExperimentMessageCount())}")


def release_com_object(com_object):
    """Best-effort COM release for PIX objects held by pythonnet."""
    if com_object is None:
        return

    try:
        from System.Runtime.InteropServices import Marshal
        if Marshal.IsComObject(com_object):
            Marshal.FinalReleaseComObject(com_object)
    except Exception:
        pass


def format_hresult(value):
    """Format a signed or unsigned HRESULT value as 0xXXXXXXXX."""
    return f"0x{(int(value) & 0xFFFFFFFF):08X}"


def main():
    if len(sys.argv) < 2:
        print("Usage: DrPix <path-to-capture.wpix>")
        return 1

    capture_file_path = os.path.abspath(sys.argv[1])
    if not os.path.isfile(capture_file_path):
        print(f"ERROR: Capture file not found: {capture_file_path}")
        return 1

    capture_document = None
    analysis = None
    dr_pix = None
    run_results = None
    analysis_connected = False
    analysis_started = False

    bin_directory = find_pix_bin_directory("PixApiCsExt.dll")
    if bin_directory is None:
        print("Could not locate PixApiCsExt.dll. Install PIX or set PIX_DIR.")
        return 1

    print(f"Using PIX binaries from: {bin_directory}")
    initialize_pythonnet(bin_directory, "PixApiCsExt.dll")

    import Microsoft.PIX as pix
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Extension.DrPix as pix_drpix
    import Microsoft.PIX.Extension.GpuCapture.Analysis as pix_gpu_capture_analysis
    import System

    try:
        # Step 1: Create the PIX factory and open the GPU capture document.
        pix_factory = pix_extension.PixApiExtensions.PixCreateFactory[pix.IPixFactory]()
        capture_document = pix_extension.PixApiExtensions.OpenGpuCaptureDocument[
            pix.IPixGpuCaptureDocument](pix_factory, capture_file_path)

        # Steps 2-3: Connect analysis and start it.
        analysis = connect_analysis(capture_document)
        analysis_connected = True
        start_analysis(analysis)
        analysis_started = True

        dr_pix = pix_gpu_capture_analysis.PixApiExtensionsGpuCaptureAnalysis.GetDrPix(
            analysis)

        # Step 4: Enumerate the available experiments.
        experiments, experiments_by_guid = enumerate_experiments(dr_pix, pix)
        if len(experiments) == 0:
            print("No Dr. PIX experiments are available for this capture.")
            return 0

        first_event, last_event = find_experiment_event_range(capture_document)
        print(
            f"Using GPU event range {int(first_event.GpuId)} -> "
            f"{int(last_event.GpuId)} for experiment execution.")

        # Step 5: Register a callback that prints when experiment results arrive.
        experiment_callback = ExperimentCallback(pix, pix_drpix, experiments_by_guid)

        def handle_status_message(status_message):
            print(f"Status: {status_message}")

        def handle_progress_value(progress_value):
            print(f"Progress: {float(progress_value):.0%}")

        status_handler = System.Action[System.String](handle_status_message)
        progress_handler = System.Action[System.Single](handle_progress_value)
        progress_notifications = pix_extension.ProgressNotificationsHelper(
            status_handler,
            progress_handler)
        cancellation_token = pix_extension.PixApiExtensions.CreateCancellationToken(
            pix_factory)
        run_parameters = build_run_parameters(
            pix,
            experiments,
            first_event,
            last_event)

        # Steps 6-7: Run the experiments and wait for completion.
        print(f"Running {len(run_parameters)} Dr. PIX experiment(s)...")
        print("Waiting for experiment completion...")
        run_results = pix_drpix.PixApiExtensionsDrPix.RunExperiments(
            dr_pix,
            run_parameters,
            experiment_callback.implementation,
            progress_notifications,
            cancellation_token)

        # Step 8: Print a final summary.
        print_summary(
            run_results,
            experiments_by_guid,
            pix,
            pix_extension,
            pix_drpix,
            experiment_callback.callback_count)

        print("DrPix sample completed successfully.")
        return 0
    except Exception as exception:
        hresult = getattr(exception, "HResult", getattr(exception, "hresult", None))
        if hresult is not None:
            print(f"PIX API call failed: {format_hresult(hresult)}")
        print(f"ERROR: {exception}")
        return 1
    finally:
        # Mirror the DrPix C++ variant: explicitly stop and disconnect the
        # analysis session before releasing COM interfaces. Releasing alone
        # does not tear down the device-side session, which can surface as
        # "analysis already running" on back-to-back runs of this sample.
        if analysis is not None:
            if analysis_started:
                try:
                    analysis.StopAnalysis()
                except Exception as cleanup_exception:
                    cleanup_hresult = getattr(cleanup_exception, "HResult", getattr(cleanup_exception, "hresult", None))
                    if cleanup_hresult is None:
                        print("Warning: StopAnalysis failed during cleanup.")
                    else:
                        print(f"Warning: StopAnalysis failed during cleanup ({format_hresult(cleanup_hresult)}).")
            if analysis_connected:
                try:
                    analysis.Disconnect()
                except Exception as cleanup_exception:
                    cleanup_hresult = getattr(cleanup_exception, "HResult", getattr(cleanup_exception, "hresult", None))
                    if cleanup_hresult is None:
                        print("Warning: Disconnect failed during cleanup.")
                    else:
                        print(f"Warning: Disconnect failed during cleanup ({format_hresult(cleanup_hresult)}).")

        release_com_object(run_results)
        release_com_object(dr_pix)
        release_com_object(analysis)
        release_com_object(capture_document)

        run_results = None
        dr_pix = None
        analysis = None
        capture_document = None

        try:
            import gc
            import System
            gc.collect()
            System.GC.Collect()
            System.GC.WaitForPendingFinalizers()
            System.GC.Collect()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
