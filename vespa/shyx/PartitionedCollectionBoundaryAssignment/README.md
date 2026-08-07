# vtkSHYXPartitionedCollectionBoundaryAssignment

对 `vtkPartitionedDataSetCollection` 按 side set 面积降序分类 wall / inlet / outlet，输出边界映射文本、入口 OPT 片段，以及由**两段文本驱动**的诊断图元。

与 [Partitioned Collection Boundary Fields](../PartitionedCollectionBoundaryFields/README.md) 互补：本 filter **不写** 径向/变量场。

## 端口

| Port | 内容 |
|------|------|
| 0 | 输入 PDC（默认透传；Single outlet + Merge inlets 时把多个入口 side/node 合成一对）+ FieldData 文本戳 |
| 1 | 解析两段文本绘制：Point Label 标出 **全部** wall/inlet/outlet 的 sideset id（映射/合并后）；**仅入口**画 AABB + 法向（数值来自 options 文件中的 inlet_*，**合并前**各入口，bounds 按 `1/BoundsScale` 还原） |

**Custom adapter**（默认开）：前两行（表头 + `nodeset: ...` 真实 ENTITY_ID）不变；数据行 sideset id 重映射为 wall→3、inlet→1、outlet→21 起顺延。

**Merge inlets into one side set**（默认开）：仅在 **Single outlet** 下生效；先按各入口统计 inlet_*，再合并；Boundary assignment 看合并后映射。

第二个文本为完整 **options 文件**（非片段）：
- **Single inlet** → `options_template_single_inlet.txt`（PV / `options_PV_BJSJT_20260605`）
- **Single outlet** → `options_template_single_outlet.txt`（HV / `options_HV_BJSJT_20260605`）

其中 `-inlet_*` / `-num_outlet` 由本 filter 填入；其余路径等保持模板默认，可导出后手工改。

面板可改三个导出文件名（随 Flow Boundary Mode 默认 PV/HV）：
- **Exodus**：`PV_0.exo` / `HV_0.exo`
- **Options (.opt)**：`options_PV_0.opt` / `options_HV_0.opt`（完整 options 文本）
- **Boundary assignment (.bc)**：`options_PV_0.bc` / `options_HV_0.bc`

按钮 **Export port 0 (.exo) + options (.opt) + assignment (.bc)**：选目录后按上述名称写出三份文件。

## 典型管线

```
… → SHYX DataSet To Partitioned Collection
  → SHYX Partitioned Collection Boundary Fields   # 可选
  → SHYX Partitioned Collection Boundary Assignment
     ├─ port 0 → 导出 / 下游
     └─ port 1 → Point Label（核对文本与入口 OPT）
```

## ParaView

菜单：**SHYX** / **Vascular** → **SHYX Partitioned Collection Boundary Assignment**
