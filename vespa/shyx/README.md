# SHYX 模块总览

SHYX 是本仓库里 **一个作者命名空间**（[`vespa/shyx/`](.)），不是整个工程的中心。仓库布局见 [`../README.md`](../README.md)。**模块 ↔ 类 ↔ XML ↔ 菜单 ↔ 图标** 的完整对照以 [`../INVENTORY.md`](../INVENTORY.md) 为准。

每个子目录通常是独立 VTK 模块（自有 `vtk.module` / `DEPENDS`）。例外：[**AI Assistant**](./AIAssistant/README.md) 的**用户界面**是 View dock（`pqSHYXAI*`）。目录里仍有遗留模块 `vtkSHYXAIAssistant`；`SHYXAIAssistant.xml` **未**加入 `SERVER_MANAGER_XML`，Filters 菜单不应出现该节点，不要 `SHYXAIAssistant()`。需要 CGAL 的才依赖 `vtkCGALAlgorithm`；TetGen / VMTK / 纯 VTK 不要走 CGAL 基类。纯 VTK 的 Density Sampler / 点云 SDF 类名是 `vtkSHYX*`。骨架、切端、Surface-to-Volume 的 **vtk.module NAME** 是 `vtkSHYX*`，**C++ / XML `class=`** 仍是 `vtkCGAL*`。

`vtk.module` 的 **GROUPS**：`Meshing` / `Vascular` / `Flow` / `PointCloud`（表示层为 `ParaView`）。

## 网格修复 / 几何

* [**Adaptive Isotropic Remesher**](./AdaptiveIsotropicRemesher/README.md) — 曲率自适应各向同性重网格（CGAL ≥ 6）；同模块含 **Remesh With Endpoint**
* [**Mesh Checker**](./MeshChecker/README.md) — 汤边 / 边界环 / 自交诊断，可选修复
* [**Hole Fill**](./HoleFill/README.md) / [**Repair Degeneracies**](./RepairDegeneracies/README.md) / [**Edge Collapse**](./EdgeCollapse/README.md) / [**Boolean (relaxed)**](./BooleanOperation/README.md) / [**Shape Smoothing**](./ShapeSmoothing/README.md) — CGAL PMP
* [**Convex Hull**](./ConvexHullFilter/README.md)
* [**Disconnected Region Fuse**](./DisconnectedRegionFuse/README.md)
* [**Selection Extrude**](./SelectionExtrude/README.md) / [**Selection Append Patches**](./SelectionAppendPatches/README.md) / [**Point Extrude**](./PointExtrude/README.md) / [**Delete Selected Cells**](./DeleteSelectedCells/README.md) / [**Flip Selected Cells Winding**](./FlipSelectedCellsWinding/README.md)
* [**Selection: Fill, Alpha Wrap, Union**](./SelectionFillAlphaReunion/README.md) — CGAL ≥ 5.5
* [**Minimum OBB**](./MinimumOBB/README.md)
* [**Enhanced Ruler**](./EnhancedRuler/README.md)

## 血管与体积网格

**Vascular 工具条**（`VESPAVascularCategory.xml`，顺序固定）：骨架 → 切端 → 平面裁 → 端点重网格 → TetGen → PDC → 边界分配。下列其余项只在 **Filters → SHYX**。

* [**Skeleton Extraction**](./SkeletonExtraction/README.md) — **Vascular**
* [**Vessel End Clipper**](./VesselEndClipper/README.md) — **Vascular**
* [**Selection Plane Clipper**](./SelectionPlaneClipper/README.md) — **Vascular**
* [**TetGen**](./TetGen/README.md) — **Vascular**；同模块含 **TetGen Mesh Optimize**
* [**DataSet To Partitioned Collection**](./DataSetToPartitionedCollection/README.md) — **Vascular**
* [**Partitioned Collection Boundary Assignment**](./PartitionedCollectionBoundaryAssignment/README.md) — **Vascular**
* [**Surface To Volume Mesh**](./SurfaceToVolumeMesh/README.md) — SHYX only（CGAL Mesh_3）
* [**SnappyHexMesh**](./SnappyHexMesh/README.md) — 需 `VESPA_USE_SNAPPYHEXMESH`；SHYX only
* [**Tet Mesh Region Partition**](./TetMeshRegionPartition/README.md)
* [**Partitioned Collection Boundary Fields**](./PartitionedCollectionBoundaryFields/README.md)
* [**Partitioned Collection WSL Simulation**](./PartitionedCollectionWslSimulation/README.md)
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
* **Select Block** — 3D 视图右键复合 block（如 `Part_1`）全选该块 cell；`ParaViewPlugin/SelectBlock/`
* **Select Similar** — 有 cell 选择时右键 **Select Similar → By Normal**，按法向一次 Grow 完；`ParaViewPlugin/SelectSimilar/`（阈值与标题栏 Grow 共用）

## 表示

* [**Pulse Glyph Representation**](./PulseGlyphRepresentation/README.md) — 动画管理器在 `ParaViewPlugin/pqPulseGlyphAnimationManager`
* [**Animated Streamline Representation**](./AnimatedStreamlineRepresentation/README.md) — 动画管理器在 `ParaViewPlugin/pqAnimatedStreamlineAnimationManager`
* [**Point Label Representation**](./PointLabelRepresentation/README.md) — Display 面板点标签

---

构建选项、成对滤镜说明、以及插件菜单总表：根目录 [**VESPA_Plugin_功能说明.md**](../../VESPA_Plugin_功能说明.md)。
