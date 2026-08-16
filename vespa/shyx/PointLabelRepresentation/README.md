# vtkPointLabelRepresentation

Display 下拉 **`Point Label`**（不是 Filters 菜单）。不要 `PointLabelRepresentation()`。

Python：`GetDisplayProperties().Representation = 'Point Label'`，属性用 **exposed 名** `PL_*`：

| 属性 | 默认 | 说明 |
|------|------|------|
| `PL_ShowPointLabels` | 1 | 显示点标签 |
| `PL_OccludeLabels` | **0** | 0 = overlay（不遮挡）；XML 默认关 |
| `PL_ShowEdges` | 0 | |
| `PL_VertexOnly` | 1 | |
| `PL_PointLabelArray` | 空 | 空则用当前 active scalars |
| `PL_MaximumNumberOfLabels` | 2000 | |
| `PL_LabelFormat` | | |
| `PL_LabelColor` | 0.9 0.9 0.95 | |
| `PL_DepthOffset` | -4 | |

XML：`ParaViewPlugin/PointLabelRepresentation.xml`。
