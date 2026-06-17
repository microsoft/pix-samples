# Resource Views Sample

Inspects D3D12 resources accessed by a draw call in a PIX GPU capture (`.wpix`).

## Prerequisites

- **PIX** installed
- **Windows Developer Mode** enabled
- **A `.wpix` GPU capture** to inspect

## Building

Build the C++ sample in Visual Studio or from a Developer Command Prompt:

```
msbuild cpp\ResourceViews.sln /restore /p:Configuration=Debug /p:Platform=x64
```

The C# sample can be built with:

```
dotnet build csharp\ResourceViews.csproj -c Debug -p:Platform=x64
```

MSBuild auto-discovers PIX from a Preview install under `%ProgramFiles%\Microsoft PIX Preview\<version>\`, using the newest build dated after 2026/06/15 (older Preview builds are incompatible and skipped). To build against any other install, set the `PIX_DIR` environment variable or pass `/p:PixInstallDir="..."`.

> **Note:** Selecting the newest PIX Preview build is a temporary arrangement for the PIX API preview; the retail release of the PIX API will offer a more robust discovery mechanism.

## Usage

```text
ResourceViews.exe path\to\capture.wpix

dotnet run --project csharp\ResourceViews.csproj -- path\to\capture.wpix

python python\main.py path\to\capture.wpix
```

## What it demonstrates

| API | Purpose |
|-----|---------|
| `IPixD3D12Resource` | Inspect resource names, descriptions, and resource-specific views |
| `IPixD3D12Resources` | Enumerate all D3D12 resources present in the capture |
| `IPixResourceViews` | Enumerate views bound at an event and views associated with a resource |

