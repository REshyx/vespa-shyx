# vtkSHYXDeleteSelectedCellsFilter

菜单 **SHYX Delete Selected Cells**。输入任意 `vtkDataSet`（输出类型与输入一致）。端口 1 **Selection**（`SelectionInput`）或 `SelectionCellArrayName`。PolyData/UnstructuredGrid：**删除**单元；结构化：BlankCell；Image/Rectilinear 等：HIDDENCELL ghost。空选区且无 mask → 原样通过。

参数见 `ParaViewPlugin/SHYXDeleteSelectedCells.xml`。
