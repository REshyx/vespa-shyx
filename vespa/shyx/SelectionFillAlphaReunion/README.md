# vtkSHYXSelectionFillAlphaReunionFilter

菜单 **SHYX Selection: Fill, Alpha Wrap, Union**。需要 CGAL ≥ 5.5（内部 `VESPA_ALPHA_WRAPPING`）。输入 `vtkDataSet` + Selection（或 `SelectionCellArrayName`）。对未选中/选中两部分都补洞；对选中部分做 Alpha Wrap（除非 `SkipAlphaWrapping`）；再 CGAL union。默认 `EnableBridgeCleanup` ON：接缝处局部 isotropic remesh + fair。

参数组见同目录 `SHYXSelectionFillAlphaReunionFilter.xml`。
