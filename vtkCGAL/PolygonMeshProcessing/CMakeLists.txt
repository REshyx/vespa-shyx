set(vtkcgalpmp_files
  vtkCGALBooleanOperation
  vtkCGALFairRegion
  vtkCGALIsotropicRemesher
)
vtk_module_add_module(vtkCGALPMP
  CLASSES ${vtkcgalpmp_files}
)
