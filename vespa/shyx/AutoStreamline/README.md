# AutoStreamline

## 目的

单端口体数据自动生成流线：抽表面 → 涡核 → 近壁距离剔除 → MaskPoints 降采样 → StreamTracer。

## 管线（纯 VTK）

1. `vtkDataSetSurfaceFilter` 从体数据提取外表面
2. `vtkVortexCore`（体数据矢量场）
3. `vtkImplicitPolyDataDistance` 对涡核点算到外表面的绝对距离 `SDF`
4. 丢弃 `|SDF| < MinSurfaceDistance` 的点（离表面过近）
5. `vtkMaskPoints`：`MaximumNumberOfPoints` + `RandomModeType`（默认 `UNIFORM_SPATIAL_BOUNDS`）
6. `vtkStreamTracer`：以上点为 seed

### MaskPoints `RandomModeType`

| 值 | 模式 | 说明 |
| :--- | :--- | :--- |
| 0 | Randomized ID Strides | 按点 ID 随机步长抽取 |
| 1 | Random Sampling | Vitter 无放回随机抽样，通常能抽满上限 |
| 2 | Spatially Stratified | 空间分层抽样 |
| 3 | Uniform Spatial (Bounds) | AABB 内随机打点再找近邻（默认；空盒多时点数偏少） |
| 4 | Uniform Spatial (Surface) | 按表面积采样（需面单元；涡核点云通常不适用） |
| 5 | Uniform Spatial (Volume) | 按体积采样（需体单元；涡核点云通常不适用） |

## 端口

| 端口 | 含义 |
| :--- | :--- |
| Input 0 | 体数据 `vtkDataSet` + 3 分量矢量 |
| Output 0 | 流线 |
| Output 1 | 采样后的 seed 点 |

不依赖仓库内现有 shyx filter；仅链接 VTK Modules。菜单 **SHYX Auto Streamline**，proxy `SHYXAutoStreamline()`。种子点端口默认 **Point Gaussian**。

面板还暴露 Stream Tracer 属性（见 `SHYXAutoStreamline.xml`）：`SelectInputVectors`、`RandomSeed`、`IntegrationDirection`（默认 BOTH）、`IntegratorType`（默认 RK45）、`IntegrationStepUnit`、`InitialIntegrationStep`、`MaximumPropagation`、`MaximumNumberOfSteps`、`TerminalSpeed`、`InterpolatorType`。C++ 里的 `HigherOrderMethod` / `FasterApproximation` **未**进 XML。
