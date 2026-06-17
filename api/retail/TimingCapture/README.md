# Timing Capture Sample

Opens a PIX timing capture (`.wpix`), displays basic document information, and enumerates visible system monitor counters from a local PIX connection. Available in C++ (`cpp/`), C# (`csharp/`), and Python (`python/`).

## Prerequisites

- **PIX** installed
- **A `.wpix` timing capture file** to open
- **Optional:** a PDB path if you want the sample to resolve symbols

## Building

### C++

Open `cpp\TimingCapture.sln` in Visual Studio and build, or from a Developer Command Prompt:

```
msbuild cpp\TimingCapture.sln /restore /p:Configuration=Debug /p:Platform=x64
```

MSBuild auto-discovers PIX from a Preview install under `%ProgramFiles%\Microsoft PIX Preview\<version>\`, using the newest build dated after 2026/06/15 (older Preview builds are incompatible and skipped). To build against any other install, set the `PIX_DIR` environment variable or pass `/p:PixInstallDir="..."`.

> **Note:** Selecting the newest PIX Preview build is a temporary arrangement for the PIX API preview; the retail release of the PIX API will offer a more robust discovery mechanism.

### C#

Build `csharp\TimingCapture.csproj` in Visual Studio or with the .NET SDK.

### Python

Install `pythonnet`, then run the script directly.

## Usage

### C++ / C#

```
TimingCapture.exe path\to\capture.wpix [full-pdb-path]
```

### Python

```
python python\main.py path\to\capture.wpix [full-pdb-path]
```

Provide the optional `full-pdb-path` argument to resolve symbols before enumerating system monitor counters and closing the document.

## What it demonstrates

| API | Purpose |
|-----|---------|
| `PixCreateFactory` | Create the PIX factory object |
| `OpenTimingCaptureDocument` | Open a `.wpix` timing capture document |
| `IPixTimingCaptureDocument` | Read document paths, resolve symbols, and close the document |
| `ResolveSymbols` + `TimingCaptureSymbolSettings` + `IPixProgressNotifications` | Resolve PDB symbols with status / progress callbacks fired as the (potentially long-running) operation progresses |
| `OpenConnectionDocument` | Open a local PIX connection for device-level queries |
| `GetCounterDescriptions` | Enumerate available system monitor counters and counter groups |
| `IPixSystemMonitorCounter` | Read counter display name, units, visibility, and min/max range |
| `GetPixStoragePath` | Locate `PixStorage.dll` for direct capture queries |
| `ResolveSymbols` | Optionally load symbol data into the timing capture |

