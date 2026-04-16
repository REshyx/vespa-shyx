# vtkSHYXDataSetToPartitionedCollection

欢迎来到 **vtkSHYXDataSetToPartitionedCollection** 的专属指南！这个类的名字虽然长得让人一口气读不完，但它的功能却极其硬核：它是连接 VTK 数据世界和 IOSS/Exodus 存储格式的强力“翻译官”。

## 1. 目的与功能算法详细解释

### 🎯 核心目的
本过滤器的主要任务是将 `vtkDataSet`（通常是包含四面体的非结构化网格）转换并重组为符合 `vtkIOSSWriter` (Exodus 格式) 严格要求的 `vtkPartitionedDataSetCollection`。它负责处理体网格、提取表面、根据特征角拆分曲面，并完美构建出 IOSS 所需的**单元块 (Element Blocks)**、**侧面集 (Side Sets)** 和 **节点集 (Node Sets)**。

### 🧠 算法流水线 (The Pipeline)
这个过程就像是把一整块乐高积木精细拆解并分门别类地装箱，具体步骤如下：

1. **提取四面体 (Tet Volume Extraction)**：首先从输入网格中提取出所有的四面体（`VTK_TETRA`）单元，放入一个独立的 `vtkUnstructuredGrid` 中，并强制分配连续的全局点 ID 和单元 ID（1..N），因为 IOSS/Exodus 格式是个“强迫症”，绝对不允许 ID 断层。
2. **表面剥离 (Surface Extraction)**：利用 `vtkDataSetSurfaceFilter` 从刚才纯四面体的网格中剥离出边界曲面，并保留原始的 Cell IDs 和 Point IDs。
3. **寻找“归宿” (Element Side Mapping)**：为表面的每个三角形寻找它属于哪个四面体的哪一个面（Exodus 格式的面编号为 1..4），并将此信息记录在单元数据 `element_side` 中，这对于后续的 Side Set 定义至关重要。
4. **特征角分裂与连通域提取 (Surface Splitting & Connectivity)**：利用 `vtkPolyDataNormals` 根据设定的**特征角 (Feature Angle)** 劈开表面，接着通过 `vtkPolyDataConnectivityFilter` 将表面划分为多个独立的连通区域（Patches）。
5. **点集清洗 (Stripping Unreferenced Points)**：对每个分割出来的补丁进行“大扫除”，剔除未被该区域引用的孤立点，确保每个 `side{i}` 和 `node{i}` 块只包含自己真正用到的点。
6. **节点与侧面生成 (Node & Side Sets Generation)**：把清理好的面作为 `side{i}`，并在这些面上以 `vtkVertex` 的形式生成对应的 `node{i}`。此时还要巧妙地将表面点的全局 ID 恢复成对应的体网格点 ID，以防最终写入时坐标错乱。
7. **数据装配 (Assembly Building)**：最后，搭建一个高大上的 `vtkDataAssembly` 树，把这些数据分门别类挂载到 `element_blocks`、`node_sets` 和 `side_sets` 下，大功告成！

## 2. 参数列表及其效果和含义

这台精密仪器提供了几个关键的旋钮供您微调：

| 参数名称 | 类型 | 默认值 | 效果和含义 |
| :--- | :---: | :---: | :--- |
| **FeatureAngle** | `double` | `70.0` | **特征角**。用于在法线计算时判断是否劈开表面的角度阈值。如果两个相邻多边形的法线夹角大于此值，表面就会在这里“断开”，从而被拆分为不同的 Side Set。数值越小，切分出的碎片越多。 |
| **SortByArea** | `int` (布尔) | `1` (True) | **按面积排序**。若开启（非零），系统会计算每个切分出来的表面补丁（Patch）的总表面积，并将生成的 `side{i}` 和 `node{i}` 按照面积从大到小进行排序。强迫症患者的福音，确保面积最大的表面永远是 `side0`。 |
