# Pipeline Inspection Sample

Inspects pipeline states and global root signatures from an analyzed PIX GPU capture.
Available in C++ (`cpp/`), C# (`csharp/`), and Python (`python/`).

## Prerequisites

- **PIX** installed
- **Windows Developer Mode** enabled
- **A `.wpix` GPU capture file**
- **A local GPU capable of replaying the capture**

## Building

### C++

Open `cpp\PipelineInspection.sln` in Visual Studio and build, or from a Developer Command Prompt:

```
msbuild cpp\PipelineInspection.sln /restore /p:Configuration=Debug /p:Platform=x64
```

### C#

Build `csharp\PipelineInspection.csproj` in Visual Studio or with the .NET SDK.

### Python

Install `pythonnet`, then run the script directly.

MSBuild auto-discovers PIX from a Preview install under `%ProgramFiles%\Microsoft PIX Preview\<version>\`, using the newest build dated after 2026/06/15 (older Preview builds are incompatible and skipped). To build against any other install, set the `PIX_DIR` environment variable or pass `/p:PixInstallDir="..."`.

> **Note:** Selecting the newest PIX Preview build is a temporary arrangement for the PIX API preview; the retail release of the PIX API will offer a more robust discovery mechanism.

## Usage

### C++ / C#

```
PipelineInspection.exe path\to\capture.wpix
```

### Python

```
python python\main.py path\to\capture.wpix
```

## What it demonstrates

- `IPixPipelineState`
- `IPixRootSignature`
- `IPixGpuCaptureAnalysis`
- Pipeline state subobject enumeration
- Root signature parameter inspection

## Notes

- The sample caps inspection at the first 16 events with generic-pipeline state. Without this, very large captures (e.g. 37 MB VRS captures with hundreds of thousands of events) would take several minutes to walk.
- `GetPipelineType` is wrapped in error-handling that skips the event on failure. On raytracing-only captures, some events report a generic-pipeline program type but their underlying pipeline state isn't bound, causing this call to fail with `0x8007139F` (`E_NOT_VALID_STATE`).

