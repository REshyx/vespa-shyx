# SHYX 模块总览

SHYX 模块是 VESPA 插件中的重要组成部分，主要专注于**血管与体积网格处理**、**流场与涡结构分析**、**数据采样与点云分析**以及**高级可视化辅助**等扩展功能。

本目录包含了各个子模块的具体实现代码与独立说明文档。以下是各子模块的功能概述与内部文档链接。

## 模块列表

### 🕸️ 网格与几何处理

* [**Skeleton Extraction (骨架提取)**](./SkeletonExtraction/README.md) - 对封闭表面网格提取 1D 曲线骨架（基于 CGAL 平均曲率流）。
* [**Vessel End Clipper (血管末端裁剪)**](./VesselEndClipper/README.md) - 在血管骨架端点处平切血管末端，适用于生成 CFD 边界。
* [**Surface To Volume Mesh (表面转体积网格 - CGAL)**](./SurfaceToVolumeMesh/README.md) - 使用 CGAL 的 Delaunay 细化从封闭表面生成四面体体积网格。
* [**TetGen (表面转体积网格 - TetGen)**](./TetGen/README.md) - 使用 TetGen 库的约束 Delaunay 四面体化生成高质量体积网格。
* [**Disconnected Region Fuse (不连通区域融合)**](./DisconnectedRegionFuse/README.md) - 多路表面输入（每路为一区域）之间按距离阈值融合近邻顶点；单路输入内不连通片互不合并。
* [**Adaptive Isotropic Remesher (自适应各向同性重网格)**](./AdaptiveIsotropicRemesher/README.md) - *(需 CGAL ≥ 6.0)* 基于离散曲率的自适应尺寸场重网格滤镜。

### 💧 流场与涡结构分析

* [**Vortex Criteria (涡识别准则)**](./VortexCriteria/README.md) - 计算流场的涡量、Q-Criterion、Lambda2、Liutex 等多种主流涡识别准则。
* [**Vortex Core Test (涡核线提取)**](./VortexCoreTest/README.md) - 使用平行矢量法（速度与加速度/急动度）提取流场中的涡核中心线。
* [**FTLE Filter (有限时间李雅普诺夫指数)**](./FTLEFilter/README.md) - 计算流场的 FTLE，用于识别和提取拉格朗日相干结构 (LCS)。
* [**Clebsch Map Filter (Clebsch 映射)**](./ClebschMapFilter/README.md) - 将速度场映射为波函数以用于涡管等流场结构的高级可视化。

### 📊 数据采样与点云分析

* [**Density-Based Sampler (基于密度的体积采样)**](./DensityBasedSampler/README.md) - 在封闭网格体积内按点数据标量定义的密度场生成随机采样点。
* [**Array Probability Point Cull (数组概率点剔除)**](./ArrayProbabilityPointCull/README.md) - 根据点数组标量（作为概率）对已有数据点进行并行伯努利抽样剔除。
* [**Radius Neighbor Count (半径内邻点统计)**](./RadiusNeighborCount/README.md) - 统计给定搜索半径内的欧氏距离邻居点个数。
* [**Point Cloud Surface SDF (点云表面有符号距离场)**](./PointCloudSurfaceSDF/README.md) - 计算点云中每个点到参考曲面的有符号距离场 (SDF)。

### 📏 表面属性计算

* [**Geodesic Distance (测地距离计算)**](./GeodesicDistance/README.md) - 计算表面上所有顶点到指定源顶点的最短测地距离 (基于 Dijkstra 算法)。
* [**Surface Tip Extractor (表面尖端提取)**](./SurfaceTipExtractor/README.md) - 识别并评估多边形表面上的尖端或尖锐特征点。

### 🎨 可视化与数据映射辅助

* [**Array Curve Mapper (数组曲线映射)**](./ArrayCurveMapper/README.md) - 通过界面上可编辑的分段线性曲线，将标量或矢量映射到新的标量范围。
* [**Bidirectional Streamline Merge (双向流线合并)**](./BidirectionalStreamlineMerge/README.md) - 合并同一种子点生成的双向流线，并支持沿线积分与差分计算。
* [**DataSet To Partitioned Collection (数据集转分区集合)**](./DataSetToPartitionedCollection/README.md) - 为 IOSS/Exodus 输出准备，提取体网格和表面分区并生成对应的装配元数据。
* [**Pulse Glyph Representation (脉冲 Glyph 表示)**](./PulseGlyphRepresentation/README.md) - GPU 实例化路径上带有脉冲缩放与旋转效果的 Glyph 动画表示。
* [**Animated Streamline Representation (流线动画表示)**](./AnimatedStreamlineRepresentation/README.md) - 基于 GPU 自定义 Shader 渲染的流线逐帧动画表示。

---

> 💡 **提示**：更多关于整体插件的配置选项、第三方依赖说明以及 VESPA 核心模块的介绍，请参考根目录的 [**VESPA_Plugin_功能说明.md**](../../VESPA_Plugin_功能说明.md)。