# vtkSHYXEMeshReader

读取 OpenFOAM **featureEdgeMesh**（`.eMesh`）和 **extendedFeatureEdgeMesh** 的 ASCII FoamFile，输出 `vtkPolyData` 线。纯 VTK 解析，**不需要** `VESPA_USE_SNAPPYHEXMESH` / OpenFOAM 运行时。

出现在 **File → Open**（扩展名 `.eMesh` / `.extendedFeatureEdgeMesh`）和 **Sources → SHYX**（以及 Filters → SHYX，若 ParaView 把该 source 的 ShowInMenu 归到 SHYX）。

## 支持

- `class featureEdgeMesh` / `edgeMesh`：点 + 边 → `VTK_LINE`
- `class extendedFeatureEdgeMesh` / `extendedEdgeMesh`：再读分类切片下标；输出数组与 [Extended Feature Edge Mesh](../ExtendedFeatureEdgeMesh/README.md) 对齐
- 无 FoamFile 头、仅两个 list 的裸 ASCII（且扩展名是 eMesh 一类）
- 不支持 gzip（`.gz`）：官方 ParaView 不带 `vtkzlib` DLL。请先解压再打开

不支持 **binary** Foam format（会报错，请先转 ascii）。

## 输出（extended 时）

| 数组 | 位置 | 含义 |
|------|------|------|
| `EdgeStatus` / `EdgeStatusName` | cell | external / internal / flat / open / multiple |
| `RegionEdge` | cell | `regionEdges` |
| `PointStatus` / `PointStatusName` | point | convex / concave / mixed / nonFeature |
| `FeaturePoint` | point | 下标 &lt; `nonFeatureStart` |
| field `FoamExtendedFeatureEdgeMesh` | field | 原始 ASCII，可供 SnappyHexMesh 写出 |
| field `concaveStart` 等 | field | 分类切片下标 |

简单 `.eMesh` 只有几何线，没有上述分类数组。接到 **SHYX SnappyHexMesh → Feature edges** 时仍会写成 `features.eMesh`。

默认显示 Wireframe。

## 示例

```python
from paraview.simple import *
r = SHYXEMeshReader(FileName=r'constant/triSurface/motorBike.eMesh')
Show(r)
```
