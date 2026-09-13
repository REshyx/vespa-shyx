# vtkSMSHYXSelectionBoundsDomain

ParaView 自定义 Domain（XML 标签 **`SHYXSelectionBoundsDomain`**）。继承 `vtkSMBoundsDomain`，面板上仍有 Scale/Reset；**Reset** 用选区 cell 的 AABB 最长边 × `scale_factor`，不是整张 Input。

服务端 `vtkPVSHYXSelectionBoundsInformation` 对 filter 的 port 0 数据集 + port 1 `vtkSelection` 做 `vtkExtractSelection`（无 Selection 时用 `SelectionCellArrayName` cell mask）。仅在 `VESPA_BUILD_PV_PLUGIN=ON` 时编进 `VESPAPlugin`。

当前用法：[`SHYXSelectionFillAlphaReunionFilter.xml`](../SelectionFillAlphaReunion/SHYXSelectionFillAlphaReunionFilter.xml) 的 Alpha / Offset。
