"""Shared PIX bootstrap helpers for the Python API samples.

Each sample under api/{preview,retail}/<sample>/python/main.py adds the
parent of this module to sys.path and imports find_pix_bin_directory and
initialize_pythonnet from here, rather than carrying its own copy. The
two functions parameterize on a single "marker" DLL name so retail samples
can probe for PixApiCsExt.dll and preview samples for the experimental
variant without duplicating the discovery logic.

The discovery cascade (find_pix_bin_directory) and CLR bootstrap
(initialize_pythonnet) are otherwise identical across samples; keep them
identical here -- a sample that diverges from this module's behavior is
almost certainly a bug in that sample, not a desired override.
"""

import os
import struct


def find_pix_bin_directory(marker):
    """Locate the PIX installation directory containing `marker`.

    Discovery order:
      1. PIX_DIR environment variable
      2. Convention path scan: %ProgramFiles%/Microsoft PIX*/<version>/

    `marker` is the filename whose presence we use to validate that a
    candidate directory is in fact a PIX install of the right flavor:
        - PixApiCsExt.dll                for retail samples
        - PixApiCsExt.experimental.dll   for preview samples

    Preview installs ship both DLLs (the experimental surface is a superset
    of the retail one), so the marker alone correctly selects a preview
    install but does not exclude one when the caller asked for retail. For
    retail samples (marker == "PixApiCsExt.dll") we prefer installs that
    are not the experimental superset, so a side-by-side Preview + Retail
    install picks the Retail one; if no retail-only install is present we
    fall back to using the Preview install (which can still host the
    retail surface).

    Skips registry discovery; the shipping PIX installer does not currently
    write a well-known InstallPath value, so the samples rely on the
    standard Program Files install path that the WiX installer hard-codes.
    """
    pix_directory = os.environ.get("PIX_DIR")
    if pix_directory and os.path.isfile(os.path.join(pix_directory, marker)):
        return os.path.abspath(pix_directory)

    program_files = (os.environ.get("ProgramW6432")
                     or os.environ.get("ProgramFiles")
                     or r"C:\Program Files")
    if not os.path.isdir(program_files):
        return None

    prefer_retail_only = marker == "PixApiCsExt.dll"
    experimental_marker = "PixApiCsExt.experimental.dll"

    retail_only_candidates = []
    other_candidates = []
    try:
        for entry in os.listdir(program_files):
            if not entry.startswith("Microsoft PIX"):
                continue
            flavor_dir = os.path.join(program_files, entry)
            if not os.path.isdir(flavor_dir):
                continue
            for version in os.listdir(flavor_dir):
                install_dir = os.path.join(flavor_dir, version)
                if not os.path.isfile(os.path.join(install_dir, marker)):
                    continue
                has_experimental = os.path.isfile(os.path.join(install_dir, experimental_marker))
                if prefer_retail_only and not has_experimental:
                    retail_only_candidates.append(install_dir)
                else:
                    other_candidates.append(install_dir)
    except OSError:
        return None

    # Prefer retail-only installs over Preview/Experimental superset installs
    # when the caller asked for retail; otherwise both lists are equivalent.
    chosen = retail_only_candidates if retail_only_candidates else other_candidates
    if not chosen:
        return None

    # Sort by version dir name; PIX uses YYMM.DD.NNN[-flavor] which lex-sorts
    # to chronological order, so the last entry is the newest install.
    chosen.sort()
    return os.path.abspath(chosen[-1])


def initialize_pythonnet(bin_directory, csext_dll_name):
    """Bootstrap CoreCLR and load the PIX C# extension assembly.

    `csext_dll_name` is "PixApiCsExt.dll" for retail samples or
    "PixApiCsExt.experimental.dll" for preview samples. The DLL is loaded
    from `bin_directory`, which is the value returned by
    find_pix_bin_directory above.
    """
    python_bitness = struct.calcsize("P") * 8
    if python_bitness != 64:
        raise RuntimeError(
            f"PIX requires 64-bit Python; this interpreter is {python_bitness}-bit.")

    os.add_dll_directory(bin_directory)

    from pythonnet import load
    load("coreclr")

    import clr
    clr.AddReference(os.path.join(bin_directory, csext_dll_name))
