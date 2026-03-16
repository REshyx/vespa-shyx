set(vtkcgalpmp_files
  vtkCGALBooleanOperation
  vtkCGALDensityBasedSampler
  vtkCGALSurfaceToVolumeMesh
  vtkSHYXTetGen
  vtkCGALIsotropicRemesher
  vtkCGALMeshChecker
  vtkCGALMeshDeformation
  vtkCGALMeshSubdivision
  vtkCGALPatchFilling
  vtkCGALRegionFairing
  vtkCGALShapeSmoothing
  vtkCGALSignedDistanceFunction
  vtkCGALSkeletonExtraction
  vtkCGALVesselEndClipper
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

if (${USE_CERES})
  list(APPEND vtkcgalpmp_files
    vtkCGALMeshSmoothing
  )
  set(VESPA_MESH_SMOOTHING ON CACHE INTERNAL "vtkCGALMeshSmoothing state" FORCE)
else()
  message(WARNING "Mesh smoothing is disabled because Vespa is configured without the Ceres library.")
  set(VESPA_MESH_SMOOTHING OFF CACHE INTERNAL "vtkCGALMeshSmoothing state" FORCE)
endif()

vtk_module_add_module(vtkCGALPMP
  ${FORCE_STATIC_MODULES_STRING}
  CLASSES ${vtkcgalpmp_files}
)

vtk_module_link(vtkCGALPMP PRIVATE CGAL::Eigen3_support tet)
target_include_directories(vtkCGALPMP PRIVATE ${CMAKE_SOURCE_DIR}/TetGen-1.6.0)

if (VESPA_USE_SMP)
  target_compile_definitions(vtkCGALPMP PRIVATE VESPA_USE_SMP)
  message(STATUS "vtkCGALDensityBasedSampler: SMP optimization ENABLED")
else()
  message(STATUS "vtkCGALDensityBasedSampler: SMP optimization disabled (for official ParaView compatibility)")
endif()

# ensure correct link of module
# OPTIONAL_DEPENDS does not work here for an unknown reason
if (${USE_CERES})
  vtk_module_link(vtkCGALPMP PRIVATE Ceres::ceres)
endif()
