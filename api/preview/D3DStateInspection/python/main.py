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
# Program State Inspection Sample (Python)
#
# Opens a .wpix GPU capture, starts analysis, finds the first
# Draw / Dispatch / DispatchRays / DispatchMesh event, and prints the
# program / pipeline state at that event:
#   - Program type (Graphics, Compute, Mesh, Raytracing, etc.)
#   - Global root signature (name, ApiObjectId)
#   - Pipeline state subobjects (count + per-subobject types)
#   - Shaders bound to the program (per-stage IDs)
#
# Uses IPixGpuCaptureDocument.GetProgramState(eventInfo) -- the public,
# retail-shippable entry point that returns IPixProgramState ->
# IPixGpuProgram / IPixGenericPipeline (root signature, pipeline state,
# shaders). This works against any GPU capture that contains at least
# one supported program-driven event, including non-graphics events
# (Dispatch / DispatchRays / DispatchMesh).
#
# Runtime D3D state (viewports, scissor rects, vertex/index buffers,
# root parameter values bound at the time of the draw) is not part of
# program/pipeline state and lives on a separate IPixD3DState API path
# reachable from postmortem dumps; it is intentionally outside this
# sample's scope.
#
# Requirements:
#   - 64-bit Python 3.9+
#   - pythonnet >= 3.0  (pip install pythonnet)
#   - .NET 10 runtime
#   - PIX installed (Preview), or PIX_DIR environment variable set.
#   - Windows Developer Mode enabled for GPU analysis.
#

import os
import sys


# Shared PIX-install discovery + pythonnet bootstrap. Sample lives at
# api/{preview,retail}/<sample>/python/main.py, so api/_pix_bootstrap.py
# is three directories up.
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
from _pix_bootstrap import find_pix_bin_directory, initialize_pythonnet


def find_first_gpu_program_event(capture_document, pix_namespace, pix_extension, pix_gpu_capture):
    """Find the first Draw / Dispatch / DispatchRays / DispatchMesh event.

    Per IPixProgramState's contract, the program-state API returns failure
    for anything outside this set, so we filter to those event-name prefixes
    here rather than letting GetProgramState throw later.
    """
    program_event_prefixes = ("Draw", "Dispatch")
    queues = pix_gpu_capture.PixApiExtensionsGpuCapture.GetQueues(capture_document)

    for queue_index in range(queues.GetCount()):
        queue_info = pix_extension.PixApiExtensions.Get[pix_namespace.IPixGpuCaptureQueueInfo](queues, queue_index)
        queue_type = queue_info.GetType()
        if (queue_type != pix_namespace.PIX_QUEUE_TYPE.PIX_QUEUE_TYPE_GRAPHICS
                and queue_type != pix_namespace.PIX_QUEUE_TYPE.PIX_QUEUE_TYPE_COMPUTE):
            continue

        for event_index in range(queue_info.GetEventCount()):
            event_info = pix_gpu_capture.PixApiExtensionsGpuCapture.GetEvent(queue_info, event_index)
            event_name = str(event_info.Name)
            if any(event_name.startswith(prefix) for prefix in program_event_prefixes):
                print(f'Found program event "{event_name}" @QueueIndex {queue_index}, EventIndex {event_index}')
                return event_info

    return None


def start_analysis(capture_document, pix_device_connection, pix_gpu_capture_experimental,
                   pix_gpu_capture_analysis_retail, pix_gpu_capture_analysis_experimental):
    analysis = pix_gpu_capture_experimental.PixApiExtensionsGpuCapture.GetAnalysisExperimental(capture_document)
    analysis_connection_desc = pix_device_connection.PIX_CONNECTION_DESC.CreateLocal()
    # Connect lives on the retail extension class but works on the experimental
    # analysis object because IPixGpuCaptureAnalysisExperimental inherits from
    # IPixGpuCaptureAnalysis. StartAnalysis is the experimental-flavored one.
    pix_gpu_capture_analysis_retail.PixApiExtensionsGpuCaptureAnalysis.Connect(analysis, analysis_connection_desc, None)
    pix_gpu_capture_analysis_experimental.PixApiExtensionsGpuCaptureAnalysis.StartAnalysis(analysis)
    return analysis


def stop_analysis(analysis):
    try:
        analysis.StopAnalysis()
    except Exception:
        # Best-effort: if analysis is in a state that doesn't support stop,
        # surface nothing -- the document is being torn down anyway.
        pass
    try:
        analysis.Disconnect()
    except Exception:
        pass


def print_program_type(program_state, pix_resources):
    program_type = pix_resources.PixApiExtensionsGpuCaptureResources.GetGpuProgramType(program_state)
    if program_type is None:
        print("Program type: (unavailable)")
        return
    type_names = {
        1: "GenericPipeline",
        4: "RaytracingPipeline",
        5: "WorkGraph",
    }
    name = type_names.get(int(program_type), f"Unknown ({int(program_type)})")
    print(f"Program type: {name} ({int(program_type)})")


def print_global_root_signature(gpu_program, pix_resources):
    try:
        root_signature = pix_resources.PixApiExtensionsGpuCaptureResources.GetGlobalRootSignature(gpu_program)
    except Exception:
        print("Global root signature: (not bound)")
        return
    if root_signature is None:
        print("Global root signature: (not bound)")
        return
    name = str(root_signature.GetName())
    print(f"Global root signature: {name if name else '(unnamed)'}")
    print(f"  ApiObjectId: {root_signature.GetApiObjectId()}")


def print_pipeline_subobjects(generic_pipeline, pix_resources):
    try:
        pipeline_state = pix_resources.PixApiExtensionsGpuCaptureResources.GetPipelineState(generic_pipeline)
    except Exception:
        print("Pipeline state: (not available)")
        return
    if pipeline_state is None:
        print("Pipeline state: (not available)")
        return

    subobject_count = pipeline_state.GetSubobjectCount()
    print(f"Pipeline state subobjects ({subobject_count}):")
    for i in range(subobject_count):
        try:
            subobject = pix_resources.PixApiExtensionsGpuCaptureResources.GetSubobject(pipeline_state, i)
            print(f"  [{i}] {subobject.Type} ({int(subobject.Type)})")
        except Exception:
            print(f"  [{i}] <retrieve failed>")


def print_shaders(gpu_program, pix_resources, pix_extension, pix_shaders_namespace):
    try:
        shaders = pix_resources.PixApiExtensionsGpuCaptureResources.GetShaders(gpu_program)
    except Exception:
        print("Shaders: (not available)")
        return
    if shaders is None:
        print("Shaders: (not available)")
        return

    shader_count = shaders.GetCount()
    print(f"Shaders ({shader_count}):")
    for i in range(shader_count):
        try:
            # IPixCollection.Get<T> takes (index) and returns the typed object.
            # In pythonnet this surfaces as a tuple (returnedObject, outValue)
            # because the underlying COM signature has an out parameter.
            result = pix_extension.PixApiExtensions.Get[pix_shaders_namespace.IPixShader](shaders, i)
            shader = result[0] if isinstance(result, tuple) else result
            print(f"  [{i}] ShaderId={shader.GetId()}, HashSizeBytes={shader.GetHashSizeBytes()}")
        except Exception:
            print(f"  [{i}] <get failed>")


def get_program_state(capture_document, event_info, pix_gpu_capture):
    """Wrapper for IPixGpuCaptureDocument.GetProgramState that handles
    pythonnet's tuple-return convention for ref/out parameters."""
    result = pix_gpu_capture.PixApiExtensionsGpuCapture.GetProgramState(capture_document, event_info)
    return result[0] if isinstance(result, tuple) else result


def main():
    if len(sys.argv) < 2:
        print("Usage: main.py <path-to-capture.wpix>", file=sys.stderr)
        return 1

    capture_path = os.path.abspath(sys.argv[1])
    if not os.path.isfile(capture_path):
        print(f"File not found: {capture_path}", file=sys.stderr)
        return 1

    bin_directory = find_pix_bin_directory("PixApiCsExt.experimental.dll")
    if bin_directory is None:
        print("PIX install not found. Set PIX_DIR or install PIX.", file=sys.stderr)
        return 1
    print(f"Using PIX binaries from: {bin_directory}")
    initialize_pythonnet(bin_directory, "PixApiCsExt.experimental.dll")

    # Imports must come AFTER initialize_pythonnet has loaded the assembly.
    import Microsoft.PIX as pix_namespace
    import Microsoft.PIX.Extension as pix_extension
    import Microsoft.PIX.Extension.DeviceConnection as pix_device_connection
    import Microsoft.PIX.Extension.GpuCapture as pix_gpu_capture
    import Microsoft.PIX.Extension.GpuCapture.Analysis as pix_gpu_capture_analysis_retail
    import Microsoft.PIX.Extension.GpuCapture.Resources as pix_resources
    import Microsoft.PIX.Internal as pix_internal
    import Microsoft.PIX.Internal.Extension.GpuCapture as pix_gpu_capture_experimental
    import Microsoft.PIX.Internal.Extension.GpuCapture.Analysis as pix_gpu_capture_analysis_experimental

    factory = pix_extension.PixApiExtensions.PixCreateFactory[pix_namespace.IPixFactory]()
    capture_document = pix_extension.PixApiExtensions.OpenGpuCaptureDocument[pix_namespace.IPixGpuCaptureDocument](factory, capture_path)
    analysis = start_analysis(
        capture_document, pix_device_connection, pix_gpu_capture_experimental,
        pix_gpu_capture_analysis_retail, pix_gpu_capture_analysis_experimental)

    try:
        event_info = find_first_gpu_program_event(capture_document, pix_namespace, pix_extension, pix_gpu_capture)
        if event_info is None:
            print("No Draw or Dispatch event was found in the GPU capture.", file=sys.stderr)
            return 1

        program_state = get_program_state(capture_document, event_info, pix_gpu_capture)

        print_program_type(program_state, pix_resources)

        gpu_program = pix_resources.PixApiExtensionsGpuCaptureResources.GetGpuProgram[pix_namespace.IPixGpuProgram](program_state)
        print_global_root_signature(gpu_program, pix_resources)
        print_shaders(gpu_program, pix_resources, pix_extension, pix_internal)

        # IPixGenericPipeline is queryable from IPixGpuProgram for graphics/
        # compute pipelines. For DispatchRays / WorkGraph events the cast
        # raises and we skip the subobject walk.
        try:
            generic_pipeline = pix_resources.PixApiExtensionsGpuCaptureResources.GetGpuProgram[pix_namespace.IPixGenericPipeline](program_state)
            if generic_pipeline is not None:
                print_pipeline_subobjects(generic_pipeline, pix_resources)
        except Exception:
            pass
    finally:
        stop_analysis(analysis)

    return 0


if __name__ == "__main__":
    sys.exit(main())
