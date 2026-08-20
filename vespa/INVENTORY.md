# 模块清单（单一事实来源）

对照 **目录 / VTK 模块 / C++ 类 / ParaView XML / 菜单 / 图标 / README**。
增删滤镜时先改本表，再改 [`shyx/README.md`](shyx/README.md) 与根目录 [`VESPA_Plugin_功能说明.md`](../VESPA_Plugin_功能说明.md)。

图例：

- **XML**：上游 VESPA 在 [`smxml/VESPAFilters.xml`](../ParaViewPlugin/smxml/VESPAFilters.xml)（仅 `VESPA_BUILT_WITH_CGAL` 时注册）；SHYX 各一份 `vespa/shyx/<Feature>/SHYX*.xml`，由该模块 `CMakeLists.txt` 的 `vespa_plugin_xml()` 注册。
- **图标**：`自有` = qrc 有对应 `SHYX_*.png`；`fluent` = Vascular 管线 qrc 指向 `_fluent.png`。
- **GROUPS**（`vtk.module`）：`Core` / `Meshing` / `Vascular` / `Flow` / `PointCloud`；表示层保持 `ParaView`。
- **后端**：模块 `vtk.module` 实际依赖，不是类名里的 `CGAL` 字样。

CMake **用户选项**：`VESPA_USE_CGAL`（默认 ON）、`VESPA_USE_VMTK`、`VESPA_USE_SNAPPYHEXMESH`（需 `FOAM_SOURCE_DIR`）、`USE_CERES`（Mesh Smoothing）、`VESPA_BUILD_PV_PLUGIN`、`VESPA_USE_SMP`、`VESPA_USE_MKL`。  
**内部状态**（`CACHE INTERNAL`，由 CGAL 版本 / Ceres 自动 FORCE，不要当 cmake-gui 开关）：`VESPA_ALPHA_WRAPPING`（CGAL ≥ 5.5）、`VESPA_MESH_SMOOTHING`（`USE_CERES`）、`VESPA_ADAPTIVE_REMESHING`（CGAL ≥ 6）。

---

## 公用层（`vespa/Algorithm`、`vespa/Core`）

| 目录 | 模块 NAME | 类 | ParaView | 说明 |
|------|-----------|----|----------|------|
| [`Algorithm/`](Algorithm/) | `vtkCGALAlgorithm` | `vtkCGALPolyDataAlgorithm`, `vtkCGALHelper` | 无 | VTK↔CGAL；仅 CGAL 滤镜 `DEPENDS` |
| [`Core/`](Core/) | `vtkVESPACore` | `vtkVESPAAttributeTransfer` | 无 | 纯 VTK 属性传递 |

---

## 上游 VESPA（`vespa/vespa/`，Filters → VESPA）

Kitware 原版 CGAL 滤镜，全部需 **`VESPA_USE_CGAL`**，只作保留（对照与测试），本清单不逐条列出。菜单由 [`VESPAFilters.xml`](../ParaViewPlugin/smxml/VESPAFilters.xml) 注册（仅 `VESPA_BUILT_WITH_CGAL`）；Alpha Wrapping、Mesh Smoothing 另有 XML（[`vespa/vespa/PolygonMeshProcessing/`](vespa/PolygonMeshProcessing/)），分别依赖 CGAL ≥ 5.5 与 Ceres。无菜单的 PMP 类 `vtkCGALSignedDistanceFunction` 是规则网格上的封闭表面 SDF，勿与 SHYX 点云 SDF 混淆。日常管线用下方 SHYX。

---

## SHYX 滤镜（`vespa/shyx/`，Filters → SHYX；部分兼入 Vascular）

### 网格修复 / 几何

| 界面标签 | 类 | 目录 | XML | 图标 | README | 后端 |
|----------|----|------|-----|------|--------|------|
| SHYX Mesh Checker | `vtkSHYXMeshChecker` | MeshChecker | SHYXMeshChecker.xml | 自有 | [有](shyx/MeshChecker/README.md) | CGAL |
| SHYX Hole Fill (CGAL) | `vtkSHYXHoleFillFilter` | HoleFill | SHYXHoleFillFilter.xml | 自有 | [有](shyx/HoleFill/README.md) | CGAL |
| SHYX Repair Degeneracies (CGAL) | `vtkSHYXRepairDegeneracies` | RepairDegeneracies | SHYXRepairDegeneracies.xml | 自有 | [有](shyx/RepairDegeneracies/README.md) | CGAL |
| SHYX Boolean (CGAL, relaxed) | `vtkSHYXBooleanOperationFilter` | BooleanOperation | SHYXBooleanOperationFilter.xml | 自有 | [有](shyx/BooleanOperation/README.md) | CGAL |
| SHYX Shape Smoothing | `vtkSHYXShapeSmoothing` | ShapeSmoothing | SHYXShapeSmoothing.xml | 自有 | [有](shyx/ShapeSmoothing/README.md) | CGAL |
| SHYX Edge Collapse (CGAL) | `vtkSHYXEdgeCollapse` | EdgeCollapse | SHYXEdgeCollapse.xml | 自有 | [有](shyx/EdgeCollapse/README.md) | CGAL |
| SHYX Adaptive Isotropic Remesher | `vtkSHYXAdaptiveIsotropicRemesher` | AdaptiveIsotropicRemesher | SHYXAdaptiveIsotropicRemesher.xml | 自有 | [有](shyx/AdaptiveIsotropicRemesher/README.md) | CGAL ≥ 6 |
| SHYX Remesh With Endpoint | `vtkSHYXRemeshWithEndpoint` | 同上模块 | SHYXRemeshWithEndpoint.xml | fluent | （同 Remesher README） | CGAL ≥ 6 |
| SHYX Convex Hull | `vtkSHYXConvexHullFilter` | ConvexHullFilter | SHYXConvexHullFilter.xml | 自有 | [有](shyx/ConvexHullFilter/README.md) | VTK |
| SHYX Disconnected Region Fuse | `vtkSHYXDisconnectedRegionFuse` | DisconnectedRegionFuse | SHYXDisconnectedRegionFuse.xml | 自有 | [有](shyx/DisconnectedRegionFuse/README.md) | VTK |
| SHYX Selection Extrude | `vtkSHYXSelectionExtrudeFilter` | SelectionExtrude | SHYXSelectionExtrude.xml | 自有 | [有](shyx/SelectionExtrude/README.md) | VTK |
| SHYX Selection Append Patches | `vtkSHYXSelectionAppendPatches` | SelectionAppendPatches | SHYXSelectionAppendPatches.xml | 自有（复用 Extrude 图） | [有](shyx/SelectionAppendPatches/README.md) | VTK |
| SHYX Point Extrude | `vtkSHYXPointExtrudeFilter` | PointExtrude | SHYXPointExtrude.xml | 自有 | [有](shyx/PointExtrude/README.md) | VTK |
| SHYX Delete Selected Cells | `vtkSHYXDeleteSelectedCellsFilter` | DeleteSelectedCells | SHYXDeleteSelectedCells.xml | 自有 | [有](shyx/DeleteSelectedCells/README.md) | VTK |
| SHYX Flip Selected Cells Winding | `vtkSHYXFlipSelectedCellsWindingFilter` | FlipSelectedCellsWinding | SHYXFlipSelectedCellsWinding.xml | 自有 | [有](shyx/FlipSelectedCellsWinding/README.md) | VTK |
| SHYX Selection: Fill, Alpha Wrap, Union | `vtkSHYXSelectionFillAlphaReunionFilter` | SelectionFillAlphaReunion | SHYXSelectionFillAlphaReunionFilter.xml | 自有 | [有](shyx/SelectionFillAlphaReunion/README.md) | CGAL ≥ 5.5 |
| SHYX Minimum OBB | `vtkSHYXMinimumOBBFilter` | MinimumOBB | SHYXMinimumOBB.xml | 自有 | [有](shyx/MinimumOBB/README.md) | CGAL |
| SHYX Enhanced Ruler | `vtkSHYXEnhancedRuler` | EnhancedRuler | SHYXEnhancedRuler.xml | 自有 | [有](shyx/EnhancedRuler/README.md) | VTK |

### 血管 / 体积网格（含 Vascular 工具条）

Vascular 顺序（[`VESPAVascularCategory.xml`](../ParaViewPlugin/smxml/VESPAVascularCategory.xml)）：骨架 → 切端 → 平面裁 → 端点重网格 → TetGen → PDC → 边界分配。

| 界面标签 | 类 | 目录 | XML | 图标 | README | 后端 | Vascular |
|----------|----|------|-----|------|--------|------|----------|
| SHYX Skeleton Extraction | `vtkCGALSkeletonExtraction` | SkeletonExtraction | SHYXSkeletonExtraction.xml | fluent | [有](shyx/SkeletonExtraction/README.md) | CGAL | 1 |
| SHYX Vessel End Clipper | `vtkCGALVesselEndClipper` | VesselEndClipper | SHYXVesselEndClipper.xml | fluent | [有](shyx/VesselEndClipper/README.md) | CGAL | 2 |
| SHYX Selection Plane Clipper | `vtkSHYXSelectionPlaneClipper` | SelectionPlaneClipper | SHYXSelectionPlaneClipper.xml | fluent | [有](shyx/SelectionPlaneClipper/README.md) | VTK | 3 |
| SHYX Remesh With Endpoint | （见上） | | | fluent | | CGAL ≥ 6 | 4 |
| SHYX Surface to Volume Mesh | `vtkCGALSurfaceToVolumeMesh` | SurfaceToVolumeMesh | SHYXSurfaceToVolumeMesh.xml | 自有 | [有](shyx/SurfaceToVolumeMesh/README.md) | CGAL | |
| SHYX TetGen | `vtkSHYXTetGen` | TetGen | SHYXTetGen.xml | fluent | [有](shyx/TetGen/README.md) | TetGen | 5 |
| SHYX TetGen Mesh Optimize | `vtkSHYXTetGenMeshOptimize` | 同上模块 | SHYXTetGenMeshOptimize.xml | 自有 | （同 TetGen README） | TetGen | |
| SHYX SnappyHexMesh | `vtkSHYXSnappyHexMesh` | SnappyHexMesh | SHYXSnappyHexMesh.xml | 自有 | [有](shyx/SnappyHexMesh/README.md) | 可选 `VESPA_USE_SNAPPYHEXMESH` + `FOAM_SOURCE_DIR` | |
| SHYX Tet Mesh Region Partition | `vtkSHYXTetMeshRegionPartition` | TetMeshRegionPartition | SHYXTetMeshRegionPartition.xml | 自有 | [有](shyx/TetMeshRegionPartition/README.md) | VTK | |
| SHYX DataSet To Partitioned Collection | `vtkSHYXDataSetToPartitionedCollection` | DataSetToPartitionedCollection | SHYXDataSetToPartitionedCollection.xml | fluent | [有](shyx/DataSetToPartitionedCollection/README.md) | VTK | 6 |
| SHYX Partitioned Collection Boundary Assignment | `vtkSHYXPartitionedCollectionBoundaryAssignment` | PartitionedCollectionBoundaryAssignment | SHYXPartitionedCollectionBoundaryAssignment.xml | fluent | [有](shyx/PartitionedCollectionBoundaryAssignment/README.md) | VTK | 7 |
| SHYX Partitioned Collection Boundary Fields | `vtkSHYXPartitionedCollectionBoundaryFields` | PartitionedCollectionBoundaryFields | SHYXPartitionedCollectionBoundaryFields.xml | 自有 | [有](shyx/PartitionedCollectionBoundaryFields/README.md) | VTK | |
| SHYX Partitioned Collection WSL Simulation | `vtkSHYXPartitionedCollectionWslSimulation` | PartitionedCollectionWslSimulation | SHYXPartitionedCollectionWslSimulation.xml | 自有 | [有](shyx/PartitionedCollectionWslSimulation/README.md) | VTK | |
| SHYX Partitioned Collection To OpenFOAM | `vtkSHYXPartitionedCollectionToOpenFOAM` | PartitionedCollectionToOpenFOAM | SHYXPartitionedCollectionToOpenFOAM.xml | 复用 Snappy | [有](shyx/PartitionedCollectionToOpenFOAM/README.md) | VTK | |
| SHYX VMTK Centerlines | `vtkSHYXVmtkPolyDataCenterlines` | VmtkPolyDataCenterlines | SHYXVmtkPolyDataCenterlines.xml | 自有 | [有](shyx/VmtkPolyDataCenterlines/README.md) | VMTK | |
| SHYX VMTK Opening Centerlines | `vtkSHYXVmtkOpeningCenterlines` | VmtkOpeningCenterlines | SHYXVmtkOpeningCenterlines.xml | 自有 | [有](shyx/VmtkOpeningCenterlines/README.md) | VMTK | |
| SHYX Vascular Stent Placement | `vtkSHYXVascularStentPlacement` | VascularStentPlacement | SHYXVascularStentPlacement.xml | 自有 | [有](shyx/VascularStentPlacement/README.md) | VTK | |
| SHYX Endpoint Stent Placement | `vtkSHYXEndpointStentPlacement` | EndpointStentPlacement | SHYXEndpointStentPlacement.xml | 自有 | [有](shyx/EndpointStentPlacement/README.md) | VTK | |

支架/圆柱 **widget representation** XML：与对应滤镜同目录的 `SHYXImplicitCylinderWidgetRepresentation.xml`、`SHYXEndpointStentWidgetRepresentation.xml`（非 Filters 菜单）。

### 流场

| 界面标签 | 类 | 目录 | XML | 图标 | README | 后端 |
|----------|----|------|-----|------|--------|------|
| SHYX Vortex Criteria | `vtkVortexCriteriaFilter` | VortexCriteria | SHYXVortexCriteria.xml | 自有 | [有](shyx/VortexCriteria/README.md) | VTK |
| SHYX FTLE Filter | `vtkFTLEFilter` | FTLEFilter | SHYXFTLEFilter.xml | 自有 | [有](shyx/FTLEFilter/README.md) | VTK |
| SHYX Clebsch Map Filter | `vtkSHYXClebschMapFilter` | ClebschMapFilter | SHYXClebschMapFilter.xml | 自有 | [有](shyx/ClebschMapFilter/README.md) | VTK（可选 MKL） |
| SHYX Vector Field Topology | `vtkSHYXVectorFieldTopology` | VectorFieldTopology | SHYXVectorFieldTopology.xml | 自有 | [有](shyx/VectorFieldTopology/README.md) | VTK（包装 `vtkVectorFieldTopology`） |
| SHYX Auto Streamline | `vtkSHYXAutoStreamline` | AutoStreamline | SHYXAutoStreamline.xml | 自有 | [有](shyx/AutoStreamline/README.md) | VTK |
| SHYX Bidirectional Streamline Merge | `vtkSHYXBidirectionalStreamlineMerge` | BidirectionalStreamlineMerge | SHYXBidirectionalStreamlineMerge.xml | 自有 | [有](shyx/BidirectionalStreamlineMerge/README.md) | VTK |

### 点云 / 采样 / 映射

| 界面标签 | 类 | 目录 | XML | 图标 | README | 后端 |
|----------|----|------|-----|------|--------|------|
| SHYX Density-Based Volume Sampler | `vtkSHYXDensityBasedSampler` | DensityBasedSampler | SHYXDensityBasedVolumeSampler.xml | 自有 | [有](shyx/DensityBasedSampler/README.md) | VTK |
| SHYX Array Probability Point Cull | `vtkSHYXArrayProbabilityPointCull` | ArrayProbabilityPointCull | SHYXArrayProbabilityPointCull.xml | 自有 | [有](shyx/ArrayProbabilityPointCull/README.md) | VTK |
| SHYX Radius Neighbor Count | `vtkSHYXRadiusNeighborCount` | RadiusNeighborCount | SHYXRadiusNeighborCount.xml | 自有 | [有](shyx/RadiusNeighborCount/README.md) | VTK |
| SHYX Point Cloud Surface SDF | `vtkSHYXPointCloudSurfaceSignedDistance` | PointCloudSurfaceSDF | SHYXPointCloudSurfaceSDF.xml | 自有 | [有](shyx/PointCloudSurfaceSDF/README.md) | **VTK only** |
| SHYX Geodesic Distance | `vtkGeodesicDistanceFilter` | GeodesicDistance | SHYXGeodesicDistance.xml | 自有 | [有](shyx/GeodesicDistance/README.md) | VTK |
| SHYX Surface Tip Extractor | `vtkSurfaceTipExtractor` | SurfaceTipExtractor | SHYXSurfaceTipExtractor.xml | 自有 | [有](shyx/SurfaceTipExtractor/README.md) | VTK |
| SHYX Array Curve Mapper | `vtkArrayCurveMapper` | ArrayCurveMapper | SHYXArrayCurveMapper.xml | 自有 | [有](shyx/ArrayCurveMapper/README.md) | VTK |

### 工具

| 界面标签 | 类 | 目录 | XML | 图标 | README | 后端 |
|----------|----|------|-----|------|--------|------|
| SHYX AI Assistant | `pqSHYXAIAssistantPanel` | AIAssistant | —（View dock，`ParaViewPlugin/AIAssistant/`） | — | [有](shyx/AIAssistant/README.md) | 客户端 Qt（OpenAI 兼容 HTTP）。无 SM XML / 无 VTK 模块；不要 `SHYXAIAssistant()` |

---

## 表示（Display 面板，非 Filters 菜单）

| 界面名 | 类 | 目录 | XML | README |
|--------|----|------|-----|--------|
| Pulse Glyphs | `vtkPulseGlyphRepresentation` | PulseGlyphRepresentation | PulseGlyphRepresentation.xml | [有](shyx/PulseGlyphRepresentation/README.md) |
| Animated Streamline | `vtkAnimatedStreamlineRepresentation` | AnimatedStreamlineRepresentation | AnimatedStreamlineRepresentation.xml | [有](shyx/AnimatedStreamlineRepresentation/README.md) |
| Point Label | `vtkPointLabelRepresentation` | PointLabelRepresentation | PointLabelRepresentation.xml | [有](shyx/PointLabelRepresentation/README.md) |

---

## 仅客户端 Qt（无 VTK 模块）

| 功能 | 代码 | 图标 |
|------|------|------|
| Sphere Selection | `ParaViewPlugin/selection/SphereSelection/` | `SHYX_Sphere_Selection.svg` |
| Grow Selection With Similar | `ParaViewPlugin/selection/GrowSelectionWithSimilar/` | `SHYX_Grow_Selection_With_Similar.svg` |
| Select Block（3D 视图 block 右键） | `ParaViewPlugin/selection/SelectBlock/` | ParaView `pqSelectBlock.svg` |
| Select Similar（选择右键，By Normal 一次 Grow 完） | `ParaViewPlugin/selection/SelectSimilar/` | 复用 Grow 图标 |
| Fill Interior（选择右键，填充被选区围住的未选面） | `ParaViewPlugin/selection/SelectSimilar/` + Grow controller | （无独立图标） |
| Select All（选择右键，全选当前连通区域） | `ParaViewPlugin/selection/SelectSimilar/` + Grow controller | （无独立图标） |
| Invert Selection（选择右键，反选） | `ParaViewPlugin/selection/SelectSimilar/` + Grow controller | （无独立图标） |
| Vascular 菜单/工具条 | `ParaViewPlugin/VascularCategory/` + [`smxml/VESPAVascularCategory.xml`](../ParaViewPlugin/smxml/VESPAVascularCategory.xml) | （各滤镜 fluent 图标） |

---

## 上游 VESPA 与 SHYX

**Filters → VESPA** 是 Kitware 原版滤镜，只作保留（对照上游行为与测试）；同类任务一般用 **SHYX**，多为升级版（诊断更细、开放网格布尔、多算法平滑、曲率自适应重网格等）。体积网格、流场、点云、选择工具等只有 SHYX。血管 CFD 主路径走 **Filters → Vascular**。
