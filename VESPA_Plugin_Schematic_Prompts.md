# VESPA / ParaView 插件 — 滤镜算法原理图解生成提示词

本文档为 **VESPAPlugin** (`vespa/shyx/` 目录下) 的各核心算法模块提供了一套**风格统一**的「四格技术原理图解（Schematic Overview）」生成提示词。

这套提示词旨在通过类似学术论文或技术文档中的分步流程图，生动直观地向用户解释底层的三维几何与流体力学算法黑魔法。适用于 Midjourney、Stable Diffusion、DALL·E 等 AI 图像生成工具。

> **🌟 更新说明**：所有的 `Panel 1` 均已着重强调了该算法的**最终目的与全局总览（What & Why）**，随后的 `Panel 2 ~ 4` 逐步深入技术与实现细节。

---

## 🎨 使用方式与全局风格

为了保证所有模块生成的图解画风统一，请在生成每张图时，将下方提供的**专属提示词**与**全局风格后缀**拼接在一起。

### 全局风格后缀（必须追加到每条提示词末尾）

**English（推荐全英文生图）：**
```text
, A 4-panel technical schematic overview diagram explaining an algorithm step-by-step. Scientific visualization style, flat vector illustration, light blue, cyan, and pastel color palette, clean thick strokes, pure white background, 3D models shown in each step with instructional arrows and abstract mathematical annotations, professional academic paper style, highly legible layout.
```

**中文备选释义：**
四格技术原理图解，逐步解释算法。科学可视化风格，扁平矢量插画，浅蓝、青色和柔和色调，干净粗线条，纯白背景。每步展示3D模型，带有指示箭头和抽象数学标注，专业学术论文风格，排版清晰易读。

---

## 🧩 SHYX 模块专属提示词 (Specific Prompts)

复制以下英文提示词，并在末尾加上全局后缀即可生成对应的算法图解。

### 1. SurfaceTipExtractor (表面尖端提取器)
```text
Panel 1: Overview of the goal: identifying and extracting sharp, pointed features on a bumpy 3D model for geometric analysis. Panel 2: Defining a search radius sphere around a target point to find geodesic neighbors. Panel 3: Calculating the centroid of the local neighborhood and measuring its deviation from the point. Panel 4: Final output with sharp tips highlighted and scored, distinguishing them from flat regions.
```

### 2. GeodesicDistance (测地线距离计算器)
```text
Panel 1: Overview of the goal: finding the shortest walkable path across a complex 3D hilly terrain, avoiding straight-line penetration. Panel 2: Initializing Dijkstra's algorithm from a single glowing source point on the mesh. Panel 3: Wavefront propagation along the surface edges, calculating accumulated distances. Panel 4: Final output displaying concentric iso-contours representing true geodesic distances over the hills.
```

### 3. SkeletonExtraction (3D骨架提取)
```text
Panel 1: Overview of the goal: extracting a thin 1D centerline curve skeleton from a thick, closed 3D model. Panel 2: Applying mean curvature flow with inward-pointing arrows indicating the mesh shrinking process. Panel 3: The 3D volume contracting and collapsing into a connected 1D graph structure. Panel 4: Final output showing the extracted curve skeleton centered inside the transparent original shape.
```

### 4. VesselEndClipper (血管端点裁剪器)
```text
Panel 1: Overview of the goal: preparing blood vessel models for CFD simulation by creating clean, flat cut ends. Panel 2: Identifying uneven, ragged endpoints and calculating the local centerline tangent direction. Panel 3: Using a cutting plane perpendicular to the tangent to slice off the ragged ends. Panel 4: Final output showing a watertight vessel mesh with perfectly flat, capped circular ends.
```

### 5. SurfaceToVolumeMesh (曲面到体网格生成器)
```text
Panel 1: Overview of the goal: converting a hollow 3D surface shell into a solid tetrahedral volume for finite element analysis. Panel 2: Detecting and preserving sharp feature edges of the input geometry. Panel 3: Using Constrained Delaunay refinement to insert points and generate internal tetrahedra. Panel 4: Final cutaway view revealing a solid interior completely filled with high-quality uniform tetrahedra.
```

### 6. PointCloudSurfaceSDF (点云到曲面有符号距离场)
```text
Panel 1: Overview of the goal: computing the exact distance and inside/outside status of a scattered point cloud relative to a 3D surface. Panel 2: Using a spatial cell locator to rapidly find the nearest triangle face for each point. Panel 3: Calculating perpendicular distances and using angle-weighted pseudo-normals to determine the sign. Panel 4: Final output showing points colored red (inside) and blue (outside) representing the signed distance field.
```

### 7. DisconnectedRegionFuse (断开区域融合)
```text
Panel 1: Overview of the goal: repairing a fractured 3D model by stitching together disconnected islands across small gaps. Panel 2: Identifying independent disconnected regions and locating boundary vertices within a tolerance distance. Panel 3: Using union-find clustering to group nearby points and calculating their common centroid. Panel 4: Final output showing a unified mesh where previously broken regions are seamlessly fused together.
```

### 8. DensityBasedSampler (基于密度的点云采样)
```text
Panel 1: Overview of the goal: scattering random sample points inside a 3D volume, controlled by a scalar density field. Panel 2: Generating a dense uniform Cartesian bounding grid of candidate points around the object. Panel 3: Discarding points outside the volume using implicit distance evaluation. Panel 4: Final output point cloud where points cluster densely in high-value areas and sparsely in low-value regions based on survival probability.
```

### 9. AnimatedStreamlineRepresentation (动态流线可视化)
```text
Panel 1: Overview of the goal: bringing static fluid streamlines to life using GPU shader animation without recomputing geometry. Panel 2: Vertex shader encoding integration time or arc length into texture coordinates. Panel 3: Fragment shader generating a periodic, comet-like pulse envelope based on time. Panel 4: Final animated output displaying glowing pulses continuously flowing along the streamline trajectories.
```

### 10. DataSetToPartitionedCollection (数据分区集合转换)
```text
Panel 1: Overview of the goal: organizing an unstructured mesh into IOSS/Exodus compatible blocks, side sets, and node sets. Panel 2: Extracting outer boundary surface triangles from the solid tetrahedral volume block. Panel 3: Splitting the boundary surface into distinct patches based on sharp feature angles. Panel 4: Final assembly tree output showing properly partitioned tet blocks and separated boundary side sets.
```

### 11. VortexCriteria (涡流识别判据)
```text
Panel 1: Overview of the goal: detecting and visualizing swirling vortex tubes hidden within chaotic 3D fluid velocity fields. Panel 2: Computing the velocity gradient tensor and splitting it into symmetric strain and antisymmetric rotation components. Panel 3: Evaluating advanced mathematical criteria like Q-criterion or Lambda-2 to identify rotation-dominant regions. Panel 4: Final output isolating and rendering 3D isosurfaces of the extracted vortex cores.
```

### 12. PulseGlyphRepresentation (脉冲体素表示法)
```text
Panel 1: Overview of the goal: creating a beating, pulsating animation for 3D data glyphs to represent dynamic variables. Panel 2: Instancing 3D arrow or cone glyphs at spatial data points. Panel 3: Modulating glyph scale and orientation using a periodic time-based mathematical envelope function. Panel 4: Final animated visualization showing a field of glyphs rhythmically growing and shrinking like a heartbeat.
```

### 13. AdaptiveIsotropicRemesher (自适应各向同性重网格化)
```text
Panel 1: Overview of the goal: optimizing a 3D mesh by allocating tiny triangles to detailed curves and large triangles to flat areas. Panel 2: Detecting sharp feature edges and applying constraints to protect them from distortion. Panel 3: Constructing an adaptive sizing field governed by local surface curvature. Panel 4: Final output showing a smooth isotropic mesh with perfectly graded triangle sizes transitioning from high to low curvature.
```

### 14. ClebschMapFilter (Clebsch映射滤镜)
```text
Panel 1: Overview of the goal: generating a smooth, singularity-free texture mapping (Clebsch Map) to visualize fluid vortex topologies. Panel 2: Initializing random complex wave functions on a 4D sphere (S3) for each mesh vertex. Panel 3: Iteratively optimizing a massive sparse Hermitian linear system to align gradients with the velocity field. Panel 4: Final output displaying elegant, intertwined level-set ribbons that highlight the fluid's topological structure.
```

### 15. VortexCoreTest (涡核提取)
```text
Panel 1: Overview of the goal: extracting precise 1D centerline polylines representing the exact center of fluid tornadoes (vortex cores). Panel 2: Computing the velocity field, acceleration field, and higher-order jerk tensor. Panel 3: Searching for spatial regions where the velocity vector and acceleration vector are strictly parallel. Panel 4: Final output showing clear, continuous 1D polyline skeletons tracing the core of the vortices.
```

### 16. ArrayCurveMapper (数组曲线映射器)
```text
Panel 1: Overview of the goal: smoothly remapping a raw dataset array into a new range using a customizable non-linear transfer curve. Panel 2: Clamping extreme outliers to ensure all input values fall within a safe bounding range. Panel 3: Translating the clamped values through an editable piecewise linear curve graph. Panel 4: Final output showing the remapped data values creating a visually distinct, high-contrast color distribution on the model.
```

### 17. ArrayProbabilityPointCull (基于概率的点云剔除)
```text
Panel 1: Overview of the goal: randomly culling a dense point cloud where each point's survival rate is dictated by a local probability value. Panel 2: Clamping the input probability scalar array strictly between 0% and 100%. Panel 3: Generating deterministic random numbers and comparing them against each point's probability threshold. Panel 4: Final decimated point cloud output where points with high probability survive and others vanish.
```

### 18. FTLEFilter (有限时间李雅普诺夫指数)
```text
Panel 1: Overview of the goal: identifying invisible barriers and flow separators (Lagrangian Coherent Structures) in a moving fluid. Panel 2: Injecting virtual particles and integrating their trajectories over a finite time interval. Panel 3: Computing the flow map gradient and deriving the Cauchy-Green deformation tensor to measure particle stretching. Panel 4: Final output displaying sharp FTLE ridges indicating regions where particles rapidly diverge or converge.
```

### 19. RadiusNeighborCount (固定半径邻域点计数)
```text
Panel 1: Overview of the goal: calculating local point density by counting exactly how many neighbors exist within a specific spherical radius. Panel 2: Building a high-performance static spatial point locator grid to accelerate spatial queries. Panel 3: Centering a search sphere at a target point and tallying all surrounding points within the boundary. Panel 4: Final point cloud output where each point is colored by a scalar value representing its local neighbor population.
```

### 20. BidirectionalStreamlineMerge (双向流线合并)
```text
Panel 1: Overview of the goal: stitching together broken forward and backward streamline halves into one continuous trajectory. Panel 2: Identifying matching pairs of streamlines that originated from the exact same initial seed point. Panel 3: Reversing the point order of the backward segment and fusing it to the forward segment, eliminating the duplicate seed. Panel 4: Final output showing a unified, smooth polyline with a newly calculated continuous arc-length integration array.
```

### 21. TetGen (TetGen 四面体剖分)
```text
Panel 1: Overview of the goal: generating a high-quality solid tetrahedral mesh from a hollow watertight surface for engineering simulations. Panel 2: Feeding the closed 3D boundary into the Constrained Delaunay Tetrahedralization (CDT) engine. Panel 3: Iteratively refining the internal tetrahedra to satisfy strict radius-edge ratio and minimum angle quality constraints. Panel 4: Final cutaway visualization revealing a robust, dense internal mesh structure without altering the original surface.
```
