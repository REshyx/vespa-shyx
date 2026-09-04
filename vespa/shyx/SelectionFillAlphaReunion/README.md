# vtkSHYXSelectionFillAlphaReunionFilter

菜单 **SHYX Selection: Fill, Alpha Wrap, Union**。需要 CGAL ≥ 5.5（内部 `VESPA_ALPHA_WRAPPING`）。Input 可接多个 `vtkDataSet`（同一 port 0，`AddInputConnection`）；内部先 `vtkGeometryFilter`（非 PolyData）再 `vtkAppendPolyData` 合成一张网格。

Selection 与 Input **按连接顺序一一对应**（VTK port 1 可重复，面板是 Copy 按钮而不是管线里的第二端口）：对第 i 支 Input 用第 i 支 Selection 做 `vtkExtractSelection`（cell id 仍是该 producer 本地的），再加上 append 的 cell 偏移后并入选区。面板按钮 **Copy Input Selections** 会按 Input 顺序拷贝每支的当前选择；没有选择的 Input 放空占位，避免错位。仍可用 `SelectionCellArrayName` 作为无 Selection 时的 cell mask（作用在合并后的网格上）。

对未选中/选中两部分都补洞；对选中部分做 Alpha Wrap（除非 `SkipAlphaWrapping`）；再 CGAL union。默认 `EnableBridgeCleanup` ON：接缝处局部 isotropic remesh + fair。清理 mask 总会写成 cell 数组 `SHYXBridgeCleanupMask`（1 = cleanup patch；remesh 后对应该区域）。

参数组见同目录 `SHYXSelectionFillAlphaReunionFilter.xml`。
