# vtkSHYXAlphaWrapping

菜单 **SHYX Alpha Wrapping**。CGAL alpha wrap：点云或三角面片汤 → **严格包住输入的水密 2-流形**。后端与 `vtkCGALAlphaWrapping`（VESPA Alpha Wrapping）相同，需要 **CGAL ≥ 5.5**。

与 VESPA 的差别：**Alpha / Offset 始终是输入长度单位**。默认 **0** 分别用包围盒最长边的 **5%** / **3%**（与面板 BoundsDomain `scaled_extent` 一致；点 Scale/Reset 可填入建议值）。没有「对角线百分比」开关。本次实际用到的值写在 field data `SHYXAlphaWrappingAlpha`、`SHYXAlphaWrappingOffset`。

| 参数 | 含义 |
| :--- | :--- |
| `Alpha` | 结果面外接圆半径上界；越小越贴凹陷。0 = 0.05 × AABB 最长边。 |
| `Offset` | 相对输入的膨胀量，须为正。0 = 0.03 × AABB 最长边。 |
| `UpdateAttributes` | 高级；默认开，把输入属性插到结果网格。 |

输入：`vtkPolyData`（点或三角形即可，不要求已是流形）。新管线用本滤镜，不必再开 VESPA Alpha Wrapping。
