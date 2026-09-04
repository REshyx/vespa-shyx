# SkeletonEndClipper（骨架提取 + 端点裁剪）

独立组合滤镜：先跑 **SHYX Skeleton Extraction**，再用提取的中心线跑 **SHYX Vessel End Clipper**。原来的两个滤镜不变；需要分开调参或把骨架接到别的节点时仍用那两个。

菜单：**Filters → SHYX** 与 **Filters → Vascular** 工具条（切端图标后面一项，标签 **SHYX Skeleton End Clipper**）。

## 输入 / 输出

- **输入**：闭合（水密）血管三角表面。
- **端口 0（Clipped Mesh）**：切过（可选封口）的几何，与 Vessel End Clipper 端口 0 相同（含 cell `EndpointIndex`）。
- **端口 1（Skeleton + Clip Planes）**：骨架折线，加上 clip 平面原点（vertex，给 **Point Label**）和短延长方向线。首次显示默认 Point Label；`VertexOnly` 只标 clip 原点，不标骨架顶点。

交互切平面 widget 与 End Clipper 相同（`shyx_endclipper_plane_handles`）。端口 1 里 clip 的点排在骨架点前面（每个端点 2 点：原点 + 方向柄）。

## 参数

骨架侧与 `vtkCGALSkeletonExtraction` 一致；裁剪侧与 `vtkCGALVesselEndClipper` 一致（含 Endpoints to Clip、Min Branch Length、Cap Endpoints）。
