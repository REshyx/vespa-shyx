set(vtkcgalpmp_files
  vtkCGALPolyDataAlgorithm

  vtkCGALBooleanOperation
  vtkCGALDelaunay2
  vtkCGALIsotropicRemesher
  vtkCGALMeshDeformation
  vtkCGALMeshSubdivision
  vtkCGALPatchFilling
  vtkCGALRegionFairing
)
vtk_module_add_module(vtkCGALPMP
  CLASSES ${vtkcgalpmp_files}
)
