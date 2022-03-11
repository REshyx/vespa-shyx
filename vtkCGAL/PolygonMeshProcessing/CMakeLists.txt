set(vtkcgalpmp_files
  vtkCGALPolyDataAlgorithm

  vtkCGALBooleanOperation
  vtkCGALIsotropicRemesher
  vtkCGALRegionFairing
  vtkCGALTunnelFilling
)
vtk_module_add_module(vtkCGALPMP
  CLASSES ${vtkcgalpmp_files}
)
