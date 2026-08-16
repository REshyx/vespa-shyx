# Original VESPA (Kitware / vtk-cgal)

Upstream VTK wrappers around CGAL:

- `Delaunay` — module `vtkCGALDelaunay`, class `vtkCGALDelaunay2` (UI: VESPA Delaunay 2D)
- `PolygonMeshProcessing` — boolean, remesh, hole fill, mesh checker, …
- `PointSetProcessing` — XYZ reader, PCA normals
- `ShapeReconstruction` — Poisson / advancing-front

These keep the historical `vtkCGAL*` class names. SHYX and other authors live in sibling folders under `vespa/`, not here.

ParaView 菜单与 XML 对照见 [`../INVENTORY.md`](../INVENTORY.md)。
