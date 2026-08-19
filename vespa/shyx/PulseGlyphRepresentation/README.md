# PulseGlyphRepresentation — Pulse Glyphs

Display 下拉 **`Pulse Glyphs`**。不要 `PulseGlyphRepresentation()`。Python：`GetDisplayProperties().Representation = 'Pulse Glyphs'`，属性用 **`PG_*`**（如 `disp.PG_Animate`、`PG_OverallScale`）以及继承的 `PulseGlyph_GlyphType` / LOD。

## 示意图

![PulseGlyphRepresentation](../../../illustrate/PulseGlyphRepresentation.png)

## 1. 目的与功能算法详细解释 🧠

### 核心目的
本模块旨在提供一种支持**随时间动态脉动效果**的三维图元 (Glyphs) 渲染表示 (Representation)。该模块继承自 `vtkGlyph3DRepresentation`，通过在管线中动态驱动 `vtkGlyph3DMapper` 的缩放 (Scale) 与旋转 (Orientation) 属性，使得点云或矢量场数据伴随时间或帧数呈现出周期性的脉冲动画效果。

### 工作原理与算法
动态表现的底层逻辑基于一套相位包络 (Phase Envelope) 算法：
1. **动画驱动器**: 借助 `ParaViewPlugin/Representations/pqPulseGlyphAnimationManager` 统筹管理，当视图中包含开启了 `PG_Animate` 的脉冲图元时，管理器将连续触发视图重绘 (`view->render()`)，以生成连续的动画帧。
2. **混合相位计算 (Mix Value)**: 读取 `PG_AnimationCoordinateArray`（找不到则用到原点的点半径）× `PG_IntegrationScale`，再加 **renderFrame × PG_TimeScale**（不是墙钟时间）。
3. **包络函数 (Envelope)**: 相位小数部分经 `PG_Trunc` 钳制到 `[0, 1]`，再 `1.0 - pow(clamped, PG_Pow)`。
4. **变换映射**:
   - **缩放**: 受 `PG_PulseAffectsScale` 门控；可叠加 `PG_ExtraScaleArray`。
   - **旋转**: 包络 × `PG_RotationSweep`。`PG_Shuffle` 用 `std::minstd_rand`，种子来自 mixValue。

---

## 2. 参数列表及其效果和含义 🎛️

Python 用 Display 上的 **exposed 名**（`disp.PG_*`）。XML 内部属性名不同（如 `PulseOverallScale` → `PG_OverallScale`）。默认值与同目录 `PulseGlyphRepresentation.xml` 一致：

| 参数名称 | 类型 | 默认 | 含义与效果 |
| :--- | :---: | :---: | :--- |
| **PG_Animate** | `bool` | 1 | **动画总开关**。 |
| **PG_TimeScale** | `double` | 0.4 | **时间缩放系数**。控制脉冲动画随时间演化的频率与速度。 |
| **PG_IntegrationScale** | `double` | 50 | **空间频响**。作为空间坐标或目标数组的乘数，控制脉冲状态在空间分布上的密集度与变化率。 |
| **PG_Trunc** | `double` | 2 | **截断因子**。决定脉冲包络的波形截断占比，控制图元处于极值状态的时长比例。 |
| **PG_Pow** | `double` | 1 | **衰减指数**。控制脉动强度衰减曲线的平滑度（线性或指数级衰减）。 |
| **PG_OverallScale** | `double` | 1 | **全局缩放系数**（XML `PulseOverallScale`）。最终 PulseGlyphScale 乘以该系数。 |
| **PG_AnimationCoordinateArray** | `string` | `IntegrationTime` | **相位源数组**。找不到则用到原点的点半径。 |
| **PG_ExtraScaleArray** | `string` | `None` | **附加缩放数组**。其幅值可叠加到包络项。 |
| **PG_ArrayAffectScale** | `bool` | 1 | **附加缩放使能**。是否让 `PG_ExtraScaleArray` 参与缩放。 |
| **PG_ArrayAffectScaleRatio** | `double` | 1 | **附加缩放比率**。数组幅值乘到包络项之前的权重。 |
| **PG_PulseAffectsScale** | `bool` | 1 | **缩放脉动使能**。时间脉冲包络是否作用于尺寸。 |
| **PG_PulseAffectsRotation** | `bool` | 0 | **旋转脉动使能**。时间脉冲包络是否作用于姿态。 |
| **PG_Shuffle** | `bool` | 0 | **随机离散模式**。用 `std::minstd_rand`（种子来自 mixValue）打破同步脉动。 |
| **PG_RotationSweep** | `double[3]` | 360 360 360 | **最大旋转欧拉角**（度）。包络映射到各轴 0–该分量。 |