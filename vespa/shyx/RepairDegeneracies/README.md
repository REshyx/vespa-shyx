# vtkSHYXRepairDegeneracies

菜单 **SHYX Repair Degeneracies (CGAL)**。用 CGAL PMP 修复退化三角形/针状单元。输入必须是**纯三角** `vtkPolyData`。

端口 0：修复后网格。端口 1：被处理的三角形（点数据 `SHYX_RepairRegion`：1=degenerate，2=needle，3=cap，4=both）。`RemoveAlmostDegenerate` / `PreserveGenus` 默认 ON。

参数见 `ParaViewPlugin/SHYXRepairDegeneracies.xml`。
