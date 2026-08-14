# Shared Algorithm helpers

`vtkCGALAlgorithm` is **not** an author namespace. It sits outside `vespa/vespa` and `vespa/shyx` so any module can depend on it for VTK↔CGAL conversion and `vtkCGALPolyDataAlgorithm`.

Point/cell attribute mapping after remesh lives in [`../Core`](../Core) (`vtkVESPAAttributeTransfer`). `vtkCGALPolyDataAlgorithm::interpolateAttributes` / `copyAttributes` are thin wrappers gated by `UpdateAttributes`. Pure VTK filters should `DEPENDS vtkVESPACore` and call the transfer helper — not inherit this class.

Only filters that actually call CGAL should `DEPENDS vtkCGALAlgorithm`.
