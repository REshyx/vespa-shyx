set(vtkcgalpsp_files
  vtkCGALXYZReader
  vtkCGALPCAEstimateNormals
)
vtk_module_add_module(vtkCGALPSP
  ${FORCE_STATIC_MODULES_STRING}
  CLASSES ${vtkcgalpsp_files}
)
