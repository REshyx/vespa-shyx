# vtkSHYXSelectionAppendPatches

从父网格抽出、或从管线/参数化几何加入一块块独立 patch，装进 **`vtkPartitionedDataSetCollection`**。未选中的父网格单元不会自动进任何 patch。所有行在输出里都是同等的几何 patch 分块（SnappyHexMesh 的 Surface / Region 表可以按名引用其中任一块）。

## 输出端口

| 端口 | 名称 | 内容 |
|------|------|------|
| 0 | Added patches | 当前表里已经 Add 的 patch（同名在 Apply 时合并） |
| 1 | Remaining cells | **Input 减去所有 selection 行的 cell**（并集）。pipeline / box / sphere 行不参与相减 |

## 用法

1. 把滤镜接到封闭或开放的 `vtkDataSet`（通常是表面 `vtkPolyData`）。
2. **Add from selection**：在 Input（或端口 1 剩余网格）上选单元（Sphere / Grow / 橡皮筋均可），**不必**点 Copy Active Selection。新行默认名 `geo_N`。
3. **Add from pipeline**：下拉选择管线里其它几何节点（`vtkDataSet` / PDC 等），作为额外 patch 接入。不要选本滤镜自己或其下游，也不要重复加同一个端口。
4. **Add from shape**：下拉 **Box** 或 **Sphere**。初始中心与半径与标题栏 **Sphere Selection** 相同（视口中心贴到表面上，直径约短边的 30%）。3D 视图里出现可拖动/旋转/缩放的 widget（Box 左键旋转）；勾选 Apply on Add 时，松手会 Apply。
5. **Apply on Add**（默认勾选）：每次 **Add** 或 **Remove** 都会立刻 **Apply**，刷新两个端口。接着在 **Remaining cells** 上选，已 Add 的 selection 单元不在该端口上，不会重复选中。
6. 取消勾选 **Apply on Add**：只改表、不 Apply。
7. 双击改 **Name**。**表里的行不会合并**，同名可以有多行。
8. 手动 **Apply**：按名称把同名行合成一个输出 patch（沿用该名第一次出现时的标记），唯一名称按表中顺序标记 `0, 1, 2, ...`。

不写 GlobalIds，各块顶点相互独立。Mark 写到该块所有单元的 cell data / field data（数组名 **Mark Array Name**，默认 `PatchMark`）。

空表且 Selection 端口有选择时，Apply 会抽出单个 `geo_0`（标记 `0`），端口 1 为其余单元。
