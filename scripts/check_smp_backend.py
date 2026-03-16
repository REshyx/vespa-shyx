#!/usr/bin/env python
"""
Check VTK SMP backend and version.
Run inside ParaView Python Console, or: pvpython check_smp_backend.py
"""
import sys
import os
import ctypes

try:
    import paraview
    from paraview import vtk
except ImportError:
    try:
        import vtk
    except ImportError:
        print("Cannot import vtk. Run inside ParaView Python Console or use pvpython.")
        sys.exit(1)


def get_tbb_version():
    """Try to get TBB runtime version via ctypes. Returns None if not available."""
    if vtk.vtkSMPTools.GetBackend() != "TBB":
        return None
    # Search dirs: sys.executable dir (pvpython), PATH, paraview lib
    search_dirs = []
    if sys.executable:
        search_dirs.append(os.path.dirname(os.path.abspath(sys.executable)))
    try:
        pv = __import__("paraview")
        if hasattr(pv, "__file__") and pv.__file__:
            pv_path = os.path.dirname(os.path.abspath(pv.__file__))
            search_dirs.extend([pv_path, os.path.join(pv_path, "..", "bin")])
    except Exception:
        pass
    # oneTBB: tbb12, tbb2021, etc.; classic TBB: tbb
    base_names = ["tbb12", "tbb2021", "tbb2020", "tbb", "tbbmalloc12", "tbbmalloc"]
    dll_names = [b + e for b in base_names for e in ["", ".dll"]]

    def try_load(name):
        try:
            lib = ctypes.CDLL(name)
            ver = getattr(lib, "TBB_runtime_version", None)
            if ver is not None:
                ver.restype = ctypes.c_char_p
                return ver().decode("ascii", errors="replace")
        except (OSError, AttributeError):
            pass
        return None

    for d in search_dirs:
        d = os.path.normpath(d)
        if not os.path.isdir(d):
            continue
        for fn in dll_names:
            result = try_load(os.path.join(d, fn))
            if result:
                return result
    for fn in dll_names:
        result = try_load(fn)
        if result:
            return result
    return None


print("=" * 50)
print("VTK SMP Backend Check")
print("=" * 50)
print(f"vtkSMPTools.GetBackend(): {vtk.vtkSMPTools.GetBackend()}")
print(f"vtkSMPTools.GetEstimatedNumberOfThreads(): {vtk.vtkSMPTools.GetEstimatedNumberOfThreads()}")
print(f"vtkSMPTools.GetEstimatedDefaultNumberOfThreads(): {vtk.vtkSMPTools.GetEstimatedDefaultNumberOfThreads()}")
print(f"VTK version: {vtk.vtkVersion.GetVTKVersion()}")
try:
    if hasattr(paraview, '__version__'):
        print(f"ParaView version: {paraview.__version__}")
except NameError:
    pass
tbb_ver = get_tbb_version()
if tbb_ver:
    print(f"TBB version: {tbb_ver}")
elif vtk.vtkSMPTools.GetBackend() == "TBB":
    print("TBB version: (TBB backend active but version unknown)")
else:
    print("TBB version: N/A (backend is not TBB)")
print("=" * 50)
