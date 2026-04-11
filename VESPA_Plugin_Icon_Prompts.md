# VESPA / ParaView 插件 — 滤镜图标图像生成提示词

本文档为 **VESPAPlugin** 中各 **Filter**（及唯一 **Source**）各提供一条**风格统一**的提示词，侧重**算法意象**与**可识别的视觉隐喻**，便于 Midjourney、Stable Diffusion、DALL·E、通义万相等工具生成 **方形工具栏图标**。

---

## 使用方式

1. **复制「全局风格后缀」**到每条提示词末尾（或作为固定 negative / style 参考），保证整套图标一致。  
2. **主提示词为英文**（多数模型对英文细节控制更稳）；**中文备选**供国产模型或偏好中文描述时使用。  
3. 生成后建议：**去背景**、**裁切圆角**、导出 **64×64 / 128×128** 做 ParaView 小图标。  
4. 标注 **（可选构建）** 的滤镜在部分安装中不存在，仍可先备图标。

---

## 全局风格后缀（每条提示词末尾追加）

**English（推荐整段贴在提示词最后）：**

`Flat vector app icon, 1024x1024 square, single centered symbol, scientific visualization and computational geometry theme, palette: deep navy, cyan-teal accent, light gray, pure white background, subtle faint isometric grid or coordinate ticks only as atmosphere, thick clean strokes, high legibility at small size, no text, no letters, no numbers, no watermark, no photorealism, no human faces.`

**中文备选后缀：**

`扁平矢量软件图标，1024 方形构图，单一居中图形，科学可视化与计算几何风格，配色深蓝、青绿点缀、浅灰线条，白底，轻微等距网格氛围，粗线条易辨认，无文字无水印，非照片写实。`

---

## Filters → VESPA

### VESPA Delaunay 2D

**English prompt:** A flat triangular mesh filling a circle or rounded rectangle region on a plane, small dots at vertices, Delaunay-style empty circumcircle feeling shown by faint dashed circles around a few triangles, constrained edges hinted by slightly bolder line segments; conveys planar triangulation from scattered points.

**中文备选：** 平面上的圆角区域内铺满互不重叠的三角形网格，顶点用小圆点，两三个三角形外画虚线空圆暗示 Delaunay，部分边加粗表示约束边，整体表达平面点集的 Delaunay 三角剖分。

---

### VESPA Boolean Operation

**English prompt:** Two overlapping closed mesh blobs in wireframe or translucent fill, one subtracting or intersecting the other shown by a cut-away crescent or lens-shaped overlap region highlighted in teal, third ghost outline suggesting union; conveys CSG boolean on closed triangle meshes.

**中文备选：** 两个半透明封闭三角网格体相互重叠，重叠区用青绿色高亮成新月或透镜状，暗示差集/交集/并集之一，第三道淡轮廓暗示并集体积，表达封闭网格布尔运算。

---

### VESPA Isotropic Remesher

**English prompt:** A curved surface patch covered by uniform-sized equilateral-ish triangles in a regular honeycomb pattern, one sharp crease edge protected and drawn as a bold polyline while surrounding triangles stay even; conveys CGAL isotropic remeshing with feature protection.

**中文备选：** 弯曲曲面上覆盖大小均匀的三角网格，像蜂巢一样规则，一道折痕棱边加粗保留、周围三角形仍均匀，表达各向同性重网格与特征边保护。

---

### VESPA Mesh Checker

**English prompt:** A simple mesh with a magnifying glass overlay, tiny warning glyphs: a small gap at one edge for non-watertight, crossed lines for self-intersection, optional wrench icon subtly in corner for repair; conveys mesh diagnostics and validation.

**中文备选：** 三角网格上加放大镜，边缘小缺口表示非水密，交叉线表示自交，一角淡色扳手暗示可修复，表达网格检查与诊断。

---

### VESPA Mesh Deformation

**English prompt:** A soft surface mesh with a few control points shown as larger nodes, arrows from original to displaced positions, smooth ripple propagating across the surface away from handles; conveys handle-based deformation with ROI smoothing.

**中文备选：** 柔软三角面上数个大控制点，箭头从原位指向新位置，表面泛起平滑波纹向外衰减，表达控制点驱动变形与区域平滑传播。

---

### VESPA Mesh Subdivision

**English prompt:** One coarse triangle on the left splitting into four smaller triangles on the right with a subtle arrow between, recursive subdivision pattern on a curved patch; conveys mesh refinement / subdivision surfaces.

**中文备选：** 左侧一个粗三角形，箭头指向右侧四个细三角形，曲面局部重复细分纹理，表达网格细分加密。

---

### VESPA Hole Filling

**English prompt:** A donut-shaped mesh with a bite missing, the hole being patched by a fan of new triangles shaded slightly differently, outer boundary smooth blend; conveys filling holes or patches on a triangle mesh.

**中文备选：** 环形曲面缺一块，缺口处扇形新三角面片颜色略不同，边界光顺过渡，表达补洞与补丁填充。

---

### VESPA Region Fairing

**English prompt:** A selected bulge or dent on a mesh outlined with a dashed lasso, the region smoothed into a fairer curvature with gentle highlight; conveys localized fairing / smoothing of a mesh region.

**中文备选：** 曲面上用虚线套索圈出一块凸起或凹陷，圈内曲率变柔和高光均匀，表达选中区域的光顺处理。

---

### VESPA Shape Smoothing

**English prompt:** A bumpy irregular silhouette morphing into a smoother blob, mean-curvature flow suggested by small normal arrows shrinking bumps, time-step vibe with faint motion streaks; conveys curvature-based shape smoothing.

**中文备选：** 凹凸不平的轮廓逐渐变成光滑外形，小法向箭头指向收缩鼓包，带轻微运动拖尾，表达平均曲率流形状平滑。

---

### VESPA Poisson Surface Reconstruction Delaunay

**English prompt:** Scattered points in a cloud above, implicit field iso-surface below as a watertight mesh, Poisson-like gradient arrows from points to surface, Delaunay hint with a few wireframe tetrahedra fading out; conveys Poisson reconstruction via Delaunay.

**中文备选：** 上方散乱点云，下方由隐式场等值面形成的水密三角网格，点与面之间渐变箭头，角落淡出若干四面体线框暗示 Delaunay，表达 Poisson 表面重建。

---

### VESPA Advancing Front Surface Reconstruction

**English prompt:** A growing band of new triangles advancing like a wavefront across scattered points, frontier edges highlighted in teal, inward progress arrows; conveys greedy advancing-front surface construction from point cloud.

**中文备选：** 三角形条带像波前一样在点云上向外扩展，前沿边青绿高亮，箭头表示推进方向，表达前沿法表面重建。

---

### VESPA PCA Estimate Normals

**English prompt:** A small cluster of points with a best-fit plane shown as a translucent parallelogram, short normal arrows emerging perpendicular to that plane, PCA axes as faint orthogonal ticks; conveys principal-component local plane and normal estimation.

**中文备选：** 点簇贴合一张半透明拟合平面，垂直于平面的短法向箭头，三根淡色正交轴暗示 PCA，表达主成分估计法向。

---

### VESPA Alpha Wrapping（可选构建，`VESPA_ALPHA_WRAPPING`）

**English prompt:** A noisy point cloud or triangle soup enclosed by a tight shrink-wrapped watertight mesh like plastic film, alpha-ball metaphor as a few transparent spheres kissing the surface, offset shell slightly outside; conveys alpha wrapping to manifold mesh.

**中文备选：** 杂乱点或面片汤外包裹一层紧绷的水密网格，像热缩膜，几个透明球贴附表面暗示 alpha 球，外层略厚表示 offset，表达 Alpha 包裹成流形网格。

---

### VESPA Mesh Smoothing（可选构建，`VESPA_MESH_SMOOTHING`）

**English prompt:** Triangle mesh with vertices sliding tangentially along local tangent planes drawn as small squares at corners, before/after jitter reduced, optional angle-balance symbol (equal small arcs); conveys tangential or angle-area mesh smoothing.

**中文备选：** 三角网格顶点沿切平面小方块滑动，锯齿边线变柔顺，可用等分小弧符号暗示角度面积均衡，表达切向或角度面积平滑。

---

## Filters → SHYX

### SHYX Density-Based Volume Sampler

**English prompt:** A closed watertight volume cutaway showing interior, scalar field as blue-to-red gradient, many sample dots denser in red regions and sparser in blue; conveys density-weighted random sampling inside a volume or enclosed mesh.

**中文备选：** 封闭体剖面内标量场蓝到红渐变，采样点在高值区密、低值区疏，表达基于密度场的体积内随机采样。

---

### SHYX Array Probability Point Cull

**English prompt:** Grid of point vertices each with a tiny dice or coin flip icon, some points vanishing with fade particles, others solid, clamp bar 0–1 at side; conveys per-point Bernoulli culling by scalar probability.

**中文备选：** 点阵上每点旁微小骰子/硬币意象，部分点淡出碎屑、部分保留，侧边 0–1 夹条暗示概率截断，表达按标量概率独立剔除点。

---

### SHYX Bidirectional Streamline Merge

**English prompt:** Two polyline traces from one seed, one arrowhead forward one reversed then merged into a single continuous thick line, small SeedIds tag glyph; conveys merging bidirectional streamlines by seed id.

**中文备选：** 同一种子出发两条折线，一条正向一条反向再首尾相接成一条连续粗线，小标签暗示 SeedIds，表达双向流线合并。

---

### SHYX Skeleton Extraction

**English prompt:** A thick tubular surface shrinking inward into a thin dark centerline graph (1D tree or graph inside), mean-curvature flow suggested by inward normals on the walls; conveys curve skeleton from closed watertight mesh.

**中文备选：** 厚管状封闭表面向内收缩成内部细长中心线图，管壁法向指向内暗示曲率流，表达封闭网格的曲线骨架提取。

---

### SHYX Vessel End Clipper

**English prompt:** A blood-vessel-like tube with centerline, flat cutting plane perpendicular to line at endpoint, clean circular cap sealing the cut, clip offset arrows along tangent; conveys clipping vessel ends at skeleton terminals.

**中文备选：** 管状血管与中心线，端点处垂直于线的切割平面，截面圆盘封口，沿切向偏移箭头，表达在骨架端点平切血管末端。

---

### SHYX Surface Tip Extractor

**English prompt:** A rounded surface with localized high-curvature tip pinched out as a small highlighted cap or protrusion marker, search-radius ring around tip; conveys extracting surface tips within radius.

**中文备选：** 光滑曲面上高曲率尖端被圈出并顶出为小帽状标记，周围搜索半径环，表达在半径内提取表面尖端结构。

---

### SHYX Surface to Volume Mesh

**English prompt:** Closed triangulated shell on outside, interior filled with tetrahedral wireframe peeking through cutaway, quality parameters vibe with balanced tet shapes; conveys CGAL surface-to-tetrahedral volume meshing.

**中文备选：** 外为封闭三角壳，剖开可见内部四面体填充，四面体形状均衡，表达由封闭表面生成体网格（CGAL）。

---

### SHYX TetGen

**English prompt:** Input closed surface mesh arrow into a solid brick of regular tetrahedra, TetGen-style quality (no vendor logo), boundary facets matching outer shell; conveys tetrahedral mesh generation with TetGen.

**中文备选：** 封闭表面箭头指向充满规则四面体的实体块，边界面与外壳对齐，表达 TetGen 四面体剖分。

---

### SHYX DataSet To Partitioned Collection

**English prompt:** A block of tetrahedra labeled as one volume chunk, separate colored surface patches split along sharp edges, side-set and node-set badges, assembly tree silhouette; conveys partitioning for IOSS/Exodus: tet block plus split boundary sides/nodes.

**中文备选：** 一块四面体体网格与按棱线分裂的多色边界面片，侧面集/节点集小徽章，旁有层级树剪影，表达为 IOSS 写出准备的分区集合。

---

### SHYX Adaptive Isotropic Remesher（需 CGAL ≥ 6.0）

**English prompt:** Same curved surface but triangle size varies: tiny triangles in high-curvature creases, larger in flat areas, min-max edge length shown as ruler ticks, curvature waves in background; conveys curvature-adaptive CGAL isotropic remeshing with edge clamps.

**中文备选：** 同一弯曲曲面上网眼疏密随曲率变化，褶皱处三角细、平坦处粗，旁有最小最大边长刻度尺，表达曲率自适应各向同性重网格。

---

### SHYX Geodesic Distance

**English prompt:** A bumpy 2-manifold surface with one source point glowing, concentric geodesic iso-contours wrapping hills and valleys (not Euclidean circles), shortest path ribbon to another point; conveys geodesic distance on mesh.

**中文备选：** 起伏曲面上一个源点发光，测地线等值线绕过山丘（非平面圆），到另一点的最短路径带状高亮，表达网格上的测地距离。

---

### SHYX Point Cloud Surface SDF

**English prompt:** Point cloud on one side, reference surface on other, perpendicular distance arrows positive outside negative inside, scalar bar +/−; conveys signed distance from points to surface (implicit distance).

**中文备选：** 点云与参考曲面对置，垂直距离箭头外正内负，标量条 ± 示意，表达点到曲面的有符号距离场。

---

### SHYX Radius Neighbor Count

**English prompt:** Each point with a circle or sphere of fixed radius, neighbor counts as small stacked tally marks or heat color inside disk, many overlapping circles in point cloud; conveys counting neighbors within radius r.

**中文备选：** 每个点周围固定半径圆或球，圆内用计数符或热力色表示邻点数，多点云重叠圆环，表达半径邻域计数。

---

### SHYX FTLE Filter

**English prompt:** Flow field as small curved arrows, a deformed ellipse or material line stretching along manifold, ridge lines highlighted in teal, clock and integration arrow; conveys finite-time Lyapunov exponent / separation in advected flow.

**中文备选：** 流场小弯箭头，物质线/椭圆被拉长，脊线青绿高亮，时钟与积分箭头，表达有限时间李雅普诺夫指数与流场分离。

---

### SHYX Vortex Criteria

**English prompt:** Swirling velocity vectors forming a vortex, layered scalar chips labeled abstractly as Q, Lambda2, Omega (no text—use symbolic swirls and tensor grid), multiple small glyphs for different criteria; conveys vortex identification diagnostics bundle.

**中文备选：** 旋转速度矢量成涡旋，多层抽象标量切片用涡量与张量格点象征（无文字），多小图标暗示 Q、Lambda2 等多种涡判据，表达涡识别与诊断量计算。

---

### SHYX Vortex Core (Test)

**English prompt:** A vortex tube showing a dark spiral core line extracted from vector field, test-tube or lab accent subtle, surrounding streamlines helical; conveys experimental vortex core line extraction.

**中文备选：** 涡管中抽出深色螺旋核心线，周围螺旋流线，轻微试管/实验感，表达涡核线检测（测试性质）。

---

### SHYX Vector Field Topology

**English prompt:** Critical points as colored dots (saddle, spiral, node) on a plane, separatrix curves connecting them, small vector field arrows in background; conveys topological skeleton of vector field.

**中文备选：** 平面上临界点用不同颜色点（鞍点、焦点等），分界线连接各点，背景小矢量箭头，表达向量场拓扑结构。

---

### SHYX Clebsch Map Filter

**English prompt:** Three intertwined scalar bands or level-set ribbons on a volume slice, velocity field implied by orthogonal gradients, sphere S² or color wheel motif for Clebsch angles; conveys Clebsch map construction from velocity.

**中文备选：** 切片上三色交织的等值带或条带，正交梯度暗示速度场，球面或色环意象暗示 Clebsch 角，表达由速度场构造 Clebsch 映射。

---

### SHYX Array Curve Mapper

**English prompt:** A wavy input-to-output graph as a piecewise linear curve on a mini chart, scalar bar morphing through the curve, point cloud picking up new colors; conveys remapping array values through editable transfer curve.

**中文备选：** 小坐标系中折线传递曲线连接输入输出轴，点云颜色沿曲线变化，表达用分段曲线映射数组标量。

---

### SHYX Disconnected Region Fuse

**English prompt:** Two separate mesh islands with a bridge of merged vertices forming between them, distance threshold shown as a small gap ruler closing, only cross-island stitches highlighted; conveys fusing vertices between disconnected components within threshold.

**中文备选：** 两座分离网格岛之间在阈值内架起合并顶点桥，小标尺表示距离阈值，仅跨岛缝合处高亮，表达不连通区域顶点融合。

---

## 表示 Representations（非 Filter，菜单在 Display）

若仅为 **Filters** 菜单做图标，可跳过本节；若整套插件统一资源，可一并生成。

### Pulse Glyphs

**English prompt:** Many small arrow or cone glyphs on a surface, a periodic pulse ring or heartbeat envelope modulating their scale, GPU instancing suggested by faint duplicate offsets, time axis as subtle repeating wave; conveys animated pulse-modulated 3D glyphs.

**中文备选：** 表面上大量箭头/锥形 glyph，心跳式包络环调制大小，轻微重影暗示 GPU 实例化，背景淡周期波暗示时间，表达脉冲动画 Glyph 表示。

---

### Animated Streamline

**English prompt:** Streamline ribbon mesh with flowing opacity gradient along the line, shader-glow bands moving along the tube, texture-coordinate vibe as staggered highlights; conveys GPU-animated streamline surface representation.

**中文备选：** 流线带状网格沿路径流动的透明度渐变，高亮条纹沿管段推进，表达 GPU 着色驱动的流线动画表示。

---

## 数据源（非 Filter，可选做同一套图标）

### CGAL Point Cloud Reader

**English prompt:** Document sheet corner with scattered dots escaping into a point cloud, file corner fold, formats abstract symbols as tiny geometric stamps (no letters); conveys reading .las .off .ply .xyz point clouds.

**中文备选：** 卷角文件飞出点云，角上用小几何印记象征多种格式（不出现字母），表达点云文件读取。

---

## 与功能说明文档的关系

算法细节、参数表与构建开关仍以 `VESPA_Plugin_功能说明.md` 为准；本文件仅服务**视觉隐喻**与**图标生成**。
