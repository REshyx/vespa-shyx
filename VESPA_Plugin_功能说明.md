# VESPA 插件功能说明

本文档说明在 ParaView 中以 VESPA 插件形式提供的各滤镜功能及其参数。

---

## 1. VESPA Advancing Front Surface Reconstruction

**功能**：从无组织点云进行表面重建的贪心算法。基于 CGAL 的 Advancing Front 方法，按“前沿”逐步选择三角形，生成**可定向的流形三角网格**。适用于散乱点云生成封闭曲面。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Per** | double | 0.0 | 周长界（Perimeter bound），用于控制边界边长。 |
| **Radius Ratio Bound** | double | 5.0 | 半径比界。限制新生成三角形的外接圆半径与最短边之比，越大允许越“扁”的三角形。 |

---

## 2. VESPA Alpha Wrapping

**功能**：从点云或三角面片汤（triangle soup）生成**严格包住输入的 2-流形网格**。输出保证水密、可定向。输入支持点和三角形；不要求输入已是流形。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Absolute Thresholds** | bool | false | 为 false 时，Alpha 和 Offset 为包围盒对角线长度的比例；为 true 时为绝对数值。 |
| **Alpha** | double | 5 | 控制结果网格中面的最大外接圆半径。越小越能进入凹陷区域，细节越多。 |
| **Offset** | double | 3 | 控制结果相对输入的“膨胀”量。须为正，以保证 2-流形输出。 |

---

## 3. VESPA Boolean Operation

**功能**：对两个**封闭、三角化的网格**做布尔运算，得到封闭网格。需要两个输入：主输入（Input）与第二输入（Source）。

| 参数/输入 | 类型 | 默认值 | 说明 |
|-----------|------|--------|------|
| **Operation Type** | enum | DIFFERENCE | 运算类型：**DIFFERENCE**（差集）、**INTERSECTION**（交集）、**UNION**（并集）。 |
| **Source 连接** | 第二输入 | — | 第二个 vtkPolyData，与主输入做布尔运算。 |

---

## 4. VESPA Delaunay 2D

**功能**：对**平面上的**点、边、多边形做 2D Delaunay 三角化。输入须落在某一平面（沿 x、y 或 z 的平面）；边约束可以相交，只要不需要在交点处插入新点。

**参数**：无额外参数。只需一个 vtkPolyData 输入（点、线或面，需共面）。

---

## 5. VESPA Hole Filling（Patch Filling）

**功能**：在三角网格上**填充孔洞或补丁**。可配合第二个输入（Selection）指定要移除的三角形或点，再对形成的洞用 CGAL 的三角化+细化+光顺方法填充。也可用于填充“隧道”（选中内部单元后填充）。与各向同性重网格不同，不强调保持原形。

| 参数/输入 | 类型 | 默认值 | 说明 |
|-----------|------|--------|------|
| **Source 连接** | 可选第二输入 | — | vtkSelection：要删除的三角形或点，形成待填充的洞/区域。不设则填充网格上检测到的洞。 |
| **Fairing Continuity** | int | 1 | 边界切向连续性：**0** = C0（平面填充）、**1** = C1、**2** = C2。 |

---

## 6. VESPA Isotropic Remesher

**功能**：使用 CGAL 的**各向同性重网格**对三角网格重划分，使边长更均匀，并**保护特征边**（根据角度阈值识别）。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Target Length** | double | -1（自动） | 目标边长。若未设置（-1），自动取包围盒对角线的 1%。 |
| **Protect Angle** | double | 45 | 特征边角度阈值（度）。二面角超过此值的边在重网格时会被保护。 |
| **Number Of Iterations** | int | 1 | CGAL 各向同性重网格的迭代次数。 |

---

## 7. VESPA Mesh Checker

**功能**：对网格做**诊断**，检查流形性、自交、水密性等；可选尝试修复非共形网格。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Check Watertight** | bool | true | 是否检查网格封闭且围成体积（水密）。 |
| **Check Intersect** | bool | true | 是否检查自交。 |
| **Attempt Repair** | bool | false | 是否尝试修复非共形网格（会触发一次深拷贝）。 |

---

## 8. VESPA Mesh Deformation

**功能**：通过将**控制点**移动到目标位置来**变形表面网格**；可定义 ROI（感兴趣区域），使变形在控制点附近平滑传播。  
三个输入：端口 0 = 待变形网格（点需有唯一 ID）；端口 1 = 控制点目标位置（按 ID 对应）；端口 2 = 可选 ROI 的 vtkSelection。未指定 ROI 时，仅移动控制点，其余网格不变。

| 参数/输入 | 类型 | 默认值 | 说明 |
|-----------|------|--------|------|
| **Source 连接** | 第二输入 | — | vtkPointSet：控制点的目标位置（通过 ID 与网格点对应）。 |
| **Selection 连接** | 第三输入 | — | vtkSelection：ROI；不设则仅移动控制点。 |
| **Mode** | enum | SMOOTH | **SMOOTH**：最小化非线性变形；**SRE_ARAP**：尽量保持局部形状（Smoothed Rotation Enhanced ARAP）。 |
| **Sre Alpha** | double | 0.02 | SRE_ARAP 模式下角度变形的刚度。 |
| **Number Of Iterations** | unsigned int | 5 | 变形迭代次数。 |
| **Tolerance** | double | 1e-4 | 能量收敛容差。 |
| **Global Id Array** | string | "" | 用于 ROI 与控制点的 ID 数组名；空则使用点的 GlobalIds。 |

---

## 9. VESPA Mesh Subdivision

**功能**：对多边形网格做**细分**，通过插入新点细化并平滑网格。支持多种细分规则。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Subdivision Type** | enum | SQRT3 | 细分方法：**CATMULL_CLARK**（PQQ）、**LOOP**（PTQ）、**DOO_SABIN**（DQQ）、**SQRT3**。 |
| **Number Of Iterations** | int | 1 | 细分迭代次数（每轮应用一次细分）。 |

---

## 10. VESPA PCA Estimate Normals

**功能**：对**无组织点云**用 PCA 估计法向（固定邻域点数或固定半径邻域），并可进行法向定向与删除无法定向的点。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Number Of Neighbors** | unsigned int | 18 | 计算法向时的邻域点数；在“固定半径”模式下也用于估计平均间距。 |
| **Orient Normals** | bool | true | 是否对法向进行定向（如最小生成树定向）。 |
| **Delete Unoriented** | bool | true | 是否删除无法定向的法向（及对应点）。 |

---

## 11. VESPA Poisson Surface Reconstruction Delaunay

**功能**：从**带定向法向的点云**做泊松表面重建。先求隐函数，再从中提取等值面得到三角网格。假设输入点云噪声和离群点较少。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Min Triangle Angle** | double | 20.0 | 最小三角形角（度）。 |
| **Max Triangle Size** | double | 2.0 | 最大三角形边长（相对点云平均间距的倍数）。 |
| **Distance** | double | 0.375 | 表面近似误差（相对点云平均间距）。 |
| **Generate Surface Normals** | bool | true | 是否（重新）生成表面法向。 |

---

## 12. VESPA Region Fairing

**功能**：对三角网格上的**指定区域**做光顺（fairing），使用 CGAL 的 `fair` 方法，常用于**盲孔**等区域的光顺。
Fairing 的目标是去除网格表面的高频噪声（例如 3D 扫描产生的微小抖动），或者在设计中为了美观而平滑曲面。它试图最小化某种“能量函数”，通常与表面的曲率（Curvature）有关。
它不仅仅是简单的局部平均（像拉普拉斯平滑那样迭代），而是基于数学上的连续性要求来重新定位顶点。
3. 平滑的阶数 (Continuity Order)
CGAL 允许你指定平滑的“阶数”（Order），这决定了平滑的力度和性质：
$k=1$ (Position/Membrane energy): 类似于最小化表面积。这相当于求解拉普拉斯方程。效果是表面会尽可能收缩变平（像肥皂膜一样）。
$k=2$ (Curvature/Thin-plate energy): 类似于最小化曲率变化。这相当于求解双调和方程（Bi-Laplacian）。效果是表面会变得光滑且连续，就像弯曲的薄金属板，这是最常用的 Fairing 模式，因为它能产生非常自然的光滑表面，而不会过度收缩体积。
更高阶: 提供更高级别的连续性，但计算量更大。

| 参数/输入 | 类型 | 说明 |
|-----------|------|------|
| **Source 连接** | 第二输入 | vtkSelection：要光顺的区域（如孔洞边界或区域内的单元）。 |

**参数**：无其他可调参数。

---

## 13. VESPA Shape Smoothing

**功能**：基于**平均曲率**对网格顶点进行移动，平滑整体形状。通过迭代次数和时间步长控制平滑强度；时间步越大，单步变形越大。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Number Of Iterations** | unsigned int | 1 | 平滑迭代次数。 |
| **Time Step** | double | 1e-4 | 时间步长（平滑速度）。典型范围约 1e-6～1；越大形状变化越快。 |

---

## 14. VESPA Skeleton Extraction

**功能**：对**封闭（水密）、无边界、三角化**的表面网格提取 1D 曲线骨架（Curve Skeleton）。基于 CGAL 的 **Mean Curvature Flow Skeletonization**：先对网格进行平均曲率流收缩（contract），再将收缩后的“meso-skeleton”转换为骨架图，最后输出为一组折线（polyline）。  
输出是 `vtkPolyData`（Points + Lines），可直接在 ParaView 里显示为线框/骨架。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| **Max Iterations** | int | 500 | `contract_until_convergence()` 的最大迭代次数（CGAL `max_iterations`）。 |
| **Area Threshold** | double | 1e-4 | 收敛阈值（CGAL `area_variation_factor`）。当一次迭代后 meso-skeleton 的面积变化小于 `AreaThreshold * 原始面积` 时认为收敛；值越小通常骨架越“细”，但更耗时。 |
| **Max Triangle Angle (deg)** | double | 110 | 局部重网格参数（CGAL `max_triangle_angle`）。在收缩过程的局部 remeshing 中，角度过大的三角形可能被拆分。单位为**度**（内部会转为弧度传给 CGAL）。 |
| **Min Edge Length** | double | 0 | 局部重网格参数（CGAL `min_edge_length`）。过短边可能被塌缩。若为 **0**，表示使用 CGAL 的默认（与网格尺寸相关的）最小边长。 |
| **Quality / Speed Tradeoff (w_H)** | double | 0.1 | 收缩速度与质量权衡（CGAL `quality_speed_tradeoff`，论文中的 \(w_H\)）。值越小收敛更快，但骨架质量可能下降。 |
| **Medially Centered** | bool | true | 是否做 medial centering（CGAL `is_medially_centered`）。开启通常骨架更接近中轴，但更耗时。 |
| **Medial Centering Tradeoff (w_M)** | double | 0.2 | medial centering 的平滑/靠近中轴权衡（CGAL `medially_centered_speed_tradeoff`，论文中的 \(w_M\)）。值越大骨架更靠近中轴但可能更慢；仅在 `MediallyCentered=true` 时生效。 |

---

## 使用建议

- 多数网格滤镜要求输入为**三角化**的 `vtkPolyData`，建议先 **Tetrahedralize** + **Extract Surface**，必要时再用 **VESPA Alpha Wrapping** 得到水密 2-流形。
- 布尔运算、重网格、细分等要求输入为**封闭、流形**网格时效果更稳定；可先用 **VESPA Mesh Checker** 检查或修复。
- 使用 **VESPA Skeleton Extraction** 时，输入必须是**水密闭合**网格；若模型有洞，建议先用 **VESPA Hole Filling** 或 **VESPA Alpha Wrapping** 处理。
- 从点云重建时，一般顺序：点云 → **VESPA PCA Estimate Normals** → **VESPA Poisson** 或 **Advancing Front**；若点云很乱可考虑先 **Alpha Wrapping** 再后续处理。

以上参数与行为基于当前 VESPA 源码与头文件整理，若与 ParaView 界面标签略有差异，以界面为准；更底层说明见 [CGAL 文档](https://doc.cgal.org/)。
