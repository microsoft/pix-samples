# GPU Capture Sample

Demonstrates the full PIX programmatic GPU capture workflow:

1. **Launch** a D3D12 application under GPU capture
2. **Take** a GPU capture (blocking until the capture completes)
3. **Open** the resulting `.wpix` file and parse its event data
4. **Start analysis** on the capture (requires Developer Mode) and demonstrate an analysis-time operation (resource enumeration via `GetD3D12Resources`)

Available in C++ (`cpp/`), C# (`csharp/`), and Python (`python/`).

## Prerequisites

- **PIX Preview** installed (the MSI puts headers, libs, and runtime DLLs under `%ProgramFiles%\Microsoft PIX Preview\<version>\`)
- **Windows Developer Mode** enabled (see top-level README)
- A built **D3D12 executable** to use as the capture target

## Building

### C++ and C#

Open `cpp\GpuCapture.sln` in Visual Studio and build, or from a Developer Command Prompt:

```
msbuild cpp\GpuCapture.sln /restore /p:Configuration=Debug /p:Platform=x64
```

MSBuild auto-discovers PIX from a Preview install under `%ProgramFiles%\Microsoft PIX Preview\<version>\`, using the newest build dated after 2026/06/15 (older Preview builds are incompatible and skipped). To build against any other install, set the `PIX_DIR` environment variable or pass `/p:PixInstallDir="..."`.

> **Note:** Selecting the newest PIX Preview build is a temporary arrangement for the PIX API preview; the retail release of the PIX API will offer a more robust discovery mechanism.

`GpuCapture.sln` builds only the capture samples. Build your target D3D12 executable separately.

### Python

Install the `pythonnet` package:

```
pip install pythonnet
```

No build step needed — run the script directly.

## Running

### C++ / C#

Run the built executable with the path to your D3D12 executable. It will:
- Launch the target executable under GPU capture via PIX
- Take a capture, parse events, start analysis
- Clean up the temporary capture file

```
x64\Debug\GpuCapture.exe C:\path\to\YourD3D12App.exe
```

### Python

```
python python\main.py C:\path\to\YourD3D12App.exe
```

The script still discovers PIX binaries from the convention install path (or `PIX_DIR`), but the target executable path must be provided on the command line.

## What it demonstrates

| API | Purpose |
|-----|---------|
| `PixCreateFactory` | Create the PIX factory object |
| `OpenConnectionDocument` | Connect to a local PIX device |
| `LaunchProcess` | Launch a process under GPU capture |
| `TakeGpuCapture` / `TakeGpuCaptureResult` | Request a GPU capture |
| `OpenGpuCaptureDocument` | Open a `.wpix` capture file |
| `GetQueues` / `GetEvent` | Walk the capture's command queues and events |
| `GetAnalysis` / `StartAnalysis` / `StopAnalysis` / `Disconnect` | Run GPU analysis with the recommended cleanup sequence |
| `GetD3D12Resources` | Example analysis-time call performed between `StartAnalysis` and cleanup |

## Troubleshooting

| Error | Cause | Fix |
|-------|-------|-----|
| `0x8abc0001` | Developer Mode not enabled | Enable Developer Mode (see Prerequisites) |
| `0x8abc0020` | No GPU work captured | Target app exited before rendering; increase sleep or use a longer-running app |
| `WinPixEventRuntime.dll not found` | The target app can't find its runtime DLL | Ensure the target app's dependencies are available |
| `PixApi.h not found` / `No usable PIX installation found` | PIX Preview not installed, not discoverable, or older than the required build | Install a PIX Preview build dated after 2026/06/15, or set `PIX_DIR` / `/p:PixInstallDir` |

