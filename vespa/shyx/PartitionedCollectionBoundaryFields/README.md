# vtkSHYXPartitionedCollectionBoundaryFields

对已划分好的 `vtkPartitionedDataSetCollection`（通常来自 `vtkSHYXDataSetToPartitionedCollection`）修改边界场与 block 名称，无需重新分区。

## 功能

- **输入**：带 `vtkDataAssembly`（IOSS element_blocks / side_sets / node_sets）的 `vtkPartitionedDataSetCollection`
- **径向值**：可选计算 `BoundaryRadialValue = 1 - x^a`（点/单元数据）
- **法线**：写入 `BoundaryRadialValueNormal`（side/node set 点数据；可选累加到四面体体块）
- **变量**：按面板为每个 side set 写入 `BoundaryVariable1`、`BoundaryVariable2`、…（非零时累加到体块）
- **覆盖**：同名数组存在时直接覆盖
- **命名**：通过 **Partitioned block names** 面板编辑 side set 名称；node set 自动加 `node_` 前缀

## 属性

| 属性 | 说明 |
|------|------|
| **ComputeBoundaryRadialValue** | 是否计算径向标量并乘入法线 |
| **BoundaryRadialNormalFalloffFactor** | 指数 `a` |
| **BlockNames** | 面板维护的 block 名称（换行分隔） |
| **BoundaryVariables** | 面板 Variable 列（tab 分隔多列） |
| **BoundaryWriteNormals** | 面板 Write Normal 列（0/1） |

## 典型管线

```
vtkDataSet → SHYX DataSet To Partitioned Collection → SHYX Partitioned Collection Boundary Fields → IOSS Writer / WSL Simulation
```

## ParaView

菜单：**SHYX** / **Vascular** → **SHYX Partitioned Collection Boundary Fields**
