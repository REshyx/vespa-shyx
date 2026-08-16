# vtkSHYXTetMeshRegionPartition

划分四面体网格。默认 **METIS k-way**（`PartitionMethod=2`）；可选 min-cut / 连通域。CMake 可链 `METIS::metis`。端口 0 分区网格，端口 1 dual graph（默认 Wireframe）。不在 Vascular 工具条。

参数见 `ParaViewPlugin/SHYXTetMeshRegionPartition.xml`。
