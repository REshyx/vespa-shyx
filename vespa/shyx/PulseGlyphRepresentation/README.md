# PulseGlyphRepresentation - 脉冲体素表示法 🫀

## 示意图

![PulseGlyphRepresentation](../../../illustrate/PulseGlyphRepresentation.png)

## 1. 目的与功能算法详细解释 🧠

### 核心目的
本模块旨在提供一种支持**随时间动态脉动效果**的三维图元 (Glyphs) 渲染表示 (Representation)。该模块继承自 `vtkGlyph3DRepresentation`，通过在管线中动态驱动 `vtkGlyph3DMapper` 的缩放 (Scale) 与旋转 (Orientation) 属性，使得点云或矢量场数据伴随时间或帧数呈现出周期性的脉冲动画效果。

### 工作原理与算法
动态表现的底层逻辑基于一套相位包络 (Phase Envelope) 算法：
1. **动画驱动器**: 借助 `pqPulseGlyphAnimationManager` 统筹管理，当视图中包含开启了 `Animate` 的脉冲图元时，管理器将连续触发视图重绘 (`view->render()`)，以生成连续的动画帧。
2. **混合相位计算 (Mix Value)**: 针对数据集中的每个顶点，算法读取指定的 `AnimationCoordinateArray`（若未指定则采用空间坐标）的值。将其乘以空间频响系数 `IntegrationScale`，并叠加基于时间驱动项（当前时间或帧数与 `TimeScale` 的乘积），以此得到该顶点的混合相位。
3. **包络函数 (Envelope)**: 相位数据将被传入非线性包络函数中。首先提取小数部分并根据 `Trunc` 进行区间截断和限制（钳制在 `[0, 1]` 之间），最后通过幂次运算 `1.0 - pow(clamped, Pow)` 计算出当前时刻的脉冲强度包络值。
4. **变换映射**:
   - **缩放 (Scale)**: 顶点的缩放系数计算为 `OverallScale * (Envelope + ExtraArrayMagnitude * ArrayAffectScaleRatio)`。
   - **旋转 (Orientation)**: 脉冲包络值将乘上 `RotationSweep` 中设定的最大欧拉角。若启用 `Shuffle` 模式，每个轴向将生成独立的伪随机相位偏差，以表现非均匀的随机动态旋转。

---

## 2. 参数列表及其效果和含义 🎛️

本表示模块提供的核心配置参数如下：

| 参数名称 | 类型 | 含义与效果 |
| :--- | :---: | :--- |
| **Animate** | `bool` | **动画总开关**。设为 `true` 时，开启时间与帧序列演化，视图将执行连续重绘。 |
| **TimeScale** | `double` | **时间缩放系数**。控制脉冲动画随时间演化的频率与速度。 |
| **IntegrationScale** | `double` | **空间频响**。作为空间坐标或目标数组的乘数，控制脉冲状态在空间分布上的密集度与变化率。 |
| **Trunc** | `double` | **截断因子**。决定脉冲包络的波形截断占比，控制图元处于极值状态的时长比例。 |
| **Pow** | `double` | **衰减指数**。控制脉动强度衰减曲线的平滑度（线性或指数级衰减）。 |
| **PulseOverallScale** | `double` | **全局缩放系数**。图元计算得出的最终动态缩放量都将乘以该全局系数。 |
| **AnimationCoordinateArray**| `string` | **相位源数组**。指定用于驱动空间相位的点数据数组名称（默认值为 `IntegrationTime`）。若未找到，算法将默认使用空间坐标计算。 |
| **ExtraScaleArray** | `string` | **附加缩放数组**。指定另一个数据数组，其数值大小将额外作用于图元的基准缩放。 |
| **ArrayAffectScale** | `bool` | **附加缩放使能**。是否允许 `ExtraScaleArray` 参与最终的缩放计算。 |
| **ArrayAffectScaleRatio** | `double` | **附加缩放比率**。定义额外数组的幅值影响基础缩放的权重系数。 |
| **PulseAffectsScale** | `bool` | **缩放脉动使能**。设置时间脉冲包络是否作用于图元的尺寸变化。 |
| **PulseAffectsRotation** | `bool` | **旋转脉动使能**。设置时间脉冲包络是否作用于图元的姿态旋转。 |
| **Shuffle** | `bool` | **随机离散模式**。开启后，将引入伪随机偏差以打破统一的同步演化，使每个图元呈现独立的脉动和旋转状态。 |
| **RotationSweep** | `double[3]`| **最大旋转欧拉角**。定义脉冲作用在 X, Y, Z 三轴上允许达到的最大旋转角度范围。 |