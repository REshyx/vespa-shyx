# vtkSHYXDataSetToPartitionedCollection

## 示意图

![DataSetToPartitionedCollection](../../../illustrate/DataSetToPartitionedCollection.png)

## 1. 目的与功能算法详细解释

### 🎯 核心目的
`vtkSHYXDataSetToPartitionedCollection` 模块的主要任务是将 `vtkDataSet`（通常为包含四面体单元的非结构化网格）转换并重组为兼容 `vtkIOSSWriter` (Exodus 格式) 要求的 `vtkPartitionedDataSetCollection` 数据结构。该模块负责处理体网格、提取表面、按**单元数据分区标量**（默认 `EndpointIndex`，与 `vtkCGALVesselEndClipper` 输出的 cap 标记一致）将边界曲面分组为若干 patch，并构建 IOSS 格式所需的**单元块 (Element Blocks)**、**侧面集 (Side Sets)** 及 **节点集 (Node Sets)**。若该数组不存在或长度不匹配，则回退为特征角 + 连通域拆分。

### 🧠 算法流水线 (The Pipeline)
该转换过程分为以下具体步骤：

1. **提取四面体 (Tet Volume Extraction)**：从输入数据集中提取所有四面体 (`VTK_TETRA`) 单元，生成独立的 `vtkUnstructuredGrid` 对象，并为其分配连续的全局点 ID 和单元 ID（1至N），以符合 Exodus 格式对 ID 连续性的要求。
2. **表面剥离 (Surface Extraction)**：通过 `vtkDataSetSurfaceFilter` 提取四面体网格的外部边界曲面，并保留其在体网格中的 Cell IDs 与 Point IDs 映射。
3. **归属映射 (Element Side Mapping)**：识别表面每个三角形单元所属的内部四面体及其对应的面索引（Exodus 格式面编号为 1至4），并将此映射关系存储至单元数据 `element_side`，用于定义后续的侧面集 (Side Set)。
4. **单元标量分区 (Cell-array partitioning)**：读取表面单元数据中的分区数组（属性 `PartitionCellArrayName`，默认 `EndpointIndex`）。对每个单元取标量第一个分量并**四舍五入为整数**，相同整数值的单元归入同一 patch；patch 顺序为标量键升序（例如 …, -1, 1, 2, …）。若数组缺失、非数值类型或 tuple 数与单元数不一致，则退回到 **特征角分裂与连通域提取**：`vtkPolyDataNormals` + `vtkPolyDataConnectivityFilter`。若将分区数组名置为空字符串，则始终走特征角路径。
5. **点集清洗 (Stripping Unreferenced Points)**：对提取出的各个表面区域进行校验，移除区域内未引用的孤立点，确保构建出的 `side{i}` 和 `node{i}` 块中仅包含有效的顶点。
6. **节点与侧面生成 (Node & Side Sets Generation)**：将处理完毕的曲面保存为 `side{i}` 块，并在曲面上生成等效的顶点拓扑数据 (`vtkVertex`) 以形成 `node{i}` 块。在此过程中恢复所有点的原始全局 ID 映射，保证坐标的准确性。
7. **数据装配 (Assembly Building)**：最终生成一个 `vtkDataAssembly` 层级树结构，将转换后的块级数据规范地映射至 `element_blocks`、`node_sets` 及 `side_sets` 分支，完成格式转换。

## 2. 参数列表及其效果和含义

以下为该模块可供调整的配置参数：

| 参数名称 | 类型 | 默认值 | 效果和含义 |
| :--- | :---: | :---: | :--- |
| **PartitionCellArrayName** | `char*` | `EndpointIndex` | **分区单元数组名**。表面 `vtkCellData` 中用于分组的标量数组；与 `vtkCGALVesselEndClipper` 写入的 `EndpointIndex`（端点 1-based 编号，未标记为 -1）一致。置为空字符串则仅使用下面的特征角路径。 |
| **FeatureAngle** | `double` | `70.0` | **特征角阈值**（回退路径）。当分区数组不可用或拆不出 patch 时，用法线夹角判断拆分；或当数组名为空时单独使用。 |
| **SortByArea** | `int` (布尔) | `1` (True) | **按面积排序**。若启用，按各 patch 总表面积从大到小重排 `side{i}` / `node{i}`；关闭时保持分区标量升序（或连通域索引顺序）。 |
| **CustomPostReorder** | `int` (布尔) | `1` (True) | **自定义重排**：在面积排序（若启用）之后，将第 3 个 patch 移到最前、第 1 个移到最后（与原先逻辑相同）。少于 3 个 patch 时无效果。 |