# Shared VTK utilities (no CGAL)

`vtkVESPACore` sits next to `Algorithm/` (CGAL helpers). Non-CGAL filters that need remesh-style attribute mapping `DEPENDS vtkVESPACore` and call `vtkVESPAAttributeTransfer::Interpolate` / `Copy` — they do not inherit `vtkCGALPolyDataAlgorithm`.
