set(vtkcgalalgorithm_files
  vtkCGALPolyDataAlgorithm
)

vtk_module_add_module(vtkCGALAlgorithm
  ${FORCE_STATIC_MODULES_STRING}
  CLASSES ${vtkcgalalgorithm_files}
)
