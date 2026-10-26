# Interactive Stream Tracer

## 目的

一个滤镜同时封装 **可交互种子 + `vtkStreamTracer`**。不再需要先在管线里建 Line/Sphere/Plane Source 再接到 Stream Tracer：在属性面板切换 **Seed Shape**，用 3D widget 拖种子，释放时按需吸附到输入表面顶点，然后直接得到流线。

- 输入：**一个**流场数据集（`vtkDataSet`，带 3 分量矢量）。- 端口 0：流线。
- 端口 1：实际使用的种子几何（线 / 球面 / 平面）。

## 三种种子形状

| Seed Shape | 种子几何 | 交互 | 吸附 |
| :--- | :--- | :--- | :--- |
| **Line**（0） | `vtkLineSource`，`LineResolution + 1` 个点 | 左键拖两个端点；中间手柄整体平移 | **每个端点独立**：`LineSnapPoint1` / `LineSnapPoint2` |
| **Sphere**（1） | `vtkSphereSource` 表面点 / `vtkPointSource` 球内点 | **左键拖球体**平移；**半径在面板 Radius 里改**（3D 上不显示 handle，也不再用拖 handle 改半径）；右键拖拽仍可快速缩放 | 球心：`SphereSnapCenter` |
| **Rectangle**（2） | `vtkPlaneSource`，矩形平面 | `vtkPlaneWidget`：角点缩放、法向箭头旋转、平面平移 | 原点：`RectangleSnapOrigin`；Point 1 / Point 2 随原点平移，保持尺寸与朝向 |

> **Apply on release 默认关闭**：拖完/输入完只更新种子，流线要点 **Apply** 才刷新（勾选后松手自动 Apply）。
> **Place in view** 把当前种子放到**相机正在看的地方**，保证落在当前视图内（不再是包围盒中心，缩放/平移后可能跑到视野外）：
> - **Line**：取视口宽度 **1/3** 和 **2/3** 处两个屏幕位置，各自打射线命中 Input 表面并吸到最近顶点；
> - **Sphere / Rectangle**：取**视口中心**打射线，命中点作为球心 / 矩形原点；
> - 尺寸按视口比例（球半径 15% 短边、矩形半边长 20% 短边）换算成命中深度处的世界长度；
> - 矩形两条边沿相机 right / up，所以初始就是正对屏幕的平面；
> - 某条射线没打中表面时回退到数据集包围盒中心（状态栏会提示），不会没反应。

## 吸附规则（vessel / stent）

两种 **Snap to** 模式（面板顶部下拉，默认 **Surface under cursor (screen ray)**）：

| 模式 | 行为 | 适用 |
| :--- | :--- | :--- |
| **Surface under cursor (screen ray)**（默认） | 从手柄当前屏幕位置向场景打一条射线，取 **Input 表面沿视线最前面的交点**，再吸附到距该交点最近的顶点 | 与视觉一致；血管袋状、支架双层、内外壁贴得很近时不会吸到背面 |
| **Nearest vertex (3D)** | 忽略相机，取世界空间最近的顶点 | 纯几何定位、已知目标点在背面、或不需要视线语义时 |

- **单输入网格**：所有吸附都针对 **Input 自己的表面顶点**；射线也只打 Input 的表面（不依赖 ParaView 的全局拾取，因此不会被别的对象挡住）。
- Input 是 `vtkPolyData` 时直接用它（含 cells）；是体网格时先 `vtkDataSetSurfaceFilter` 抽外表面再建点/单元定位器。
- 因此血管壁与支架如果已经在同一个数据集里，**两类顶点都是候选**：射线命中的那一层优先。
- 射线没打中（手柄被拖到轮廓外）时自动回退到 3D 最近顶点，不会静默失败。
- 吸附一律写成 **顶点坐标**（不是任意表面点），所以滤镜 `RequestData` 里的 3D 最近顶点吸附是 **幂等** 的：Python / 批处理复跑不会把手动吸附的结果再挪走。
- 纯 Python / 批处理只有 3D 最近顶点语义（滤镜侧没有光标信息）；射线模式是客户端 widget 的行为。

## Sphere 的 Theta / Phi 是什么

`vtkSphereSource` 把球面切成经纬网格，两个参数就是**网格的分段数**，不是点的个数：

| 参数 | 含义 | 类比 |
| :--- | :--- | :--- |
| **Theta（longitude，经度）** | 绕球轴一圈切多少份 | 竖着的「橘瓣」数量 |
| **Phi（latitude，纬度）** | 从北极到南极切多少份 | 横着的「圈」数量 |

顶点数（也就是 **Surface 模式的种子数**）：

\[
N = \Theta \times (\Phi - 2) + 2
\]

`- 2` 是因为南北极各自只有一个点；`+ 2` 把这两个极点加回来。默认 `32 x 24` → `32 x 22 + 2 = 706` 个种子点。面板底部会直接显示这个算式的结果。

Theta 调大 → 竖瓣更密；Phi 调大 → 横圈更密。两者都只影响**球面**上的点，球内部永远是空的。

## 球内采样：Sphere sampling = Interior

原来只有球面种子（流线从球壳上出发）。现在面板有 **Sampling** 下拉：

| 模式 | 种子来源 | 数量由谁决定 |
| :--- | :--- | :--- |
| **Surface (theta x phi grid)** | `vtkSphereSource` 的顶点 | Theta / Phi 网格 |
| **Interior (point count)**（默认） | `vtkPointSource` 在球内均匀撒点（`rho = R·u^⅓`） | **Interior points** 数字框，精确等于该值 |

Interior 模式下：

- **Interior points**：球内种子点数，默认 **20**，是**精确值**（不是网格近似）；
- **Random seed**（Advanced）：同样的点数 + 同样的种子 → 每次 Apply 位置完全一致；改种子就换一套分布；
- 球的 center / radius 仍然由 3D widget 控制，拖球、拖 handle 都照常；
- 面板底部显示 `Interior sampling: N seed points uniformly inside the ball.`。

Interior 模式与 Surface 模式互斥，切换时另一组控件会灰显/隐藏。

## 参数

| 参数 | 默认 | 说明 |
| :--- | :--- | :--- |
| **Seed Shape** | Line | 0 Line / 1 Sphere / 2 Rectangle |
| **Vectors** | — | `SetInputArrayToProcess`，3 分量 |
| **LinePoint1 / LinePoint2** | `-1 0 0` / `1 0 0` | 世界坐标 |
| **LineResolution** | 20 | 线段细分数；种子数为 Resolution + 1 |
| **LineSnapPoint1 / LineSnapPoint2** | on | 释放时把该端点吸到最近表面顶点 |
| **SphereCenter** | `0 0 0` | 球心 |
| **SphereRadius** | 1.0 | 半径；≤0 时用包围盒最长边的 15% |
| **SphereSeedMode** | Interior | 0 Surface（球面网格）/ 1 Interior（球内均匀撒点） |
| **SphereInteriorPointCount** | 20 | Interior 模式的种子数，精确值 |
| **SphereRandomSeed** | 1 | Interior 采样随机种子（高级）；同种子同结果 |
| **SphereThetaResolution / SpherePhiResolution** | 32 / 24 | Surface 模式网格：`Theta x (Phi-2) + 2` 个种子 |
| **SphereSnapCenter** | on | 释放时球心吸附 |
| **RectangleOrigin / Point1 / Point2** | `0 0 0` / `1 0 0` / `0 1 0` | 与 `vtkPlaneSource` 同义：原点 + 两条边 |
| **RectangleResolutionX / Y** | 10 / 10 | 平面种子网格 |
| **RectangleSnapOrigin** | on | 释放时原点吸附；Point 1/2 同步平移 |
| **IntegrationDirection** | BOTH | FORWARD / BACKWARD / BOTH |
| **IntegratorType** | Runge-Kutta 4-5 | RK2 / RK4 / RK45 |
| **IntegrationStepUnit** | Cell Length | Length / Cell Length |
| **InitialIntegrationStep** | 0.2 | 高级 |
| **MaximumPropagation** | 1.0 | 高级；按 Input 包围盒 `scaled_extent` 建议 |
| **MaximumNumberOfSteps** | 2000 | 高级 |
| **TerminalSpeed** | 1e-12 | 高级 |
| **InterpolatorType** | Point Locator | 高级 |
| **MaximumError** | 1e-6 | 高级；RK45 |
| **ComputeVorticity** | on | 高级 |

## Python 示例

```python
from paraview.simple import *

tracer = SHYXInteractiveStreamTracer(Input=volume)
tracer.SelectInputVectors = ['POINTS', 'U']
tracer.SeedShape = 0                    # 0 Line / 1 Sphere / 2 Rectangle
tracer.LinePoint1 = [0.0, 0.0, 0.0]
tracer.LinePoint2 = [10.0, 0.0, 0.0]
tracer.LineSnapPoint1 = 1               # 0 = keep the typed position
tracer.LineSnapPoint2 = 1
tracer.LineResolution = 20
tracer.UpdatePipeline()

Show(tracer)                            # port 0 streamlines
view = GetActiveViewOrCreate('RenderView')
Show(tracer, view, 'Seeds')             # port 1 seed geometry
```

球/矩形同理：`SeedShape = 1` 用 `SphereCenter` / `SphereRadius` / `SphereSnapCenter`；
`SeedShape = 2` 用 `RectangleOrigin` / `RectanglePoint1` / `RectanglePoint2` / `RectangleSnapOrigin`。

## 与分 patch / OpenFOAM 的关系
参考现有分 patch 链路（用户原话里的「矩形（openfoam 分 patch）」是参考）：

- [`SHYXSelectionAppendPatches`](../SelectionAppendPatches/README.md)：`Add from shape` 已有 Box / Sphere；也可以把本滤镜的 **端口 1（矩形种子）** 通过 **Add from pipeline** 收进 PDC 作为一块 patch 几何。
- [`SHYXPartitionedCollectionToOpenFOAM`](../PartitionedCollectionToOpenFOAM/README.md)：真正写 `constant/polyMesh` 的出口，需要 **体块**；`Selection Append Patches` 只有表面、没有体，不能直接写 polyMesh。
- 若要做「在表面上框一块 → 选 cell → 分 patch」，应走 `Selection Append Patches` 的 **Add from selection**（3D 选区）或后续可复用的矩形选区工具；本滤镜的 Rectangle 是 **流线种子平面**，不改变表面 cell 选择。

## 实现 / 构建

- 模块：`vespa/shyx/InteractiveStreamTracer/`，`vtk.module NAME=vtkSHYXInteractiveStreamTracer`，`GROUPS=Flow`。
- 纯 VTK：`vtkStreamTracer` + `vtkLineSource` / `vtkSphereSource` / `vtkPointSource` / `vtkPlaneSource` + `vtkDataSetSurfaceFilter` + `vtkStaticPointLocator` / `vtkStaticCellLocator`。
- 客户端 widget：`ParaViewPlugin/widgets/pqSHYXStreamlineSeedWidget.*`，注册为 `panel_widget="shyx_streamline_seed"`（`PropertyWidgets.cmake`）。3D widget 直接建在 RenderView 的 interactor 上，不新增 SM widget representation XML。
- 球体交互：直接用 `vtkSphereWidget2` + `vtkSphereRepresentation`，**不显示 surface handle**（`HandleVisibilityOff()`）；左键拖球体平移，半径通过面板 **Radius** 修改（右键拖拽仍是 VTK 原生缩放）。
- **半径保护**：`vtkSphereRepresentation::SetCenter()` 在 handle 可见时会把半径改成「handle 到新球心的距离」。这里 handle 已关闭，但移动球心统一走 `SetSphereCenterKeepingRadius()`，不依赖该细节。
- **枚举下拉必须按 entry 文本链接**：`pqSMAdaptor::setEnumerationProperty` 只匹配 `EnumerationDomain` 的 entry 文本。用 `addPropertyLink(combo, "currentIndex", ...)` 写进去的是 `"0"/"1"/"2"`，匹配不到任何 entry，属性静默保持不变（表现就是「切了形状但 Apply 后仍是 Line 的种子」）。正确做法是把 item 文本直接用 domain 的 `GetEntryText()` 填，然后链接 `currentText` + `currentTextChanged`（`PopulateEnumCombo()` / `ComboEnumValue()`）。
- 自定义 `panel_widget` 组内，成员属性上的 `PropertyWidgetDecorator` **不生效**（pqProxyWidget 整个组交给自定义 widget），Sphere 的 sampling/theta/phi 显示与灰显由 widget 自己处理。
