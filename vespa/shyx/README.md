# SHYX 模块总览

SHYX 是本仓库里 **一个作者命名空间**（[`vespa/shyx/`](.)），不是整个工程的中心。仓库布局见 [`../README.md`](../README.md)。**模块 ↔ 类 ↔ XML ↔ 菜单 ↔ 图标** 的完整对照以 [`../INVENTORY.md`](../INVENTORY.md) 为准。

每个子目录通常是独立 VTK 模块（自有 `vtk.module` / `DEPENDS` / **`SHYX*.xml`**，由 `vespa_plugin_xml()` 注册）。例外：[**AI Assistant**](./AIAssistant/README.md) 的**用户界面**是 View dock（`ParaViewPlugin/AIAssistant/pqSHYXAI*`），本目录只有文档，没有 VTK 模块或 SM XML；不要 `SHYXAIAssistant()`。需要 CGAL 的才依赖 `vtkCGALAlgorithm`；TetGen / VMTK / 纯 VTK 不要走 CGAL 基类。纯 VTK 的 Density Sampler / 点云 SDF 类名是 `vtkSHYX*`。骨架、切端、Surface-to-Volume 的 **vtk.module NAME** 是 `vtkSHYX*`，**C++ / XML `class=`** 仍是 `vtkCGAL*`。

`vtk.module` 的 **GROUPS**：`Meshing` / `Vascular` / `Flow` / `PointCloud`（表示层为 `ParaView`）。

## 网格修复 / 几何

* [**Adaptive Isotropic Remesher**](./AdaptiveIsotropicRemesher/README.md) — 曲率自适应各向同性重网格（CGAL ≥ 6）；同模块含 **Remesh With Endpoint**
* [**Mesh Checker**](./MeshChecker/README.md) — 汤边 / 边界环 / 自交诊断，可选修复
* [**Auto Mesh Repair**](./AutoMeshRepair/README.md) — 自交分簇后局部 Alpha Wrap + smooth（CGAL ≥ 5.5；Mesh Checker 交叉面的下一步）
* [**Hole Fill**](./HoleFill/README.md) / [**Repair Degeneracies**](./RepairDegeneracies/README.md) / [**Edge Collapse**](./EdgeCollapse/README.md) / [**Boolean (relaxed)**](./BooleanOperation/README.md) / [**Shape Smoothing**](./ShapeSmoothing/README.md) — CGAL PMP
* [**Convex Hull**](./ConvexHullFilter/README.md)
* [**Disconnected Region Fuse**](./DisconnectedRegionFuse/README.md)
* [**Selection Extrude**](./SelectionExtrude/README.md) / [**Selection Append Patches**](./SelectionAppendPatches/README.md) / [**Point Extrude**](./PointExtrude/README.md) / [**Delete Selected Cells**](./DeleteSelectedCells/README.md) / [**Extract Selection**](./ExtractSelectedCells/README.md) / [**Flip Selected Cells Winding**](./FlipSelectedCellsWinding/README.md)
* [**Selection: Fill, Alpha Wrap, Union**](./SelectionFillAlphaReunion/README.md) — CGAL ≥ 5.5
* [**Minimum OBB**](./MinimumOBB/README.md)
* [**Enhanced Ruler**](./EnhancedRuler/README.md)

## 血管与体积网格

**Vascular 工具条**（`ParaViewPlugin/smxml/VESPAVascularCategory.xml`，顺序固定）：骨架 → 切端 → **骨架+切端组合** → 平面裁 → 端点重网格 → TetGen → PDC → 边界分配。下列其余项只在 **Filters → SHYX**。

* [**Skeleton Extraction**](./SkeletonExtraction/README.md) — **Vascular**
* [**Vessel End Clipper**](./VesselEndClipper/README.md) — **Vascular**
* [**Skeleton End Clipper**](./SkeletonEndClipper/README.md) — **Vascular**（骨架 + 切端一节点；原两个滤镜不变）
* [**Selection Plane Clipper**](./SelectionPlaneClipper/README.md) — **Vascular**
* [**TetGen**](./TetGen/README.md) — **Vascular**；同模块含 **TetGen Mesh Optimize**
* [**DataSet To Partitioned Collection**](./DataSetToPartitionedCollection/README.md) — **Vascular**
* [**Partitioned Collection Boundary Assignment**](./PartitionedCollectionBoundaryAssignment/README.md) — **Vascular**
* [**Surface To Volume Mesh**](./SurfaceToVolumeMesh/README.md) — SHYX only（CGAL Mesh_3）
* [**SnappyHexMesh**](./SnappyHexMesh/README.md) — 需 `VESPA_USE_SNAPPYHEXMESH`；PDC 分块当 STL patch；网格成功后可按块写 `0/shyx_BoundaryVariableN`；SHYX only
* [**Extended Feature Edge Mesh**](./ExtendedFeatureEdgeMesh/README.md) — 需 `VESPA_USE_SNAPPYHEXMESH`；OpenFOAM `extendedFeatureEdgeMesh` 分类特征边/点；接到 SnappyHexMesh Feature edges
* [**OpenFOAM eMesh Reader**](./EMeshReader/README.md) — File → Open / Sources；读 `.eMesh` 与 `extendedFeatureEdgeMesh` ASCII（不支持 gzip）；不依赖 OpenFOAM 运行时
* [**Tet Mesh Region Partition**](./TetMeshRegionPartition/README.md)
* [**Partitioned Collection Boundary Fields**](./PartitionedCollectionBoundaryFields/README.md)
* [**Partitioned Collection WSL Simulation**](./PartitionedCollectionWslSimulation/README.md)
* [**Partitioned Collection To OpenFOAM**](./PartitionedCollectionToOpenFOAM/README.md) — 体网格 PDC → `polyMesh`；SHYX only
* [**VMTK Centerlines**](./VmtkPolyDataCenterlines/README.md) / [**Opening Centerlines**](./VmtkOpeningCenterlines/README.md) — 需 `VESPA_USE_VMTK`
* [**Vascular Stent Placement**](./VascularStentPlacement/README.md) / [**Endpoint Stent Placement**](./EndpointStentPlacement/README.md)

## 流场

* [**Vortex Criteria**](./VortexCriteria/README.md)
* [**FTLE Filter**](./FTLEFilter/README.md)
* [**Clebsch Map Filter**](./ClebschMapFilter/README.md) — 可选 MKL
* [**Vector Field Topology**](./VectorFieldTopology/README.md) — 包装 VTK `vtkVectorFieldTopology`
* [**Auto Streamline**](./AutoStreamline/README.md)
* [**Bidirectional Streamline Merge**](./BidirectionalStreamlineMerge/README.md)

## 点云 / 采样 / 映射

* [**Density-Based Sampler**](./DensityBasedSampler/README.md) — 纯 VTK
* [**Array Probability Point Cull**](./ArrayProbabilityPointCull/README.md)
* [**Radius Neighbor Count**](./RadiusNeighborCount/README.md)
* [**Point Cloud Surface SDF**](./PointCloudSurfaceSDF/README.md) — 纯 VTK；勿与 PMP 体素 SDF 混淆
* [**Geodesic Distance**](./GeodesicDistance/README.md)
* [**Surface Tip Extractor**](./SurfaceTipExtractor/README.md)
* [**Array Curve Mapper**](./ArrayCurveMapper/README.md)

## 工具

* [**AI Assistant**](./AIAssistant/README.md) — **View → SHYX AI Assistant** 可勾选停靠窗口；Send / Run script 跑 ParaView Python（需 ParaView 开启 Python）
* **Proximity Gap Selection** — RenderView 标题栏：按 ε 选出不同连通域（含封闭壳体）的近距接触带。按钮为开关：打开后才计算；打开时在按钮上滚轮改 ε。默认 **Single nearest region**。可选 **Toward opposite center**（默认关）。结果可交给 [**Selection: Fill, Alpha Wrap, Union**](./SelectionFillAlphaReunion/README.md)。`ParaViewPlugin/selection/ProximityGapSelection/`
* **Select Block** — 3D 视图右键复合 block（如 `Part_1`）先清当前选择再全选该块 cell；`ParaViewPlugin/selection/SelectBlock/`
* **Select Similar** — 有 cell 选择时右键 **Select Similar → By Normal**，按法向一次 Grow 完；`ParaViewPlugin/selection/SelectSimilar/`（阈值与标题栏 Grow 共用）
* **Fill Interior** — 有 cell 选择时右键 **Fill Interior**，把被当前选区完全围住的未选面补进选择（开放网格上仍连到开口的区域不填）
* **Select All** — 有 cell 选择时右键 **Select All**，全选当前选区所在边连通区域（不相接的其它壳不选）
* **Invert Selection** — 有 cell 选择时右键 **Invert Selection**，反选当前数据集上的 cell

## 表示

* [**Pulse Glyph Representation**](./PulseGlyphRepresentation/README.md) — 动画管理器在 `ParaViewPlugin/Representations/pqPulseGlyphAnimationManager`
* [**Animated Streamline Representation**](./AnimatedStreamlineRepresentation/README.md) — 动画管理器在 `ParaViewPlugin/Representations/pqAnimatedStreamlineAnimationManager`
* [**Point Label Representation**](./PointLabelRepresentation/README.md) — Display 面板点标签

---

构建选项、成对滤镜说明、以及插件菜单总表：根目录 [**VESPA_Plugin_功能说明.md**](../../VESPA_Plugin_功能说明.md)。
