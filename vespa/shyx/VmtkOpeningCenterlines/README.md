# vtkSHYXVmtkOpeningCenterlines

从开口种子点算 VMTK 中心线。需 `VESPA_USE_VMTK`。端口 0 可选中心线/网络，端口 1 Opening seed points（首次显示默认 **Point Label**，不是 Point Gaussian）。面板 `shyx_openings_table`。

**Centerline method**（需打开 Calculate centerline）：

- **Voronoi tree (source–target)**：`vtkvmtkPolyDataCenterlines`，封闭面 + inlet/outlet 种子。环只保留代价更低的一臂。一条 cell = 一条 source→target 路径（父血管段上多条路径重叠）。
- **Network (punch openings, keep loops)**：把 Threshold 数组（默认 `EndpointIndex`）幅度 > 0 的单元从副本上删掉，每个端帽变成边界环，再跑 `vtkvmtkPolyDataNetworkExtraction`。不使用 inlet/outlet 勾选。一条 cell = 图上的一条分支。

**Centerline post-process**（需打开 Calculate centerline；默认全关，顺序固定：attributes → branches → geometry）：

- **Compute attributes**：`Abscissas`、`ParallelTransportNormals`
- **Extract branches**：`CenterlineIds`、`TractIds`、`GroupIds`、`Blanking`（面向 Voronoi 重叠路径；Network 会丢掉 `Topology`）
- **Compute geometry**：`Length` / `Curvature` / `Torsion` / `Tortuosity` 与 Frenet 三轴

参数见同目录 `SHYXVmtkOpeningCenterlines.xml`。

## 端口 0 数组含义

几何：点是中心线上的采样点；cell 是一条 polyline。着色/筛选时看清数组是 **Point Data** 还是 **Cell Data**。

### 算完中心线就有

| 数组 | 位置 | 何时出现 | 含义 |
|------|------|----------|------|
| `MaximumInscribedSphereRadius` | Point | 两种方法都有 | 该点处管腔半径。**Voronoi** 是真正的最大内切球半径（MISR）。**Network** 是推进虚拟球的局部半径估计，不是 MISR，只是同名。 |
| `Topology` | Cell，2 分量 | 仅 Network | 这条分支两端接到图上的哪两个节点：`(node0, node1)`。**-1** 自由端（推进球停住）；**0** 多半是开口；**>0** 分叉节点编号。编号在退化分叉清理后会压缩，可能和「开口 = 0」撞号，端点也不保证都带 `-1`。更稳的连通关系应看分支端点几何，不要当精确邻接表。 |

Voronoi 的 Eikonal / 代价等写在内部 Voronoi 图上，不出现在端口 0。

### Compute attributes

先于 Extract branches 勾选，弧长才会从入口连续穿过分叉；若先切分支，每段 tract 的弧长从 0 重计。

| 数组 | 位置 | 含义 |
|------|------|------|
| `Abscissas` | Point | 沿**当前这条 cell**从起点累计的弧长（曲线坐标 s）。用来在同一分支上取等间隔截面、对齐不同中心线。 |
| `ParallelTransportNormals` | Point，3 分量 | 沿切向平行运输的法向。和切向组成随体标架，避免 Frenet 法向在接近直线处翻转。后续把截面/图像绕中心线展开（mapping）时用这个，而不是原始 Frenet 法向。 |

### Extract branches

把互相套着的 source→target 路径切成 tract，再按「是否落在对方的半径管里」捆成分支。典型分叉上：分叉前的公共段一组、分叉区内一组（Blanking=1）、每个子支一组。

| 数组 | 位置 | 含义 |
|------|------|------|
| `CenterlineIds` | Cell | 这条 tract 来自哪条原始中心线（哪条 source→target 路径的 cell id）。 |
| `TractIds` | Cell | 同一条原始中心线上，从上游到下游的段序号（0, 1, 2…）。 |
| `GroupIds` | Cell | 捆在一起的**解剖分支**编号。同一根血管段上重叠的几条路径会得到同一个 GroupId。`vmtkbranchclipper`、截面、harmonic mapping 都认这个。 |
| `Blanking` | Cell | **1** = 分叉区 tract（两条中心线仍在彼此半径管内，几何不是单一管腔）；**0** = 真正的分支。裁表面、算分支几何时通常丢掉 Blanking=1 的段。 |

四者一起唯一标识一条 tract，以及它和邻段的拓扑关系。Network 本身已是一条 cell 一条边，再跑 extractor 会重建 cell 并丢掉 `Topology`。

### Compute geometry

作用在**当时的 cell**上：未切分支则一条 source→target 路径一个值；切过则每个 tract 一个值。Frenet 在接近直线或折线很糙时不稳定。

| 数组 | 位置 | 含义 |
|------|------|------|
| `Length` | Cell | 该 cell 折线总长。 |
| `Curvature` | Cell / 点列 | 弯曲程度（切向转角相对弧长）。越大弯得越急。 |
| `Torsion` | Cell / 点列 | 密切平面扭转，三维空间里「拧」的程度。 |
| `Tortuosity` | Cell | 迂曲度：折线长 / 两端直线距离 − 1。0 表示几乎拉直。 |
| `FrenetTangent` | Point，3 分量 | 单位切向（沿中心线前进方向）。 |
| `FrenetNormal` | Point，3 分量 | 主法向（指向弯曲内侧）。曲率接近 0 时方向会跳。 |
| `FrenetBinormal` | Point，3 分量 | 副法向 = Tangent × Normal，构成右手标架。 |

## 端口 1（开口种子）

每个未删除开口一个顶点：`OpeningArrayValue`（阈值数组幅度均值）、`OpeningIndex`、`SurfacePointId`（代表顶点，默认着色）、`IsInlet`（1 = 勾成 inlet）。
