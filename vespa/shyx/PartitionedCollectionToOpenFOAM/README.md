# vtkSHYXPartitionedCollectionToOpenFOAM

把带**体网格**的 IOSS 风 `vtkPartitionedDataSetCollection` 写成 OpenFOAM case（`constant/polyMesh`）。**不**接 SHYX Selection Append Patches（没有体块，做不出 polyMesh）。

## 输入

通常：

```
… → SHYX TetGen → SHYX DataSet To Partitioned Collection
  →（可选）Boundary Fields / Boundary Assignment
  → SHYX Partitioned Collection To OpenFOAM
```

- **必须**有 `element_blocks` 里的一块体积 `vtkUnstructuredGrid`（点 GlobalIds 1..N）
- **side_sets** 变成 `polyMesh/boundary` 里的 patch；用 side 点上的体点 GlobalIds 找回体体面（`element_side` 作回退）
- **node_sets 忽略**（OpenFOAM patch 只有面）

漂浮、对不上体网格的 side：**跳过该块**，Output Messages 用 `vtkErrorMacro` 点名（例如 `skipping side "inlet_extra": 128 faces not on the volume boundary`）。其余合法 side 仍写出。空 patch（`nFaces 0`）不写。两块 side 抢同一张体面则失败。

未认领的体边界面默认失败；高级项 **Allow Default Faces** 才打进 `defaultFaces`（`type wall`）。

## 输出

- 磁盘：Case Directory 下 `constant/polyMesh/{points,faces,owner,neighbour,boundary}`、`system/controlDict`、`case.foam`
- 管线：`vtkOpenFOAMReader` 的 `vtkMultiBlockDataSet`（`internalMesh` + 各 patch），与 File → Open `case.foam` 相同

`points` 只来自体块。patch 面是体网格外表面的子集，不是独立表面。

## 属性

| 属性 | 说明 |
|------|------|
| **Case Directory** | 要写的 OpenFOAM case 根目录 |
| **Allow Default Faces** | 未覆盖的体边界面是否写入 Default Faces Name |
| **Default Faces Name** | 兜底 patch 名（默认 `defaultFaces`） |

菜单：**Filters → SHYX**（不在 Vascular 工具条）。不依赖 `FOAM_SOURCE_DIR` / SnappyHexMesh。
