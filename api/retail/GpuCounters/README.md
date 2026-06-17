# GPU Counters Sample

Demonstrates how to collect GPU performance data from an analyzed PIX GPU capture.
Available in C++ (`cpp/`), C# (`csharp/`), and Python (`python/`).

## Prerequisites

- **PIX** installed
- **Windows Developer Mode** enabled
- **A `.wpix` GPU capture file**
- **A local GPU capable of replaying the capture**

## Building

### C++

Open `cpp\GpuCounters.sln` in Visual Studio and build, or from a Developer Command Prompt:

```
msbuild cpp\GpuCounters.sln /restore /p:Configuration=Debug /p:Platform=x64
```

### C#

Build `csharp\GpuCounters.csproj` in Visual Studio or with the .NET SDK:

```
dotnet build csharp\GpuCounters.csproj -c Release
```

### Python

Install `pythonnet`, then run the script directly.

MSBuild auto-discovers PIX from a Preview install under `%ProgramFiles%\Microsoft PIX Preview\<version>\`, using the newest build dated after 2026/06/15 (older Preview builds are incompatible and skipped). To build against any other install, set the `PIX_DIR` environment variable or pass `/p:PixInstallDir="..."`.

> **Note:** Selecting the newest PIX Preview build is a temporary arrangement for the PIX API preview; the retail release of the PIX API will offer a more robust discovery mechanism.

## Usage

### C++ / C#

```
GpuCounters.exe path\to\capture.wpix
```

### Python

```
python python\main.py path\to\capture.wpix
```

The sample:

1. Calls `PixCreateFactory`
2. Opens the capture with `IPixFactory::OpenGpuCaptureDocument`
3. Gets `IPixGpuCaptureAnalysis` from the document
4. Connects to the local GPU and starts analysis
5. Gets `IPixGpuCaptureCounters`
6. Enumerates available counters and counter groups
7. Collects a few sample counters and prints representative values
8. Gets `IPixGpuCaptureOccupancy` and summarizes occupancy types, stages, and collected points
9. Gets `IPixGpuCaptureHighFrequencyCounters` and summarizes counters, groups, sets, and collected samples
10. Collects `IPixGpuCaptureTiming` data and prints per-event GPU timing for a few events
11. Stops analysis and disconnects

## What it demonstrates

- `IPixGpuCaptureDocument`
- `IPixGpuCaptureAnalysis`
- `IPixGpuCaptureCounters`
- `IPixGpuCaptureOccupancy`
- `IPixGpuCaptureHighFrequencyCounters`
- `IPixGpuCaptureTiming`
- `GetCounters`, `GetCounterGroups`, `CollectCounters`, `GetOccupancy`, `CollectOccupancy`, `GetHighFrequencyCounters`, `CollectCounterData`, and `CollectTiming`

## Notes

- Analysis replay can take time depending on the capture and GPU.
- GPU counter, occupancy, and high-frequency counter collection require replay on a local GPU.
- Optional features (occupancy, high-frequency counters, per-event timing) can be unavailable on some hardware. The sample catches both `COMException` and `NotImplementedException` for each block independently and continues with the remaining steps.

