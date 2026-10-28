# vtkSHYXCameraAdjust

把**相机配置**存进 pipeline 节点，而不是全局视图或脚本变量。出现在 **Sources → SHYX**（`sources` 组，不是 Filters）：无输入，预设全部存在节点自己的 SM 属性里，所以工程 / Load State / 分支复制都会带着走。

## 面板

属性面板里的 **Cameras** 表（`panel_widget="shyx_camera_adjust"`）：

| 操作 | 行为 |
|------|------|
| **Add (current camera)** | 抓当前活动 RenderView 的 `Position` / `FocalPoint` / `ViewUp` / `ViewAngle` / `ParallelScale` / `ParallelProjection`，追加一行，默认名 `Camera_N` |
| **Update selected** | 用当前相机覆盖选中的第一行（名字不变） |
| **Delete selected** | 删除选中的一行或多行（Ctrl / Shift 多选） |
| **Apply to view** | 把选中行设回活动 RenderView 并重绘 |
| 双击非 **Name** 单元格 | 等同 Apply；Name 列双击是改名 |

表格只读显示数值（够核对），完整精度存在属性里。活动视图不是 3D RenderView 时 Add / Apply 会给出提示。

## 属性（换行分隔，行序对齐）

| 属性 | 每行 |
|------|------|
| `CameraNames` | 预设名（组锚点，不能设成 `panel_visibility="never"`） |
| `CameraPositions` | `x,y,z` |
| `CameraFocalPoints` | `x,y,z` |
| `CameraViewUps` | `x,y,z` |
| `CameraLens` | `viewAngle,parallelScale,parallelProjection`（0 透视 / 1 平行） |

某一行缺数据时按默认值补齐（位置 0,0,0；焦点 0,0,-1；上方向 0,1,0；30°,1,0），不会丢行。

## 输出

`vtkPolyData`，每个预设一个顶点（位置 = `CameraPositions`）：

- 点数组：`CameraIndex`（int）、`CameraName`（string）
- 字段数组：`CameraPositions`、`CameraFocalPoints`、`CameraViewUps`、`CameraLens`

XML 里默认 **Point Gaussian**，所以能在 3D 里看到机位点；切 **Point Label** 并用 `CameraName` 即可标名字。**输出本身不会移动相机**，只有 Apply 会。

## Python

```python
from paraview.simple import *

cams = SHYXCameraAdjust(CameraNames='Front\nSide')
cams.CameraPositions = '0,0,100\n100,0,0'
cams.CameraFocalPoints = '0,0,0\n0,0,0'
cams.CameraViewUps = '0,1,0\n0,1,0'
cams.CameraLens = '30,1,0\n30,1,0'

# 手动把某条预设设到活动视图
view = GetActiveViewOrCreate('RenderView')
view.CameraPosition = [0.0, 0.0, 100.0]
view.CameraFocalPoint = [0.0, 0.0, 0.0]
view.CameraViewUp = [0.0, 1.0, 0.0]
```

面板是常规编辑方式；需要在脚本里批量建预设时再直接写属性。

## 构建

`vtkSHYXCameraAdjust` 是普通 VTK 模块（`VTK::CommonCore` / `CommonDataModel` / `CommonExecutionModel`），不需要 CGAL / VMTK / Snappy；`SHYXCameraAdjust.xml` 由模块 `CMakeLists.txt` 的 `vespa_plugin_xml()` 注册。客户端表格在 `ParaViewPlugin/widgets/pqSHYXCameraAdjustWidget.*`，由 `ParaViewPlugin/cmake/PropertyWidgets.cmake` 注册为 `shyx_camera_adjust`。
