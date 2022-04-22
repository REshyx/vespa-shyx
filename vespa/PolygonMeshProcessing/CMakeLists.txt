set(vtkcgalpmp_files
  vtkCGALPolyDataAlgorithm

  vtkCGALBooleanOperation
  vtkCGALIsotropicRemesher
  vtkCGALMeshDeformation
  vtkCGALMeshSubdivision
  vtkCGALPatchFilling
  vtkCGALRegionFairing
  vtkCGALShapeSmoothing
)
vtk_module_add_module(vtkCGALPMP
  CLASSES ${vtkcgalpmp_files}
)
