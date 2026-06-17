# D3D State Inspection Sample

Demonstrates inspecting full D3D12 pipeline state at a specific draw call from a `.wpix` GPU capture.

Available in C++ (`cpp/`), C# (`csharp/`), and Python (`python/`).

## Prerequisites

- **PIX Preview** installed
- **Windows Developer Mode** enabled
- A `.wpix` **GPU capture** file

## Notes

- This sample uses the experimental `IPixProgramState` workflow off `IPixGpuCaptureDocument::GetProgramState`. It requires a **Preview PIX** installation for the experimental analysis bring-up, but the program-state API itself ships in retail.

## Building

### C++ and C#

Open `cpp\D3DStateInspection.sln` in Visual Studio and build, or from a Developer Command Prompt:

```
msbuild cpp\D3DStateInspection.sln /restore /p:Configuration=Debug /p:Platform=x64
```

### Python

Install `pythonnet`:

```
pip install pythonnet
```

MSBuild auto-discovers PIX from a Preview install under `%ProgramFiles%\Microsoft PIX Preview\<version>\`, using the newest build dated after 2026/06/15 (older Preview builds are incompatible and skipped). To build against any other install, set the `PIX_DIR` environment variable or pass `/p:PixInstallDir="..."`.

> **Note:** Selecting the newest PIX Preview build is a temporary arrangement for the PIX API preview; the retail release of the PIX API will offer a more robust discovery mechanism.

## Running

### C++ / C#

```
x64\Debug\D3DStateInspection.exe <path-to-capture.wpix>
```

### Python

```
python python\main.py <path-to-capture.wpix>
```

## What it demonstrates

- Create a PIX factory and open a GPU capture document
- Connect analysis and start analysis
- Walk command queues to find a Draw / Dispatch / DispatchRays / DispatchMesh event
- Query `IPixProgramState` at that event via `IPixGpuCaptureDocument::GetProgramState`
- Print the program type (Generic / Raytracing / WorkGraph)
- Print the global root signature (name, ApiObjectId)
- Print the bound shaders (per-stage IDs and hash sizes)
- For graphics/compute pipelines, walk pipeline-state subobjects and print their types

## What it does NOT demonstrate

Runtime D3D state (viewports, scissor rects, vertex/index buffers, root parameter values bound at the draw call) is not part of program/pipeline state and lives on a separate `IPixD3DState` API path reachable from postmortem dumps. It is intentionally outside this sample's scope.

