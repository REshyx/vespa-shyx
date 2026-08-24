# vtkSHYXExtractSelectedCellsFilter

菜单 **SHYX Extract Selection**。与 **SHYX Delete Selected Cells** 相对：保留选中单元、丢掉其余。输入任意 `vtkDataSet`。端口 1 **Selection**（`SelectionInput`）或 `SelectionCellArrayName`。创建 filter 时会把 Input 上的当前选择拷进 Selection 面板（与 Extract Selection 相同，不必先点 Copy Active Selection）。

与 ParaView **Extract Selection** 不同：不会把输出一律变成 `vtkUnstructuredGrid`。若输入是 `vtkPolyData`，或保留单元全是 PolyData 类型（点 / 线 / 面 / strip），输出为 **vtkPolyData**；否则为 **vtkUnstructuredGrid**。**Invert Selection**（默认关）对选区取补集后再提取。空选区且无 mask（取补后仍为空）→ 空输出。

参数见同目录 `SHYXExtractSelectedCells.xml`。
