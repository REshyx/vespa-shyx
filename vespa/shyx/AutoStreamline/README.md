# AutoStreamline

## 目的

单端口体数据自动生成流线：抽表面 → 涡核 → 近壁距离剔除 → 空间均匀降采样 → StreamTracer。

## 管线（纯 VTK）

1. `vtkDataSetSurfaceFilter` 从体数据提取外表面
2. `vtkVortexCore`（体数据矢量场）
3. `vtkImplicitPolyDataDistance` 对涡核点算到外表面的绝对距离 `SDF`
4. 丢弃 `|SDF| < MinSurfaceDistance` 的点（离表面过近）
5. `vtkMaskPoints`：`MaximumNumberOfPoints` + `UNIFORM_SPATIAL_BOUNDS`
6. `vtkStreamTracer`：以上点为 seed

## 端口

| 端口 | 含义 |
| :--- | :--- |
| Input 0 | 体数据 `vtkDataSet` + 3 分量矢量 |
| Output 0 | 流线 |
| Output 1 | 采样后的 seed 点 |

不依赖仓库内现有 shyx filter；仅链接 VTK Modules。
