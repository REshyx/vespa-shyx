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

if (${CGAL_VERSION} VERSION_GREATER_EQUAL 5.5)
  # Alpha wrapping module has been introduced in CGAL 5.5
  list(APPEND vtkcgalpmp_files
    vtkCGALAlphaWrapping
  )
else()
  message(STATUS "CGAL Version lower than 5.5 (${CGAL_VERSION}), Alpha Wrapping is disabled.")
endif()

vtk_module_add_module(vtkCGALPMP
  ${FORCE_STATIC_MODULES_STRING}
  CLASSES ${vtkcgalpmp_files}
)
