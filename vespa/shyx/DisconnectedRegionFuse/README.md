# vtkSHYXDisconnectedRegionFuse

## 示意图

![DisconnectedRegionFuse](../../../illustrate/DisconnectedRegionFuse.png)

## 1. 目的与功能算法详细解释

**目的与功能**：
在三维网格处理中，常将多块表面（例如分段建模或并行输出的片）在边界处对齐并缝合。`vtkSHYXDisconnectedRegionFuse` 在 **VTK 输入端口 0 上挂多路连接（multiple connections）**：**第 *k* 路连接上的整块 `vtkPolyData` 视为第 *k* 个 fuse 域**，在域之间做近邻融合；**同一连接（同一路输入）内部的多个不连通片不会彼此合并**。这不是「很多个输入端口、每个端口只接一路」那种多端口模型，而是和 `vtkAppendPolyData` 一样：**一个端口、多条连接**。

**ParaView 注意**：插件里 `Input` 属性必须使用 **`AddInputConnection`**（ParaView 内置 Append 类过滤器同理）。若误用 **`SetInputConnection`**，新接上的线会**替换**掉旧连接，看起来就像「第二个输入没了」。

**算法详细步骤**：
1. **域与全局顶点编号**：端口 0 上每条连接对应一个 fuse 域；将各路 `vtkPolyData` 的顶点按连接顺序编号为连续的全局顶点 ID（不在内存里先做 `vtkAppendPolyData`）。
2. **分组归类 (Grouping)**：按连接索引（域索引）把顶点归入各域，再只做**跨域**近邻搜索。
3. **跨区匹配 (Cross-Region Matching)**：利用 `vtkStaticPointLocator` 结构实施空间近邻搜索。当来自不同区域的顶点距离低于设定的容差阈值时，算法将其标记为合并候选对（该搜索过程支持 VTK SMP 并行计算）。
4. **并查集聚类 (Union-Find Clustering)**：针对复杂的间接连接关系，使用并查集 (Union-Find) 算法将匹配的合并对划分至同一等价类。聚类操作完成后，合并后的新顶点坐标取该等价类集合内顶点的重心（平均位置）。
5. **拓扑重构 (Topology Update)**：遍历网格中现有的多边形面片 (Polygons)，将面片使用的旧顶点 ID 映射至合并后的统一新 ID。在此阶段，由于多顶点融合而发生退化（顶点数小于 3）的面片结构将被清除。
6. **属性继承 (Attribute Mapping)**：完成几何更新后，维护更新 `PointData` 及 `CellData` 数据。合并产生的新顶点将继承所在等价类首个元素的属性，保留面片层级的原始单元特征。

## 2. 参数列表及其效果和含义

该模块的控制逻辑专注于以下核心参数：

* **`FuseThreshold`** (类型: `double`, 默认值: `0.01`)
  * **含义**：融合距离阈值。
  * **效果**：定义判定跨**不同连接（不同域）**顶点合并的临界欧氏距离。仅当两点来自不同输入连接且距离 $\le$ `FuseThreshold` 时才会合并。
  * **注意事项**：阈值设置过小可能导致本应闭合的缝隙无法成功连接；若设置过大，可能误将无关的特征点错误聚类，造成严重网格穿模或扭曲。建议依据三维模型的实际物理尺度合理调整。