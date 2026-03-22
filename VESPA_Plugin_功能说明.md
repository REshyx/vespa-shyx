# VESPA 插件功能说明

本文档说明在 ParaView 中以 VESPA 插件形式提供的各滤镜、数据源功能及其参数。插件包含两大类：**VESPA**（通用网格处理、点云与表面重建）和 **SHYX**（血管建模、体积网格生成、流场分析等）。

---

## 数据源

### CGAL Point Cloud Reader

**功能**：读取 CGAL 点云数据文件，支持格式 `.las`、`.off`、`.ply`、`.xyz`。

---

## 一、VESPA 滤镜

### 1. VESPA Advancing Front Surface Reconstruction

**功能**：从无组织点云进行表面重建的贪心算法。基于 CGAL 的 Advancing Front 方法，按“前沿”逐步选择三角形，生成**可定向的流形三角网格**。适用于散乱点云生成封闭曲面。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Perimeter** | double | 0.0 | 周长界（Perimeter bound），用于控制边界边长。 |
| **Radius Ratio Bound** | double | 5.0 | 半径比界。限制新生成三角形的外接圆半径与最短边之比，越大允许越“扁”的三角形。 |

---

### 2. VESPA Alpha Wrapping（可选，CMake 启用 `VESPA_ALPHA_WRAPPING`）

**功能**：从点云或三角面片汤（triangle soup）生成**严格包住输入的 2-流形网格**。输出保证水密、可定向。输入支持点和三角形；不要求输入已是流形。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Use Absolute Thresholds** | bool | false | 为 false 时，Alpha 和 Offset 为包围盒对角线长度的比例；为 true 时为绝对数值。 |
| **Alpha** | double | 5.0 | 控制结果网格中面的最大外接圆半径。越小越能进入凹陷区域，细节越多。 |
| **Offset** | double | 3 | 控制结果相对输入的“膨胀”量。须为正，以保证 2-流形输出。 |

---

### 3. VESPA Boolean Operation

**功能**：对两个**封闭、三角化的网格**做布尔运算，得到封闭网格。需要两个输入：主输入（Input）与第二输入（Source）。

| 参数/输入 | 类型 | 默认值 | 说明 |
|-----------|------|--------|------|
| **Operation Type** | enum | Difference | 运算类型：**Difference**（差集）、**Intersection**（交集）、**Union**（并集）。 |
| **Source 连接** | 第二输入 | — | 第二个 vtkPolyData，与主输入做布尔运算。 |
| **Interpolate attributes** | bool | true | 是否将属性插值到结果网格（高级选项）。 |

---

### 4. VESPA Delaunay 2D

**功能**：对**平面上的**点、边、多边形做 2D Delaunay 三角化。输入须落在某一平面（沿 x、y 或 z 的平面）；边约束可以相交，只要不需要在交点处插入新点。

**参数**：无额外参数。只需一个 vtkPolyData 输入（点、线或面，需共面）。

---

### 5. VESPA Hole Filling（Patch Filling）

**功能**：在三角网格上**填充孔洞或补丁**。可配合第二个输入（Selection）指定要移除的三角形或点，再对形成的洞用 CGAL 的三角化+细化+光顺方法填充。也可用于填充“隧道”（选中内部单元后填充）。

| 参数/输入 | 类型 | 默认值 | 说明 |
|-----------|------|--------|------|
| **Selection 连接** | 可选第二输入 | — | vtkSelection：要删除的三角形或点，形成待填充的洞/区域。不设则填充网格上检测到的洞。 |
| **Fairing Continuity** | int | 1 | 边界切向连续性：**0** = C0（平面填充）、**1** = C1、**2** = C2。 |

---

### 6. VESPA Isotropic Remesher

**功能**：使用 CGAL 的**各向同性重网格**对三角网格重划分，使边长更均匀，并**保护特征边**（根据角度阈值识别）。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Target Length** | double | 1.0 | 目标边长。 |
| **Protection Angle** | double | 45 | 特征边角度阈值（度）。二面角超过此值的边在重网格时会被保护。 |
| **Number Of Iterations** | int | 1 | CGAL 各向同性重网格的迭代次数。 |

---

### 7. VESPA Mesh Checker

**功能**：对网格做**诊断**，检查流形性、自交、水密性等；可选尝试修复非共形网格。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Check Watertight** | bool | true | 是否检查网格封闭且围成体积（水密）。 |
| **Check Intersect** | bool | true | 是否检查自交。 |
| **Attempt Repairing Mesh** | bool | false | 是否尝试修复非共形网格（会触发一次深拷贝）。 |

---

### 8. VESPA Mesh Deformation

**功能**：通过将**控制点**移动到目标位置来**变形表面网格**；可定义 ROI（感兴趣区域），使变形在控制点附近平滑传播。  
三个输入：端口 0 = 待变形网格（点需有唯一 ID）；端口 1 = 控制点目标位置（按 ID 对应）；端口 2 = 可选 ROI 的 vtkSelection。未指定 ROI 时，仅移动控制点，其余网格不变。

| 参数/输入 | 类型 | 默认值 | 说明 |
|-----------|------|--------|------|
| **Source 连接** | 第二输入 | — | vtkPointSet：控制点的目标位置（通过 ID 与网格点对应）。 |
| **Selection 连接** | 第三输入 | — | vtkSelection：ROI；不设则仅移动控制点。 |
| **Deformation mode** | enum | Smooth | **Smooth**：最小化非线性变形；**Enhanced As Rigid As Possible**：尽量保持局部形状（ARAP）。 |
| **Global Point ID Array** | string | "" | 用于 ROI 与控制点的 ID 数组名；空则使用点的 GlobalIds。 |
| **Number Of Iterations** | int | 5 | 变形迭代次数（1–20）。 |
| **Tolerance** | double | 1e-4 | 能量收敛容差。若为 0 则不使用能量准则。 |

---

### 9. VESPA Mesh Smoothing（可选，CMake 启用 `VESPA_MESH_SMOOTHING`）

**功能**：对三角网格做**几何平滑**。支持两种模式：**Tangential relaxation**（在顶点局部切平面内做面积拉普拉斯平滑）和 **Angle and area smoothing**（均衡相邻边夹角与三角形面积）。边界顶点被约束不移动。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Smoothing Method** | enum | Tangential relaxation | **Tangential relaxation**：切平面拉普拉斯；**Angle and area smoothing**：角度与面积平滑。 |
| **Number Of Iterations** | int | 10 | 平滑迭代次数（1–25）。 |
| **Use Safety Constraints** | bool | false | 为 false 时允许所有移动；为 true 时启用安全约束。 |

---

### 10. VESPA Mesh Subdivision

**功能**：对多边形网格做**细分**，通过插入新点细化并平滑网格。支持多种细分规则。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Subdivision Type** | enum | Sqrt3 | 细分方法：**Catmull-Clark**（PQQ）、**Loop**（PTQ）、**Doo-Sabin**（DQQ）、**Sqrt3**。 |
| **Number Of Iterations** | int | 1 | 细分迭代次数（1–10）。 |

---

### 11. VESPA PCA Estimate Normals

**功能**：对**无组织点云**用 PCA 估计法向（固定邻域点数或固定半径邻域），并可进行法向定向与删除无法定向的点。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Number of Neighbors** | int | 18 | 计算法向时的邻域点数；在“固定半径”模式下也用于估计平均间距。 |
| **Orient Normals** | bool | true | 是否对法向进行定向（如最小生成树定向）。 |
| **Delete unoriented normals** | bool | true | 是否删除无法定向的法向（及对应点）。 |

---

### 12. VESPA Poisson Surface Reconstruction Delaunay

**功能**：从**带定向法向的点云**做泊松表面重建。先求隐函数，再从中提取等值面得到三角网格。假设输入点云噪声和离群点较少。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Min Triangle Angle** | double | 20.0 | 最小三角形角（度）。 |
| **Max Triangle Size** | double | 2.0 | 最大三角形边长（相对点云平均间距的倍数）。 |
| **Distance** | double | 0.375 | 表面近似误差（相对点云平均间距）。 |
| **Generate Surface Normals** | bool | true | 是否（重新）生成表面法向。 |

---

### 13. VESPA Region Fairing

**功能**：对三角网格上的**指定区域**做光顺（fairing），使用 CGAL 的 `fair` 方法，常用于**盲孔**等区域的光顺。需通过 Selection 指定要光顺的区域。

| 参数/输入 | 类型 | 说明 |
|-----------|------|------|
| **Selection 连接** | 第二输入 | vtkSelection：要光顺的区域（如孔洞边界或区域内的单元）。 |

**参数**：无其他可调参数。

---

### 14. VESPA Shape Smoothing

**功能**：基于**平均曲率**对网格顶点进行移动，平滑整体形状。通过迭代次数和时间步长控制平滑强度；时间步越大，单步变形越大。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Number Of Iterations** | int | 1 | 平滑迭代次数（1–10）。 |
| **Time Step** | double | 1e-4 | 时间步长（平滑速度）。典型范围约 1e-6～1；越大形状变化越快。 |

---

## 二、SHYX 滤镜

### 15. SHYX Skeleton Extraction

**功能**：对**封闭（水密）、无边界、三角化**的表面网格提取 1D 曲线骨架（Curve Skeleton）。基于 CGAL 的 **Mean Curvature Flow Skeletonization**：先对网格进行平均曲率流收缩，再将收缩后的 meso-skeleton 转换为骨架图，最后输出为一组折线（polyline）。  
输出为 `vtkPolyData`（Points + Lines），可直接在 ParaView 中显示为线框/骨架。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Max Iterations** | int | 500 | 收缩的最大迭代次数。 |
| **Area Threshold** | double | 1e-4 | 收敛阈值。面积变化小于该比例时认为收敛；值越小骨架越细，但更耗时。 |
| **Max Triangle Angle (deg)** | double | 110 | 局部重网格参数，过大的三角形可能被拆分。 |
| **Min Edge Length** | double | 0 | 局部重网格参数，过短边可能被塌缩。0 表示使用 CGAL 默认值。 |
| **Quality / Speed Tradeoff (w_H)** | double | 0.1 | 收缩速度与质量权衡。值越小收敛更快，骨架质量可能下降。 |
| **Medially Centered** | bool | true | 是否做 medial centering，开启时骨架更接近中轴，但更耗时。 |
| **Medial Centering Tradeoff (w_M)** | double | 0.2 | medial centering 的平滑/靠近中轴权衡。 |

---

### 16. SHYX Vessel End Clipper

**功能**：在血管表面网格的骨架端点处**平切血管末端**，用于 CFD 入口/出口边界。在骨架的度数为 1 的顶点处放置垂直于中心线的切割平面，得到平整截面。

| 输入/参数 | 类型 | 默认值 | 说明 |
|-----------|------|--------|------|
| **Input** | vtkPolyData | — | 待裁剪的血管表面网格。 |
| **Centerline** | vtkPolyData | — | 中心线骨架（如 SHYX Skeleton Extraction 输出）。 |
| **Clip Offset** | double | 0.0 | 沿中心线切向平移切割平面的距离。正向外，负向内。 |
| **Tangent Depth** | int | 1 | 估计切向时沿骨架向内走的边数，越大切向越平滑。 |
| **Cap Endpoints** | bool | true | 是否在裁剪后的孔洞处封口，生成水密网格。 |
| **Endpoints to Clip** | 选择 | — | 选择要裁剪的端点。需先 Apply 以发现端点。 |

**输出**：端口 0 = 裁剪后的网格；端口 1 = 裁剪平面可视化（位置与法向）。

---

### 17. SHYX Density-Based Volume Sampler

**功能**：在**封闭 mesh 围成的体积内**按密度场生成随机采样点。支持体积网格（vtkUnstructuredGrid）或封闭表面网格（vtkPolyData）。采样密度由用户指定的点数据标量数组控制，该数组的值会线性映射到 0–100% 密度。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Density Array** | string | (Uniform) | 控制采样密度的标量数组；留空则均匀采样。 |
| **Pre-Sample Count** | int | 100000 | 预采样网格点数，输出为经密度筛选后的内部点。 |
| **Random Seed** | int | 0 | 随机种子，用于可重复结果。 |

#### 17a. SHYX Array Probability Point Cull（数组概率点剔除）

**功能**：对**已有** `vtkDataSet` 的每个点，用**点标量直接当作保留概率**（只做 **clamp 到 [0,1]**，**不做**按数组 min/max 的线性放缩）。例如标量在 **0.1～0.5** 之间，则保留概率在 **10%～50%**；若区间为 **−1～3**，则小于 0 的当作 0、大于 1 的当作 1。对每个点独立伯努利抽样。标量从**点数据**读取；坐标使用 `vtkDataSet::GetPoint`。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Weight Array** | string | (Uniform) | 每点保留概率（标量值 clamp 到 [0,1]）；留空或 (Uniform) 表示保留全部点。 |
| **Random Seed** | int | 0 | 随机种子。 |

**输出**：仅含 **vertex** 的点云及拷贝的点数据（非原始网格拓扑）。

开启工程选项 **VESPA_USE_SMP** 时，筛选与写点使用 **VTK SMP** 并行；加权模式下随机数为由 (Seed, 点索引) 确定的 **SplitMix64**（与未开 SMP 时 VTK 序列不同，固定 Seed 仍可复现）。

**说明**：与 **SHYX Density-Based Volume Sampler** 的密度场逻辑不同——后者会将标量**按全场范围**映射到 0–100%；本滤镜**只用 clamp**。

---

### 18. SHYX Surface to Volume Mesh

**功能**：从**封闭三角表面网格**生成 tetrahedral 体积网格，使用 CGAL 的 Mesh_3（Delaunay 细化）。输入须水密、流形、仅三角面、无自交。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Facet Angle** | double | 25 | 表面 facet 角度下界（度），越小表面近似越精细。 |
| **Facet Size** | double | 0.15 | 表面 Delaunay 球半径上界。 |
| **Facet Distance** | double | 0.008 | 表面 circumcenter 与 Delaunay 球心距离上界。 |
| **Cell Radius-Edge Ratio** | double | 3 | 四面体外接球半径与最短边之比上界，须 > 2。 |
| **Cell Size** | double | 0 | 四面体外接球半径上界，0 表示由表面准则驱动。 |
| **Detect Features** | bool | true | 是否检测锐特征边/角。 |
| **Feature Angle** | double | 60 | 特征边检测的角度阈值（度）。 |
| **Edge Size** | double | 0 | 特征边上段长度上界，0 表示不显式限制。 |

---

### 19. SHYX TetGen

**功能**：从**封闭三角表面网格**生成 tetrahedral 体积网格，使用 TetGen 库的约束 Delaunay 四面体化。输入须水密、流形、仅三角面。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Max Volume** | double | 0 | 四面体体积上界，TetGen 会细化超出的单元。0 表示不限制。 |
| **Max Radius-Edge Ratio** | double | 1.8 | 质量网格时的半径-边比上界（TetGen -q）。>0 时须 ≥ 1.2；0 表示不限。 |
| **Min Dihedral Angle** | double | 0 | 质量网格时的最小二面角（度）。>0 启用质量网格；0 表示不限。 |
| **Preserve Surface** | bool | true | 开启时 TetGen 不在边界上插入 Steiner 点（-Y），保持输入表面不变。与 Use CDT 不兼容。 |
| **Use CDT** | bool | false | 使用约束 Delaunay 细化（-D）。与 Preserve Surface 不兼容。 |
| **CDT Refine Level** | int | 7 | CDT 细化等级 1–7（-D#），仅在 Use CDT 开启时有效。 |
| **Check Mesh** | bool | false | 是否检查最终网格一致性（-C）。 |
| **Epsilon** | double | 1e-8 | 共面检测容差（-T）。 |

---

### 20. SHYX Surface Tip Extractor

**功能**：识别多边形表面上的**尖端/尖锐点**。对每个顶点，计算其与搜索半径内测地连通邻域质心之间的距离，结果存入点数据数组 **TipScore**。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Search Radius** | double | 5.0 | 定义局部邻域的半径。越大捕获更大尺度特征；越小对细节更敏感。 |

**输出**：输入的副本，增加 TipScore 点数据数组。

---

### 21. SHYX Geodesic Distance

**功能**：计算表面上**所有顶点到指定源顶点的最短测地距离**，使用 Dijkstra 算法。结果存储在点数据数组 **GeodesicDistance**。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Source Vertex ID** | int | 0 | 源顶点 ID，测地距离从此顶点向所有其他顶点计算。 |

---

### 22. SHYX FTLE Filter

**功能**：从速度场计算**有限时间李雅普诺夫指数（FTLE）**场，用于识别流场中的拉格朗日相干结构（LCS）。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Integration Time** | double | 1.0 | 积分时间。 |
| **Step Size** | double | 0.1 | 积分步长。 |
| **Start Time** | double | 0.0 | 积分起始时间。 |
| **Velocity Array** | string | — | 速度矢量数组名。 |
| **Integrator Type** | enum | Runge-Kutta 4 | **Runge-Kutta 2** 或 **Runge-Kutta 4**。 |
| **Advection Mode** | enum | Streamline | **Streamline** 或 **Pathline**。 |

---

### 23. SHYX Vortex Criteria

**功能**：从速度场计算**涡量、速度梯度、应变/旋转张量**及多种涡识别准则：Q-criterion、Lambda2、Swirling Strength、Liutex（Rortex）、Omega 方法、Helicity 等。所有输出均可开关。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Velocity Array** | string | — | 速度矢量数组名。 |
| **Output Velocity** | bool | false | 是否输出速度。 |
| **Output Vorticity** | bool | true | 是否输出涡量。 |
| **Output Velocity Gradient** | bool | false | 是否输出速度梯度。 |
| **Output Strain Rate Tensor** | bool | false | 是否输出应变率张量。 |
| **Output Rotation Tensor** | bool | false | 是否输出旋转张量。 |
| **Output Q-Criterion** | bool | true | 是否输出 Q 准则。 |
| **Output Lambda2** | bool | false | 是否输出 Lambda2。 |
| **Output Swirling Strength** | bool | false | 是否输出 Swirling Strength。 |
| **Output Liutex (Rortex)** | bool | false | 是否输出 Liutex。 |
| **Output Omega Method** | bool | false | 是否输出 Omega 方法。 |
| **Output Helicity** | bool | false | 是否输出 Helicity。 |
| **Epsilon** | double | 1e-10 | Omega 公式中的小量。 |

---

### 24. SHYX Array Curve Mapper

**功能**：将点数据数组（标量或矢量）通过**可编辑的分段线性曲线**映射到新的标量范围。矢量数组使用其模长。可选取输入数组、指定输入/输出范围、编辑传递曲线，结果为新的标量点数据数组。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Input Array** | string | — | 要映射的点数据数组。 |
| **Output Array Name** | string | MappedArray | 输出标量数组名称。 |
| **Input Range Min/Max** | double | 0.0 / 1.0 | 输入值范围。 |
| **Output Range Min/Max** | double | 0.0 / 1.0 | 输出映射后的值范围。 |
| **Transfer Curve** | PiecewiseFunction | — | 通过拖拽控制点编辑映射曲线，X 为归一化输入 [0,1]，Y 为归一化输出 [0,1]。 |

---

### 25. SHYX Disconnected Region Fuse

**功能**：将表面网格中的**多个不连通区域**强行合并。针对每个连通区域内的顶点，在其它连通区域上寻找最近的点；若距离小于给定阈值，则将二者融合。**仅在不同连通区域之间进行顶点合并**，同一连通区域内的点永不合并。一个顶点可能与多个其它区域的多个顶点合并。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Input** | vtkPolyData | — | 具有多个不连通区域的表面网格。 |
| **Fuse Threshold** | double | 0.01 | 顶点融合距离阈值。来自不同连通区域且距离小于此值的顶点将被合并；超过阈值的顶点对不做处理。 |

---

## 使用建议

- 多数网格滤镜要求输入为**三角化**的 `vtkPolyData`，建议先 **Tetrahedralize** + **Extract Surface**，必要时再用 **VESPA Alpha Wrapping** 得到水密 2-流形。
- 布尔运算、重网格、细分等要求输入为**封闭、流形**网格时效果更稳定；可先用 **VESPA Mesh Checker** 检查或修复。
- **SHYX Skeleton Extraction**、**SHYX Surface to Volume Mesh**、**SHYX TetGen** 的输入必须是**水密闭合**网格；若模型有洞，建议先用 **VESPA Hole Filling** 或 **VESPA Alpha Wrapping** 处理。
- 血管建模流程示例：封闭表面 → **SHYX Skeleton Extraction** → **SHYX Vessel End Clipper**（裁剪末端）→ **SHYX TetGen** 或 **SHYX Surface to Volume Mesh**（生成体积网格）。
- 从点云重建时，一般顺序：点云 → **VESPA PCA Estimate Normals** → **VESPA Poisson** 或 **Advancing Front**；若点云较乱可考虑先 **Alpha Wrapping** 再后续处理。
- **SHYX TetGen** 与 **SHYX Surface to Volume Mesh** 均可从表面生成体积网格：TetGen 基于 TetGen 库，参数更直观；后者基于 CGAL Mesh_3，可精细控制表面与体积质量。
- **SHYX Disconnected Region Fuse** 用于将多个不连通表面（如断裂的网格）通过近距离顶点融合合并为一个整体；需根据模型尺度调整 Fuse Threshold。

以上参数与行为基于当前 VESPA 源码与 ParaView XML 配置整理，若与界面标签略有差异以界面为准；更底层说明见 [CGAL 文档](https://doc.cgal.org/)。
