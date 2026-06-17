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
//
// Capture Converter Sample (C#)
//
// Demonstrates using the PIX capture file converter to:
//   1. Create a PIX factory.
//   2. Create a capture file converter.
//   3. Detect the input .wpix capture format.
//   4. Upgrade older captures to a new file.
//
// Usage: CaptureConverter <path-to-wpix-file>
//

using Microsoft.PIX;
using Microsoft.PIX.Extension;

if (args.Length != 1)
{
    Console.Error.WriteLine("Usage: CaptureConverter <path-to-wpix-file>");
    return 1;
}

string sourceCaptureFilePath = Path.GetFullPath(args[0]);
if (!File.Exists(sourceCaptureFilePath))
{
    Console.Error.WriteLine($"Capture file not found: {sourceCaptureFilePath}");
    return 1;
}

try
{
    Console.WriteLine($"Source capture file: {sourceCaptureFilePath}");

    // Step 1: Create the PIX factory (entry point for PIX API operations).
    var factory = PixApiExtensions.PixCreateFactory<IPixFactory>();

    // Step 2: Create a GPU capture file converter from the factory.
    var captureFileConverter = factory.CreatePixCaptureFileConverter<IPixCaptureFileConverter>();

    // Step 3: Detect the source capture file format and print it.
    PIX_GPU_CAPTURE_FILE_FORMAT sourceCaptureFileFormat = captureFileConverter.GetGpuCaptureFileFormat(sourceCaptureFilePath);
    Console.WriteLine($"Detected capture format: {sourceCaptureFileFormat}");

    // Step 4: Upgrade older captures to a new file.
    if (sourceCaptureFileFormat == PIX_GPU_CAPTURE_FILE_FORMAT.PIX_GPU_CAPTURE_FILE_FORMAT_NO_FILE)
    {
        Console.Error.WriteLine("The capture file does not exist or could not be opened.");
        return 1;
    }

    if (sourceCaptureFileFormat == PIX_GPU_CAPTURE_FILE_FORMAT.PIX_GPU_CAPTURE_FILE_FORMAT_INVALID_OR_CORRUPT)
    {
        Console.Error.WriteLine("The capture file is invalid or corrupt.");
        return 1;
    }

    if (sourceCaptureFileFormat == PIX_GPU_CAPTURE_FILE_FORMAT.PIX_GPU_CAPTURE_FILE_FORMAT_CURRENT)
    {
        Console.WriteLine("The capture file is already in the current format. No upgrade is required.");
        return 0;
    }

    if (sourceCaptureFileFormat != PIX_GPU_CAPTURE_FILE_FORMAT.PIX_GPU_CAPTURE_FILE_FORMAT_PRE2026)
    {
        Console.Error.WriteLine("The capture file format is not recognized as upgradeable by this sample.");
        return 1;
    }

    string upgradedCaptureFilePath = BuildUpgradedCaptureFilePath(sourceCaptureFilePath);
    Console.WriteLine($"Upgrading capture to: {upgradedCaptureFilePath}");

    captureFileConverter.UpgradeGpuCaptureFile(sourceCaptureFilePath, upgradedCaptureFilePath, null);

    // Step 5: Print the upgrade results.
    PIX_GPU_CAPTURE_FILE_FORMAT upgradedCaptureFileFormat = captureFileConverter.GetGpuCaptureFileFormat(upgradedCaptureFilePath);
    Console.WriteLine($"Upgrade complete. Upgraded capture format: {upgradedCaptureFileFormat}");
    Console.WriteLine($"Upgraded capture saved to: {upgradedCaptureFilePath}");

    return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine($"Capture conversion failed: {exception.Message}");
    return 1;
}

static string BuildUpgradedCaptureFilePath(string sourceCaptureFilePath)
{
    string sourceDirectoryPath = Path.GetDirectoryName(sourceCaptureFilePath) ?? string.Empty;
    string sourceFileNameWithoutExtension = Path.GetFileNameWithoutExtension(sourceCaptureFilePath);
    string sourceFileExtension = Path.GetExtension(sourceCaptureFilePath);

    return Path.Combine(sourceDirectoryPath, $"{sourceFileNameWithoutExtension}_upgraded{sourceFileExtension}");
}
