set(vtkcgalpmp_files
  vtkCGALBooleanOperation
  vtkCGALIsotropicRemesher
  vtkCGALMeshChecker
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
  set(VESPA_ALPHA_WRAPPING ON CACHE INTERNAL "vtkCGALAlphaWrapping state" FORCE)
else()
  message(STATUS "CGAL Version lower than 5.5 (${CGAL_VERSION}), Alpha Wrapping is disabled.")
  set(VESPA_ALPHA_WRAPPING OFF CACHE INTERNAL "vtkCGALAlphaWrapping state" FORCE)
endif()

find_package(Ceres)
if (${CERES_FOUND})
  list(APPEND vtkcgalpmp_files
    vtkCGALMeshSmoothing
  )
  set(VESPA_MESH_SMOOTHING ON CACHE INTERNAL "vtkCGALMeshSmoothing state" FORCE)
else()
  message(WARNING "Mesh smoothing is disabled because it requires the Ceres Library, which is not available.")
  set(VESPA_MESH_SMOOTHING OFF CACHE INTERNAL "vtkCGALMeshSmoothing state" FORCE)
endif()

vtk_module_add_module(vtkCGALPMP
  ${FORCE_STATIC_MODULES_STRING}
  CLASSES ${vtkcgalpmp_files}
)
