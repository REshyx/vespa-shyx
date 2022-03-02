set(vtkcgalpmp_files
  vtkCGALBooleanOperation
  vtkCGALIsotropicRemesher
  vtkCGALRegionFairing
)
vtk_module_add_module(vtkCGALPMP
  CLASSES ${vtkcgalpmp_files}
)
