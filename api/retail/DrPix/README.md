# Dr. PIX Sample

Run Dr. PIX automated performance experiments on an existing GPU capture.
Available in C++ (`cpp/`), C# (`csharp/`), and Python (`python/`).

Dr. PIX automatically identifies performance issues and surfaces optimization suggestions by replaying targeted experiments across captured GPU work.

## Prerequisites

- **PIX** installed
- **Windows Developer Mode** enabled
- **A `.wpix` GPU capture file**

## Building

### C++

Open `cpp\DrPix.sln` in Visual Studio and build, or from a Developer Command Prompt:

```
msbuild cpp\DrPix.sln /restore /p:Configuration=Debug /p:Platform=x64
```

### C#

Build `csharp\DrPix.csproj` in Visual Studio or with the .NET SDK:

```
dotnet build csharp\DrPix.csproj -c Release
```

### Python

Install `pythonnet`, then run the script directly.

MSBuild auto-discovers PIX from a Preview install under `%ProgramFiles%\Microsoft PIX Preview\<version>\`, using the newest build dated after 2026/06/15 (older Preview builds are incompatible and skipped). To build against any other install, set the `PIX_DIR` environment variable or pass `/p:PixInstallDir="..."`.

> **Note:** Selecting the newest PIX Preview build is a temporary arrangement for the PIX API preview; the retail release of the PIX API will offer a more robust discovery mechanism.

## Usage

### C++ / C#

```
DrPix.exe path\to\capture.wpix
```

### Python

```
python python\main.py path\to\capture.wpix
```

## What it demonstrates

- `PixCreateFactory`
- `IPixGpuCaptureDocument` and `IPixGpuCaptureAnalysis`
- `IPixGpuCaptureDrPix`
- `IPixGpuCaptureExperimentCallback`
- Async experiment execution via `IPixAsyncOperation`

