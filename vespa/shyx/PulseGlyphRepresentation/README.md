# PulseGlyphRepresentation - 脉冲体素表示法 🫀

当你盯着那些毫无生气的静态模型发呆时，是否想过让它们“活”过来？`PulseGlyphRepresentation` 就是你的救星！它不仅是一个 VTK/ParaView 的表示（Representation）插件，更是赋予你数据生命的“心脏起搏器”。

## 1. 目的与功能算法详细解释 🧠

### 核心目的
本模块的主要目的是在 ParaView 中渲染具有**随时间发生脉动效果**的 3D 图元（Glyphs）。它继承自 `vtkGlyph3DRepresentation`，能够动态地驱动 `vtkGlyph3DMapper` 的缩放（Scale）和旋转（Orientation）。简而言之，就是让你的矢量或点云数据伴随着时间的流逝，像呼吸或心跳一样有节奏地放大、缩小并旋转。

### 工作原理与算法
为了实现这种“赛博朋克”式的闪烁与跳动，底层使用了一套精巧的相位包络算法：
1. **连续动画驱动**: `pqPulseGlyphAnimationManager` 作为幕后黑手，只要检测到当前视图中存在开启了 `Animate` 的脉冲图元，就会不断地踹一脚视图让其重新渲染（`view->render()`），实现永动机般的连续动画。
2. **相位计算 (Mix Value)**: 对于每一个数据点，算法会提取 `AnimationCoordinateArray` 的幅值（如果没有就用空间坐标），乘以 `IntegrationScale` 得到基础空间相位。然后，它会将当前时间（或者是渲染帧数，如果开启了 `Shuffle`）乘以 `TimeScale` 后加上去，得到最终的混合相位（Mix Value）。
3. **包络函数 (Envelope)**: 相位会被送入一个魔法函数：先把小数部分乘以 `Trunc` 并钳制在 `[0, 1]` 之间，最后用 `1.0 - pow(clamped, Pow)` 计算出当前的脉动强度（Envelope）。
4. **施加变换**:
   - **缩放**: 点的缩放值等于 `OverallScale * (Envelope + ExtraArrayMagnitude * ArrayAffectScaleRatio)`。
   - **旋转**: 将包络值乘以 `RotationSweep` 中定义的最大欧拉角。如果开启了 `Shuffle`，各个轴还会生成独立的伪随机相位，让旋转看起来更具魔性。

---

## 2. 参数列表及其效果和含义 🎛️

不要被这堆参数吓到，掌握它们你就是灯光师：

| 参数名称 | 类型 | 含义与效果 |
| :--- | :---: | :--- |
| **Animate** | `bool` | **起搏器开关**。设为 `true` 时，开启时间/帧动画，视图会疯狂连续重绘。 |
| **TimeScale** | `double` | **岁月如梭系数**。控制脉冲动画随时间演化的快慢。 |
| **IntegrationScale** | `double` | **空间频响**。乘在坐标或数组上，控制脉冲在空间分布上的密集程度。 |
| **Trunc** | `double` | **截断因子**。影响脉冲包络被钳制的范围，控制图元在最大/最小状态的停留时间。 |
| **Pow** | `double` | **平滑指数**。决定了脉冲衰减的平滑度（线性还是指数级）。 |
| **PulseOverallScale** | `double` | **全局体型**。所有图元最终算出的缩放值都会乘以这个整体系数。 |
| **AnimationCoordinateArray**| `string` | **相位源泉**。驱动动画的点数据数组名（默认是 `IntegrationTime`）。找不着就用 xyz 坐标硬算。 |
| **ExtraScaleArray** | `string` | **增肌粉数组**。额外提供缩放影响的数组，让你用另一个数据维度把图元撑大。 |
| **ArrayAffectScale** | `bool` | **增肌粉开关**。是否允许 `ExtraScaleArray` 影响最终的缩放。 |
| **ArrayAffectScaleRatio** | `double` | **增肌粉浓度**。额外数组幅值影响缩放的比例系数。 |
| **PulseAffectsScale** | `bool` | **缩放脉冲**。控制时间脉冲包络是否要作用在大小上（呼吸效果）。 |
| **PulseAffectsRotation** | `bool` | **旋转脉冲**。控制时间脉冲包络是否要作用在姿态上（扭秧歌效果）。 |
| **Shuffle** | `bool` | **群魔乱舞模式**。开启后，通过伪随机数打破时间的统一步调，让每个点各自为战地跳动和旋转。 |
| **RotationSweep** | `double[3]`| **最大扭转角**。控制 X, Y, Z 轴上允许的最大欧拉角范围。 |
