# Capture Converter Sample

Demonstrates how to detect a PIX GPU capture file's format version and upgrade older `.wpix` captures to the current format.

## Prerequisites

- PIX installed
- A `.wpix` GPU capture file

## Building

MSBuild auto-discovers PIX from a Preview install under `%ProgramFiles%\Microsoft PIX Preview\<version>\`, using the newest build dated after 2026/06/15 (older Preview builds are incompatible and skipped). To build against any other install, set the `PIX_DIR` environment variable or pass `/p:PixInstallDir="..."`.

> **Note:** Selecting the newest PIX Preview build is a temporary arrangement for the PIX API preview; the retail release of the PIX API will offer a more robust discovery mechanism.

## Usage

### C++

Build `cpp\CaptureConverter.sln`, then run:

```
CaptureConverter.exe path\to\capture.wpix
```

### C#

Build `csharp\CaptureConverter.csproj`, then run:

```
CaptureConverter.exe path\to\capture.wpix
```

### Python

Install `pythonnet`, then run:

```
python python\main.py path\to\capture.wpix
```

## What it demonstrates

- `PixCreateFactory`
- `IPixFactory`
- `IPixCaptureFileConverter::GetGpuCaptureFileFormat` — detect on-disk capture format version
- `IPixCaptureFileConverter::UpgradeGpuCaptureFile` — convert older `.wpix` captures to the current format
- `IPixProgressNotifications` — inline implementation that prints status / progress callbacks fired by the upgrade operation

