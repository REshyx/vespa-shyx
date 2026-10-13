# vtkSHYXDeleteSelectedCellsFilter

菜单 **SHYX Delete Selected Cells**。输入任意 `vtkDataSet`（输出类型与输入一致）。端口 1 **Selection**（`SelectionInput`）或 `SelectionCellArrayName`。创建 filter 时会把 Input 上的当前选择拷进 Selection 面板（与 Extract Selection 相同，不必先点 Copy Active Selection）。**Invert Selection**（默认关）对选区取补集后再删。PolyData/UnstructuredGrid：**删除**单元；结构化：BlankCell；Image/Rectilinear 等：HIDDENCELL ghost。空选区且无 mask（取补后仍为空）→ 原样通过。

三维视图有点或 cell 选择时，按 **Ctrl+D**（或右键 **Delete Selected Cells**）会在该数据上新建本滤镜并 Apply，等价于手动建节点再点 Apply。单独的 D 留给 ParaView 点选择。焦点不在三维视图时 Ctrl+D 不拦截。复合数据上的选区会拒绝。

参数见同目录 `SHYXDeleteSelectedCells.xml`。
