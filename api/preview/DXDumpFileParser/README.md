# DX Dump File Parser Sample

Demonstrates parsing DirectX dump files (`.dxdmp_preview`) to extract shader data, GPU events, resources, page fault data, etc.
Available in C++ (`cpp/`), C# (`csharp/`), and Python (`python/`).

## Prerequisites

- **PIX Preview** installed (uses experimental API headers from `include\experimental\`)
- **A `.dxdmp_preview` dump file** to parse
- **Microsoft.Direct3D.D3D12 1.721.1-preview** NuGet package (restored automatically by MSBuild)

## Building

### C++

Open `cpp\DXDumpFileParser.sln` in Visual Studio and build, or from a Developer Command Prompt:

```
msbuild cpp\DXDumpFileParser.sln /restore /p:Configuration=Debug /p:Platform=x64
```

### C#

Build `csharp\DXDumpFileParser.csproj` in Visual Studio or with the .NET SDK:

```
dotnet build csharp\DXDumpFileParser.csproj -c Release
```

### Python

Install `pythonnet`, then run the script directly.

MSBuild auto-discovers PIX from a Preview install under `%ProgramFiles%\Microsoft PIX Preview\<version>\`, using the newest build dated after 2026/06/15 (older Preview builds are incompatible and skipped). To build against any other install, set the `PIX_DIR` environment variable or pass `/p:PixInstallDir="..."`.

> **Note:** Selecting the newest PIX Preview build is a temporary arrangement for the PIX API preview; the retail release of the PIX API will offer a more robust discovery mechanism.


## Usage

Pass the path to a `.dxdmp_preview` file as a command-line argument:

### C++ / C#

```
DXDumpFileParser.exe path\to\file.dxdmp_preview
```

### Python

```
python python\main.py path\to\file.dxdmp_preview
```

## Notes

- The experimental API surface (`PixApiExperimental.h` / `PixApiCsExt.experimental.dll`) may change between PIX Preview releases.
- DirectX dump files are generated when a TDR (Timeout Detection and Recovery) occurs on Windows.

