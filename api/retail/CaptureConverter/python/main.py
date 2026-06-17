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
# Capture Converter Sample (Python)
#
# Demonstrates using the PIX capture file converter from Python via
# pythonnet. The script:
#   1. Loads the PixApiCsExt .NET assembly.
#   2. Creates a PIX factory and capture file converter.
#   3. Detects the input .wpix capture format.
#   4. Upgrades older captures to a new file.
#
# Usage: python main.py <path-to-wpix-file>
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

def build_upgraded_capture_file_path(source_capture_file_path):
    """Build the upgraded output path by appending _upgraded to the file name."""
    source_directory_path = os.path.dirname(source_capture_file_path)
    source_file_name_without_extension, source_file_extension = os.path.splitext(
        os.path.basename(source_capture_file_path))
    upgraded_file_name = f"{source_file_name_without_extension}_upgraded{source_file_extension}"
    return os.path.join(source_directory_path, upgraded_file_name)


def main():
    if len(sys.argv) != 2:
        print("Usage: python main.py <path-to-wpix-file>")
        sys.exit(1)

    source_capture_file_path = os.path.abspath(sys.argv[1])
    if not os.path.isfile(source_capture_file_path):
        print(f"Capture file not found: {source_capture_file_path}")
        sys.exit(1)

    # Step 0: Find PIX binaries and initialize pythonnet.
    bin_directory = find_pix_bin_directory("PixApiCsExt.dll")
    if bin_directory is None:
        print("Could not locate PixApiCsExt.dll. Install PIX or set PIX_DIR.")
        sys.exit(1)
    print(f"Using PIX binaries from: {bin_directory}")

    initialize_pythonnet(bin_directory, "PixApiCsExt.dll")

    try:
        # Step 1: Create the PIX factory (entry point for PIX API operations).
        import Microsoft.PIX as pix
        import Microsoft.PIX.Extension as pix_extension

        factory = pix_extension.PixApiExtensions.PixCreateFactory[pix.IPixFactory]()

        # Step 2: Create a GPU capture file converter from the factory.
        capture_file_converter = pix_extension.PixApiExtensions.CreatePixCaptureFileConverter[
            pix.IPixCaptureFileConverter](factory)

        # Step 3: Detect the source capture file format and print it.
        # The IPixCaptureFileConverter.GetGpuCaptureFileFormat method takes a Win32 PCWSTR
        # parameter that pythonnet cannot construct from a Python str. Call the generated
        # extension method directly, which exposes a System.String overload.
        source_capture_file_format = pix._IPixCaptureFileConverter_Extensions.GetGpuCaptureFileFormat(
            capture_file_converter, source_capture_file_path)
        print(f"Source capture file: {source_capture_file_path}")
        print(f"Detected capture format: {source_capture_file_format}")

        # Step 4: Upgrade older captures to a new file.
        if source_capture_file_format == pix.PIX_GPU_CAPTURE_FILE_FORMAT.PIX_GPU_CAPTURE_FILE_FORMAT_NO_FILE:
            print("The capture file does not exist or could not be opened.")
            sys.exit(1)

        if source_capture_file_format == pix.PIX_GPU_CAPTURE_FILE_FORMAT.PIX_GPU_CAPTURE_FILE_FORMAT_INVALID_OR_CORRUPT:
            print("The capture file is invalid or corrupt.")
            sys.exit(1)

        if source_capture_file_format == pix.PIX_GPU_CAPTURE_FILE_FORMAT.PIX_GPU_CAPTURE_FILE_FORMAT_CURRENT:
            print("The capture file is already in the current format. No upgrade is required.")
            return

        if source_capture_file_format != pix.PIX_GPU_CAPTURE_FILE_FORMAT.PIX_GPU_CAPTURE_FILE_FORMAT_PRE2026:
            print("The capture file format is not recognized as upgradeable by this sample.")
            sys.exit(1)

        upgraded_capture_file_path = build_upgraded_capture_file_path(source_capture_file_path)
        print(f"Upgrading capture to: {upgraded_capture_file_path}")

        pix._IPixCaptureFileConverter_Extensions.UpgradeGpuCaptureFile(
            capture_file_converter,
            source_capture_file_path,
            upgraded_capture_file_path,
            None)

        # Step 5: Print the upgrade results.
        upgraded_capture_file_format = pix._IPixCaptureFileConverter_Extensions.GetGpuCaptureFileFormat(
            capture_file_converter,
            upgraded_capture_file_path)
        print(f"Upgrade complete. Upgraded capture format: {upgraded_capture_file_format}")
        print(f"Upgraded capture saved to: {upgraded_capture_file_path}")
    except Exception as exception:
        print(f"Capture conversion failed: {exception}")
        sys.exit(1)


if __name__ == "__main__":
    main()
