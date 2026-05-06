# CGAL 各向同性 Remesh 流程概要

本文说明 [CGAL Polygon Mesh Processing](https://doc.cgal.org/latest/Polygon_mesh_processing/index.html) 包中 **`CGAL::Polygon_mesh_processing::isotropic_remeshing()`** 所实现的**增量式三角形各向同性 remesh**在逻辑上分为哪些阶段。算法来源为 Botsch 等人的局部增量 remesh 思路（边分裂、边塌缩、边翻转与 Laplacian 平滑的组合），在 CGAL 文档中有集中描述。

官方入口：

- 用户手册章节 *Local Isotropic Incremental Remeshing*：  
  https://doc.cgal.org/latest/Polygon_mesh_processing/index.html#title31  
- 函数 `isotropic_remeshing()`：  
  https://doc.cgal.org/latest/Polygon_mesh_processing/group__PMP__meshing__grp.html#ga412f696ec3009074bf957f1bba638248  
- 示例 `isotropic_remeshing_example.cpp`：  
  https://doc.cgal.org/latest/Polygon_mesh_processing/Polygon_mesh_processing_2isotropic_remeshing_example_8cpp-example.html  

---

## 1. 整体：双层循环

| 层次 | 含义 |
|------|------|
| **外层** | 对选定面片区域重复执行固定**操作序列**若干次；重复次数由 **迭代次数**（`number_of_iterations`）控制。迭代越多，网格通常越接近目标边长、外观越平滑。 |
| **内层（单次迭代）** | 依次执行 CGAL 文档列出的四类局部网格操作，并在需要时用 **切向松弛** 与 **重投影** 配合 sizing 场。 |

**Sizing 场**给出局部**目标边长**（均匀场或基于曲率的自适应场等），与迭代次数一起是算法的主要控制量。

---

## 2. 单次迭代内的核心子流程（与文档一致）

 CGAL 将每次迭代中的工作概括为对三角网格**增量**执行的以下操作（文档原意：edge splits、edge collapses、edge flips、Laplacian smoothing；顶点在 remesh 补片上被**重投影**回输入曲面以保持逼近）。

在 API 中可通过命名参数单独开关（默认多为开启）：

1. **边分裂（split）** — `do_split`（默认 `true`）  
   相对 sizing 场**过长**的边被分裂，使网格在需要处变密。

2. **边塌缩（collapse）** — `do_collapse`（默认 `true`）  
   相对 sizing 场**过短**的边被塌缩，使网格在需要处变疏。

3. **边翻转（flip）** — `do_flip`（默认 `true`）  
   通过翻转边改善三角形形状与顶点度数（valence）。

4. **切向松弛（tangential relaxation）** — `number_of_relaxation_steps`（默认每轮迭代 `1` 次）  
   等价于文档中的 **Laplacian smoothing** 思想在局部的使用；步数可按需增大。

5. **重投影（projection）** — `do_project`（默认 `true`）  
   在创建或移动顶点后，将其投影回**输入曲面**（可用自定义 `projection_functor`），以维持几何逼近。

上述 1–5 构成文档所说的**“上述操作序列”**的一次完整扫描；外层再重复该序列多轮。

---

## 3. 约束与特征线相关的准备步骤（常用但非内核“一步”）

当需要对**约束边 / 折线**做保护时，若约束段相对目标边长**过长**，仅保护可能导致无法在有限步内达到目标边长，甚至出现与分裂相关的终止问题。CGAL 文档建议：在调用 `isotropic_remeshing()` **之前**，对约束边列表先调用 **`split_long_edges()`**，把过长边按 sizing 预先细分到合适长度。

典型示例流程（与官方 `isotropic_remeshing_example.cpp` 一致）：

1. 收集边界或待保护的约束边；  
2. 若启用约束保护，先 **`split_long_edges()`**；  
3. 再对选定面范围调用 **`isotropic_remeshing()`**，并设置 **`edge_is_constrained_map`** 等命名参数及迭代次数。

---

## 4. 其他与流程相关的 API 选项（不改变主序列顺序）

- **`relax_constraints`**：为 `true` 时，约束边/边界顶点可在松弛阶段**沿所属折线**滑动（默认 `false`）。  
- **`allow_move_functor`**：在松弛步中逐点判断是否允许某次移动。  

这些影响**约束下**平滑与投影的行为，但不替代“分裂—塌缩—翻转—松弛—投影”的主线。

---

## 5. 小结：从高到低可以这样记

1. **预处理（视需求）**：三角化区域、必要时对约束边 `split_long_edges()`。  
2. **外层**：重复 `number_of_iterations` 次。  
3. **内层每次迭代**：**分裂过长边 → 塌缩过短边 → 边翻转 → 切向松弛（多步可配）→ 投影回输入曲面**（各步可按命名参数关闭或使用自定义投影）。  
4. **Sizing 场**：全程决定“多长算长、多短算短”。

若需要**基于 Delaunay 细化、带可证明性质**的另一条 remesh 路线，CGAL 在同一包中还提供 **`surface_Delaunay_remeshing()`**，与本增量各向同性 remesh **不是同一套流程**，此处不展开。
