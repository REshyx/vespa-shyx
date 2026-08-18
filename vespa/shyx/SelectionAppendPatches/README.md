# vtkSHYXSelectionAppendPatches

从父网格的 **Selection** 抽出一块块独立 patch，装进 **`vtkPartitionedDataSetCollection`**。允许重叠；未选中的单元不会自动进任何 patch。

## 用法

1. 把滤镜接到封闭或开放的 `vtkDataSet`（通常是表面 `vtkPolyData`）。
2. 在 Input 上选单元（Sphere / Grow / 橡皮筋均可）。**不必**点 Copy Active Selection。
3. 点 **Add from selection**：新行默认名 `geo_N`，记下当前 cell id。
4. 双击改 **Name**（任意字符串，不必以 `_数字` 结尾）。**表里的行不会合并**，同名可以有多行。
5. 可再选一块（可与已有行重叠）再 Add。
6. **Apply**：按名称把同名行合成一个输出 patch（沿用该名第一次出现时的标记），唯一名称按表中顺序标记 `0, 1, 2, ...`。

不写 GlobalIds，各块顶点相互独立。Mark 写到该块所有单元的 cell data / field data（数组名 **Mark Array Name**，默认 `PatchMark`）。

空表且 Selection 端口有选择时，Apply 会抽出单个 `geo_0`（标记 `0`）。
