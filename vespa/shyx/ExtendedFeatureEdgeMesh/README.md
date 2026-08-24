# vtkSHYXExtendedFeatureEdgeMesh

进程内调用 OpenFOAM **`extendedFeatureEdgeMesh`**（`surfaceFeatures` 提取 + 分类），静态链进 `SHYXSnappyHex.lib`，与 SnappyHexMesh 同一套 adapter。菜单 **Filters → SHYX**。需 `VESPA_USE_SNAPPYHEXMESH=ON`。

对照官方 `surfaceFeatureExtract`：includedAngle、trim、subset（open / non-manifold / region）、baffle（`sideVolumeType`）。

## 输入

- **Input**：`vtkPolyData` 三角面。**Region array** 下拉选 cell 数据做 patch id（默认 **None** = 单一 region 0）。`Baffle` cell 数组（非 0 = 该三角所在区域为 baffle，`BOTH`）。
- **Extra feature edges**（可选下拉）：额外 `VTK_LINE` / `VTK_POLY_LINE`，用 `extendedEdgeMesh::add` 并入。

没有三角、只有线时，走「仅线」路径（不再做夹角分类）。

## 输出

`vtkPolyData` 线：

| 数组 | 位置 | OpenFOAM |
|------|------|----------|
| `EdgeStatus` / `EdgeStatusName` | cell | `external` `internal` `flat` `open` `multiple` |
| `RegionEdge` | cell | `regionEdges` |
| `PointStatus` / `PointStatusName` | point | `convex` `concave` `mixed` `nonFeature` |
| `FeaturePoint` | point | `1` = 下标 &lt; `nonFeatureStart` |
| field `FoamExtendedFeatureEdgeMesh` | field | 完整 FoamFile，供 SnappyHexMesh 写出 |
| field `concaveStart` 等 | field | 分类切片下标 |

默认显示 Wireframe。按 `EdgeStatus` 上色。

## 接到 SnappyHexMesh

先对本滤镜 **Apply**（得到线输出），再在 SnappyHexMesh 的 **Feature edges** 下拉里选**本节点**，不要选三角面或 PDC。有 `FoamExtendedFeatureEdgeMesh` 时写入 `constant/extendedFeatureEdgeMesh/features.extendedFeatureEdgeMesh`，并打开 `explicitFeatureSnap`。否则仍写普通 `features.eMesh`。

已有 OpenFOAM `.eMesh` / `extendedFeatureEdgeMesh` 文件时，用 [**SHYX OpenFOAM eMesh Reader**](../EMeshReader/README.md)（File → Open）读进来，不必再从三角面提取。

snappy **不保证** 特征边出现在网格里；分类后的 `.extendedFeatureEdgeMesh` 只加强边加密和 snap 吸附（含特征点）。
