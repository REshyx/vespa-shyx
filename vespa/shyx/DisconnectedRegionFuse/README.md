# vtkSHYXDisconnectedRegionFuse

## 示意图

![DisconnectedRegionFuse](../../../illustrate/DisconnectedRegionFuse.png)

## 1. 目的与功能算法详细解释

**目的与功能**：
将表面、折线或顶点中**互不连通的片**在近处缝合。`vtkSHYXDisconnectedRegionFuse` 接受端口 0 上一路或多路 `vtkPolyData`。**Fuse Within Input**（默认开）时，每路内部按 verts / lines / polys / strips 的**连通域**分域，同一份数据里的断线/断片可以焊上。关掉后，**一路连接 = 一个域**，该路内部的不连通片不合并，只在不同输入之间缝。同一域内的点不会被阈值拉扁。**Fuse Verts / Fuse Lines / Fuse Polys** 决定输出保留哪些单元。

**ParaView 注意**：若要把多个管线节点一起缝，`Input` 必须用 **`AddInputConnection`**。若误用 **`SetInputConnection`**，新接上的线会**替换**掉旧连接。

**算法详细步骤**：
1. **域与全局顶点编号**：各路输入的顶点按连接顺序编成全局 ID。
2. **分域**：`FuseWithinInput` 开：每路内部沿单元边做并查集得到连通域，孤立点各自成域。关：域索引 = 输入连接索引。
3. **跨区匹配 (Cross-Region Matching)**：对全部顶点建 `vtkStaticPointLocator`，在 `FuseThreshold` 半径内找不同域的点对（支持 VTK SMP）。
4. **并查集聚类 (Union-Find Clustering)**：把匹配对并成等价类，新顶点坐标取类内重心。
5. **拓扑重构 (Topology Update)**：按勾选的 **Fuse Verts / Fuse Lines / Fuse Polys** 映射到新点。退化单元丢掉（vert 无点、line 不足 2 点、poly 不足 3 点）。**Triangle strips 只参与连通域，不写入输出**。
6. **属性继承**：新点继承等价类中第一个点的 `PointData`；`CellData` 按 verts→lines→polys 用输入上的真实 cell id 拷贝。

勾选 **Fuse Lines** 时，不同域的折线在端点够近处会**共用顶点**，网格上连通；不会把多条 cell 拼成一条更长的 polyline。只有一路、只有一个域、三个类型开关都开时，才浅拷贝透传。

## 2. 参数列表及其效果和含义

* **`FuseThreshold`** (类型: `double`, 默认值: `0.01`)
  * **含义**：融合距离阈值。
  * **效果**：不同域上距离 $\le$ `FuseThreshold` 的顶点合并。同域顶点不直接合并。
  * **注意事项**：过小缝不上；过大可能把无关片拉到一起。按模型尺度调。
* **`FuseWithinInput`** (类型: `bool`, 默认值: 开)
  * **含义**：单路输入里的不连通片是否互相融合。
  * **效果**：开 = 按连通域分域（一份断线也能焊）；关 = 一路一个域，只缝不同输入。
* **`FuseVerts`** / **`FuseLines`** / **`FusePolys`** (类型: `bool`, 默认值: 开)
  * **含义**：是否把对应单元映射到融合后的点并写入输出。
  * **效果**：关闭则丢弃该类型。连通域（若启用单路融合）仍按全部单元计算。
