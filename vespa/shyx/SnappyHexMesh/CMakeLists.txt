set(classes vtkSHYXSnappyHexMesh)
vtk_module_add_module(vtkSHYXSnappyHexMesh
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXSnappyHexMesh PRIVATE SHYXSnappyHex::SHYXSnappyHex)
