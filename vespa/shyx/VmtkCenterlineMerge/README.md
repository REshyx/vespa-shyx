# vtkSHYXVmtkCenterlineMerge

重叠中心线的 **Extract**、**Merge**、**Reconstruct** 三步可单独勾选。一个输出端口。需 `VESPA_USE_VMTK`。不需要原血管面。全关则透传输入。

典型输入：若干条 Voronoi 源→汇路径 → **Append Geometry** → 本滤镜。一条 cell 一条完整 polyline，带点半径（默认 `MaximumInscribedSphereRadius`）。不要先拆成两点 `VTK_LINE`。

| 勾选 | 输出 |
|------|------|
| 只 **Extract branches** | 切开的 tract（`GroupIds` / `Blanking` 等），路径仍重叠 |
| Extract + Merge | 共享顶点的分支树（线）；Merge 仅在 Extract 勾选时出现 |
| 只 **Reconstruct surface** | 对重叠输入做 polyball；胯部 MISR 还在，更圆 |
| Extract + Reconstruct（不 Merge） | 对 tract 做 polyball，Blanking 段球还在 |
| Extract + Merge + Reconstruct | 先合并再 polyball；胯部更锐 |

**Extract**：`vtkvmtkCenterlineBranchExtractor`（慢：成对半径管检测）。tract 结果缓存在滤镜里；开关 Merge / Reconstruct 不会重算，除非上游中心线、半径数组或 **Split by shared endpoints** 变了。

**Split by shared endpoints**（默认开）：按端点是否落在 `max(半径)` 内把输入 polyline 分成若干组，再**各组** Extract / Merge 后拼回一个端口。Append 进来的额外重叠路径只会进它真正共用开口的那一组，不会把互不相交的入口簇焊成一棵树。只有一组时和以前一样。关：整份输入一次 Extract/Merge（VMTK 按单棵源→汇树假设）。

**Merge**：`vtkvmtkMergeCenterlines`，对每个端点簇单独 Merge 再拼（不要先拼再一次 Merge，否则 `GroupIds` 会撞、Clean 也可能把远处点焊上）。**Merge blanked** 默认开，分叉共享顶点。**Resampling step length** 是绝对长度；面板 Scale/Reset 来自 BoundsDomain `scaled_extent` 0.01（Reset = 输入 AABB 最长边的 1%）。属性 **0** 时 Apply 用同一规则，不走 VMTK 的对角线 1%。

**Reconstruct**：`vtkvmtkPolyBallModeller`（polyball line）按 **Sample dimensions** 采样再抽等值面。dist^2 − r^2，**Iso value 0** 是管壁。

参数见同目录 `SHYXVmtkCenterlineMerge.xml`。
