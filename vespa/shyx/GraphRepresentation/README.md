# vtkSHYXGraphRepresentation

Display 下拉 **`Graph`**（不是 Filters 菜单）。不要 `SHYXGraphRepresentation()`。

Python：`GetDisplayProperties().Representation = 'Graph'`，属性用 **exposed 名** `GR_*`：

| 属性 | 默认 | 说明 |
|------|------|------|
| `GR_ShowVertex` | 1 | 顶点画成朝向相机的圆盘，数字写在圆心 |
| `GR_AllPoint` | 0 | 叠在 Vertex 内：把非 vertex 的网格点也画成圆盘 |
| `GR_VertexArray` | IDs | 点数组；IDs 用点号；None 只画圆不写字 |
| `GR_VertexColor` / `GR_VertexOpacity` / `GR_VertexScale` | 金 / 1 / 1 | 圆盘外观；Scale 乘在包围盒对角线约 1.2% 的自动半径上 |
| `GR_ShowLine` | 1 | 画 1 维单元，数字在线中点 |
| `GR_LineArray` / `GR_LineColor` / `GR_LineOpacity` | IDs | 线单元数组（点数组会在单元上平均） |
| `GR_ShowFace` | 1 | 画 2 维单元，数字在面心 |
| `GR_FaceArray` / `GR_FaceColor` / `GR_FaceOpacity` | IDs / 0.55 | |
| `GR_ShowVolume` | 1 | 体：外表面片 + 全部唯一边的线框（类似体选择），数字在体心。不用体渲染 |
| `GR_VolumeArray` / `GR_VolumeColor` / `GR_VolumeOpacity` | IDs / 0.35 | |
| `GR_MaximumNumberOfLabels` | 2000 | 每种实体各自封顶 |
| `GR_LabelFormat` | | |
| `GR_LabelFontSize` | 14 | |

默认 Surface 演员会藏起来，只画勾上的那几层。

XML：同目录 `GraphRepresentation.xml`。
