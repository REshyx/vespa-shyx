# vtkSHYXPartitionedCollectionBoundaryAssignment

对 `vtkPartitionedDataSetCollection` 按 side set 面积降序分类 wall / inlet / outlet，并按该名次重写各对的 Exodus `ENTITY_ID`（现有 node/side 号池升序赋给名次 0,1,2…，与 DataSet To Partitioned Collection 仅开 Sort By Area 时一致），输出边界映射文本、入口 OPT 片段，以及由**两段文本驱动**的诊断图元。

与 [Partitioned Collection Boundary Fields](../PartitionedCollectionBoundaryFields/README.md) 互补：本 filter **不写** 径向/变量场。

## 端口

| Port | 内容 |
|------|------|
| 0 | 输入 PDC（可选 **Repart** 按连通域拆同名 patch；按面积名次重写 side/node 的 `ENTITY_ID`；Single outlet + Merge inlets 时再把多个入口合成一对）+ FieldData 文本戳 |
| 1 | 解析两段文本绘制：Point Label 标出 **全部** wall/inlet/outlet 的 sideset id（映射/合并后）；**仅入口**画 AABB + 法向（数值来自 options 文件中的 inlet_*，**合并前**各入口，bounds 按 `1/BoundsScale` 还原） |

**面积排序后重写 ENTITY_ID**：现有 node/side 号池按升序赋给面积名次。12 个开口时典型为最大面 side `14`（wall）、第二大 `15`（Single inlet 时的入口）、其余 `16…`。几何与块名不变；Pipeline Browser 里块的先后仍是上游顺序，变的是各块上的 `ENTITY_ID`。

**Custom adapter**（默认开）：前两行（表头 + `nodeset: ...` 真实 ENTITY_ID）不变；数据行 sideset id 重映射为 wall→3、inlet→1、outlet→21 起顺延。

**Repart**（默认关）：在面积排序之前，把**同名** side/node 对按面连通域拆开，重命名为 `原名_1`、`原名_2`…（side 与对应 node 都拆），重建一份 PDC，再走后面的分类 / OPT / 可选 Merge inlets。已做过 **Merge inlets**、一个 side set 里有多块互不连通入口时打开此项，否则面积排序会把合并后的入口当成一个开口，options 里的 `inlet_*` 也对不上。单连通的同名组（典型 wall / 单出口）保持原名不拆。

**Merge inlets into one side set**（默认开）：仅在 **Single outlet** 下生效；先按各入口统计 inlet_*，再合并；Boundary assignment 看合并后映射。与 Repart 可同时开：先拆开算 OPT，再合并回一对写入 port 0。

第二个文本为完整 **options 文件**（非片段）：
- **Single inlet** → `options_template_single_inlet.txt`（PV / `options_PV_BJSJT_20260605`）
- **Single outlet** → `options_template_single_outlet.txt`（HV / `options_HV_BJSJT_20260605`）

其中 `-inlet_*` / `-num_outlet` 由本 filter 填入；其余路径等保持模板默认，可导出后手工改。

面板可改四个导出文件名。默认从顶层 reader 的 `FileName`/`FileNames` 解析病例号，去掉尾部 `_plaque` / `_aorta`，再把汉字转成无声调拼音（[pinyin-data](https://github.com/mozillazg/pinyin-data) 0.16.0，每字取第一个读音，`ü` 写成 `v`）。没有读音的非 ASCII 字符变成 `_`。ASCII 原样保留：

| 源文件示例 | 模式 | Exodus | Options（无后缀） | Nodeset（无后缀） | State |
|-----------|------|--------|-------------------|-------------------|-------|
| `K2-1_plaque.stl` | Single inlet (PV) | `PV_K2-1.exo` | `options_PV_K2-1` | `Nodeset_PV_K2-1` | `PV_K2-1.pvsm` |
| `K2-1_aorta.stl` | Single outlet (HV) | `HV_K2-1.exo` | `options_HV_K2-1` | `Nodeset_HV_K2-1` | `HV_K2-1.pvsm` |
| `张三_plaque.stl` | Single inlet (PV) | `PV_zhangsan.exo` | `options_PV_zhangsan` | `Nodeset_PV_zhangsan` | `PV_zhangsan.pvsm` |

找不到上游文件时回退为 `PV_0.exo` / `HV_0.exo`、`options_PV_0` / `options_HV_0`、`Nodeset_PV_0` / `Nodeset_HV_0`、`PV_0.pvsm` / `HV_0.pvsm`。

**Flow Boundary Mode 自动选择**（按上游文件 basename，大小写不敏感；仅在上游路径变化时重设，之后可手动改）：
- 文件名含 `plaque` 或 `pv` → **Single inlet**（PV）
- 文件名含 `aorta` 或 `hv` → **Single outlet**（HV）
- `plaque` / `aorta` 优先于 `pv` / `hv`
- 同一对同时出现（`plaque`+`aorta`，或 `hv`+`pv`），或都不含 → 不改动

按钮 **Export port 0 (.exo) + options + Nodeset + .pvsm**：选目录后按上述名称写出四份文件（options / Nodeset 不加扩展名；`.pvsm` 为当前 ParaView 管线/视图状态）。勾选 **Pack into .zip** 后改为另存一个 `.zip`：四份文件打进包内（包内文件名仍是上面四栏），目录里不另留散文件。

> **导出目录必须是纯 ASCII（不能含中文等字符）**。文件名里的汉字会在面板上转成拼音；文件夹仍是用户选的路径。Exodus 走的是 IOSS 的窄字符路径，中文目录会让写入器在底层失败并可能导致 ParaView 闪退；因此插件在写文件前会先检查 `.exo` 的完整路径，含非 ASCII 字符时只弹 warning 并中止导出（其余文件不会被写出）。请换到纯英文目录后重试。

拼音表由 `ParaViewPlugin/widgets/generate_hanzi_pinyin.py` 从 `pinyin.txt` 生成，编进 `VESPAPlugin.dll`。

已有 Exodus 网格、只要 options 时，可用同目录 `generate_options_from_exo.py`（**pvpython** + 本仓库 `VESPAPlugin.dll`）：按文件名选 HV/PV，默认参数并打开 **Repart**，在每个 `.exo` 旁写出 `options_<stem>`。

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
