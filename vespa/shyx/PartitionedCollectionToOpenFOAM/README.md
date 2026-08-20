# vtkSHYXPartitionedCollectionToOpenFOAM

把带**体网格**的 IOSS 风 `vtkPartitionedDataSetCollection` 写成一份可编辑的 OpenFOAM case（`constant/polyMesh` + `system/` + `0/`）。**不**接 SHYX Selection Append Patches（没有体块，做不出 polyMesh）。

## 输入

通常：

```
… → SHYX TetGen → SHYX DataSet To Partitioned Collection
  →（可选）Boundary Fields / Boundary Assignment
  → SHYX Partitioned Collection To OpenFOAM
```

- **必须**有 `element_blocks` 里的一块体积 `vtkUnstructuredGrid`（点 GlobalIds 1..N）
- **side_sets** 变成 `polyMesh/boundary` 里的 patch；名字来自 PDC 块 `NAME`（DataSet To Partitioned Collection 面板可改），**不会**改成 inlet/outlet/wall
- **node_sets 忽略**（OpenFOAM patch 只有面）

漂浮、对不上体网格的 side：**跳过该块**，Output Messages 点名。空 patch 不写。两块 side 抢同一张体面则失败。

未认领的体边界面默认失败；高级项 **Allow Default Faces** 才打进 `defaultFaces`（`type wall`）。

## 输出

磁盘（Case Directory；留空则写到 `%TEMP%\shyx-pdc-of-<id>-<mtime>\case`，与 SnappyHexMesh 的 `%TEMP%\shyx-snappy-<id>-<mtime>\case` 同一套命名）：

- `constant/polyMesh/{points,faces,owner,neighbour,boundary}`
- `system/{controlDict,fvSchemes,fvSolution}`（simpleFoam 底稿）
- `0/p`、`0/U`：每个 patch 名与 `boundary` **相同**，占位 `zeroGradient`（自己改 BC）
- `0/shyx_<数组名>`：体块 CellData / PointData，一律加 `shyx_` 前缀，避免和 `0/p`、`0/U` 等求解器场重名（VTK 名叫 `p`/`U` 则写成 `shyx_p`/`shyx_U`）。OpenFOAM 场文件**不能含 NaN**。PointData（如 `BoundaryRadialValueNormal`、`BoundaryVariable*`）要求面上**全部顶点都有限**才写平均值；三个顶点里只要有一个 NaN，该面/该体元就是 **0**（避免沿共点往外扩一圈）。体内顶点含 NaN 的单元同样为 0。有限 CellData 仍可直接作 `internalField`。面序与 `polyMesh` 该 patch 一致。`type calculated`。
- `case.foam`

管线：`vtkOpenFOAMReader` 的 `vtkMultiBlockDataSet`（`internalMesh` + 各 patch）。

引用场值：把 `0/<数组>` 里同一 patch 的 `value nonuniform List<…>` 拷进 `0/U` 的 `fixedValue`，或 `#include`。不要用 `$inlet`。

`points` 只来自体块。patch 面是体网格外表面的子集。

## 属性

| 属性 | 说明 |
|------|------|
| **Case Directory** | 一行：可选路径（占位「可留空写到 %TEMP%」）、浏览、📂 打开实际写出目录 |
| **Arrays to write** | 勾选要写成 `0/shyx_<name>` 的体 PointData/CellData；默认全开 |
| **Allow Default Faces** | 未覆盖的体边界面是否写入 Default Faces Name |
| **Default Faces Name** | 兜底 patch 名（默认 `defaultFaces`） |

菜单：**Filters → SHYX**（不在 Vascular 工具条）。不依赖 `FOAM_SOURCE_DIR` / SnappyHexMesh。
