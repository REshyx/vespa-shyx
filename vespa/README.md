# VTK modules

This tree is scanned recursively (`vtk_module_find_modules` on `vespa/`).
Each first-level folder is an **author / origin** namespace, not a CGAL dump.

| Path | Who | What |
|------|-----|------|
| [`Algorithm/`](./Algorithm/) | shared | VTK↔CGAL helpers (`vtkCGALPolyDataAlgorithm`, `vtkCGALHelper`). Used by any module that talks to CGAL. |
| [`Core/`](./Core/) | shared | VTK-only utilities (`vtkVESPAAttributeTransfer`: probe point data / nearest-cell cell data). |
| [`vespa/`](./vespa/) | original Kitware VESPA | CGAL polygon-mesh, point-set, Delaunay, and shape-reconstruction filters. |
| [`shyx/`](./shyx/) | SHYX | Vascular / volume / flow / viz filters. Backends are per-module (`DEPENDS`): CGAL, TetGen, VMTK, VTK-only, … |

To add a new line of work, create `vespa/<your-id>/YourFilter/` with `vtk.module` + `CMakeLists.txt`. Do not put non-SHYX code under `shyx/`, and do not put non-upstream filters under `vespa/vespa/`.

Third-party sources that are not VTK modules (TetGen, …) stay at the **repository root**, not here.
The ParaView UI (`ParaViewPlugin/`) is a thin aggregator of whichever modules were built.

CGAL is **one optional backend**. Set `VESPA_USE_CGAL=ON` (default) to build modules whose `vtk.module` lists `vtkCGALAlgorithm` or `CGAL::CGAL`; `find_package(CGAL)` runs only in that case. Pure VTK / TetGen / VMTK modules do not inherit `vtkCGALPolyDataAlgorithm`.
