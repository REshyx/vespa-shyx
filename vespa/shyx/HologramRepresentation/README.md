# HologramRepresentation — Hologram

Display 下拉 **`Hologram`**。不要 `HologramRepresentation()`。Python：`GetDisplayProperties().Representation = 'Hologram'`，属性用 **`HG_*`**。

表面网格按原样画。体网格会把每个体单元拆成它的全部面，内部的面也画，不是只留外皮：四面体是四个三角形，六面体和体素是六个四边形（GPU 剖开时对角线不当作线框），楔形、金字塔、五/六棱柱以及 Image / Rectilinear / Structured 的三维单元同样处理。高阶单元和多面体按各个面剖成三角形。相邻单元各出一份共面，背面剔除只留朝向相机的那一份，所以从里面也能看见。同一个数据集里的二维面保留，折线不画。

线来自 VTK 的 screen-space edge mask（`Surface With Edges` 那套 `emix`），面用平坦法线做菲涅尔。不是空气里的体积晶格。`HG_Flow` 选一种三维噪声，在数据集自己的坐标里取样。Display 里平移或旋转物体时，图案跟着网格走，不留在世界坐标轴上，也不跟着相机走。这几种噪声本身没有轴向，也不会顺着视线推一层扫带。体网格面数大约是单元数的四五倍以上，大网格会更重。Maelstrom 是三次三维域扭曲，比另外两档重。

* **Foam**： [Foaming waves](https://www.shadertoy.com/view/7sBcRG) 的三维版。旋转梯度，细纹转得更慢。
* **Turbulence warp**： [Tribulence Warp](https://www.shadertoy.com/view/NXS3DG) 抬到三维。三角波反复拧坐标，亮的是拧到原点附近的那一缕。
* **Maelstrom**： [Maelstrom_](https://www.shadertoy.com/view/MflyRf) 的三维域扭曲。fBm 在三个方向上各取一次再推回去，相位在原地变，不沿某根轴滑。

## 两种样式

* **Lattice**：线框一直在。面平时很淡，噪声经过时亮起来。
* **Ghost**（默认）：线和面都交给菲涅尔。默认 **Rim** 是掠射反光，三角面擦过视线时才出现；**Facing camera** 则是正对相机的面出现。噪声经过时也会把当前这一块再点亮。

背面剔除，避免封闭网格正反面叠两次。颜色用 Display 的 **Solid Color**（或标量着色）。**Line Width** 是线宽。整体 **Opacity** 会再乘一次。

动画由 `ParaViewPlugin/Representations/pqHologramAnimationManager` 在 `HG_Animate` 打开时连续重绘。

## 参数

| 参数 | 默认 | 作用 |
| --- | --- | --- |
| `HG_Animate` | 1 | 扫光是否随时间移动 |
| `HG_Style` | Ghost (1) | Lattice 或 Ghost |
| `HG_Flow` | Foam (0) | 0 Foam，1 Turbulence warp，2 Maelstrom。都没有轴向 |
| `HG_WireOpacity` | 0.01 | 三角边不透明度 |
| `HG_FaceOpacity` | 0.2 | 面的强度。Lattice 里静止时大约一成，扫光时抬高 |
| `HG_FresnelPower` | 4 | 越大，反光带越窄 |
| `HG_FresnelMode` | Rim (0) | 0 掠射反光，1 朝向相机 |
| `HG_ScanStrength` | 2 | 扫光强度，0 关掉 |
| `HG_ScanSpeed` | 2 | 图案在原地变化的快慢。负值反向 |
| `HG_BandCount` | 4 | 包围盒里图案有多密。Foam 原 shader 从 4 起，另外两档大约 5 到 7 |
| `HG_NoiseScale` | 0.5 | 把噪声放大。大于 1 时亮斑更大 |
| `HG_NoisePower` | 3 | 噪声对比。大于 1 时只留峰顶，一小块亮、其余暗 |

做法来自表面全息，不是体积步进：线框用重心/屏幕空间边（这里走 VTK 已有的 edge equation，见 [barycentric wireframe](https://tchayen.github.io/posts/wireframes-with-barycentric-coordinates)），鬼面用 `pow(1 - abs(N·V), power)`（[hologram fresnel + scan](https://design.solhun.com/en/three/hologram-shader/)）。三种流动分别来自 [Foaming waves](https://www.shadertoy.com/view/7sBcRG)、[Tribulence Warp](https://www.shadertoy.com/view/NXS3DG)、[Maelstrom_](https://www.shadertoy.com/view/MflyRf)，锁在模型包围盒上，跟着物体的平移和旋转走，不跟屏幕走。
