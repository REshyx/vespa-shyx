# vtkSHYXSelectionAppendPatches

从父网格的 **Selection** 抽出一块块独立 patch，装进 **`vtkPartitionedDataSetCollection`**。未选中的单元不会自动进任何 patch。

## 输出端口

| 端口 | 名称 | 内容 |
|------|------|------|
| 0 | Added patches | 当前表里已经 Add 的 patch（同名在 Apply 时合并） |
| 1 | Remaining cells | **Input 减去所有已 Add 的 cell**（并集）。便于在未 Add 区域上继续选，避免重复选中 |

## 用法

1. 把滤镜接到封闭或开放的 `vtkDataSet`（通常是表面 `vtkPolyData`）。
2. 在 Input（或端口 1 剩余网格）上选单元（Sphere / Grow / 橡皮筋均可）。**不必**点 Copy Active Selection。
3. 点 **Add from selection**：新行默认名 `geo_N`，记下当前 cell id。
4. **Apply on Add**（默认勾选）：每次 **Add** 或 **Remove** 都会立刻 **Apply**，刷新两个端口。接着在 **Remaining cells** 上选，已 Add 的单元不在该端口上，不会重复选中；Remove 后那些单元会回到端口 1，可以再选回去。
5. 取消勾选 **Apply on Add**：只改表、不 Apply。端口 1 仍是旧的（或尚未执行过减去），可以再选已经 Add 过的单元，故意重叠。
6. 双击改 **Name**（任意字符串，不必以 `_数字` 结尾）。**表里的行不会合并**，同名可以有多行。
7. 手动 **Apply**：按名称把同名行合成一个输出 patch（沿用该名第一次出现时的标记），唯一名称按表中顺序标记 `0, 1, 2, ...`。

不写 GlobalIds，各块顶点相互独立。Mark 写到该块所有单元的 cell data / field data（数组名 **Mark Array Name**，默认 `PatchMark`）。

空表且 Selection 端口有选择时，Apply 会抽出单个 `geo_0`（标记 `0`），端口 1 为其余单元。
