set(vtkcgalpmp_files
  vtkCGALPolyDataAlgorithm

  vtkCGALBooleanOperation
  vtkCGALIsotropicRemesher
  vtkCGALMeshDeformation
  vtkCGALPatchFilling
  vtkCGALRegionFairing
)
vtk_module_add_module(vtkCGALPMP
  CLASSES ${vtkcgalpmp_files}
)
