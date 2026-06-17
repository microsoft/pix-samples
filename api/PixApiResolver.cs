//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
//
//*********************************************************

using System;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;

namespace PixApiSamples.Internal
{
    // Shared bootstrap linked into every C# sample csproj via the
    // <Compile Include="..." Link="..."/> rule in api/Directory.Build.props.
    //
    // Why this exists
    // ---------------
    // The C# csproj files reference PixApiCsExt[.experimental].dll out of
    // the PIX install directory with <Private>false</Private>, which means
    // MSBuild does NOT copy the managed PIX assembly into the sample's
    // output folder. The PIX install dir contains pixapi.dll and ~15 MB
    // of transitive native dependencies (WinPixEngineHostDll.dll,
    // WinPixCaptureReplay.dll, WinPixGpuCapturer.dll, the GPU vendor
    // plugins, AgilitySDK, etc.) -- copying that whole tree into every
    // sample bin would balloon the build artifact to ~1 GB across the 10
    // samples. Loading PixApiCsExt in place keeps the SxS deployment
    // invariant that PixApiCsExt's runtime resolver depends on (pixapi.dll
    // sits next to it) and avoids the duplication.
    //
    // What this does
    // --------------
    // [ModuleInitializer] fires during CLR module load for the sample's
    // own assembly -- before Main runs, before any code that touches
    // PixApi types is JIT'd. We subscribe to AssemblyLoadContext.Default's
    // Resolving event so that when the CLR fails to find PixApiCsExt or
    // PixApiCsExt.experimental in the default probe paths (i.e. next to
    // the .exe), our handler loads it from %PIX_DIR% instead. From that
    // point on, typeof(PixApiExtensions).Assembly.Location is the PIX
    // install directory, and the SxS dll resolver inside PixApiCsExt
    // finds pixapi.dll in the very first iteration of its assembly-
    // relative walk.
    //
    // If PIX_DIR is unset or invalid we deliberately do nothing; the
    // standard probing then fails with a clear FileNotFoundException
    // naming PixApiCsExt, which points the user at the actual problem.
    internal static class PixApiResolver
    {
        [ModuleInitializer]
        internal static void Init()
        {
            string? pixDir = Environment.GetEnvironmentVariable("PIX_DIR");
            if (string.IsNullOrEmpty(pixDir) || !Directory.Exists(pixDir))
            {
                return;
            }

            AssemblyLoadContext.Default.Resolving += (AssemblyLoadContext context, AssemblyName name) =>
            {
                if (name.Name is not ("PixApiCsExt" or "PixApiCsExt.experimental"))
                {
                    return null;
                }

                string candidate = Path.Combine(pixDir, name.Name + ".dll");
                return File.Exists(candidate)
                    ? context.LoadFromAssemblyPath(candidate)
                    : null;
            };
        }
    }
}
