set(vtkcgalsr_files
  vtkCGALPoissonSurfaceReconstructionDelaunay
  vtkCGALAdvancingFrontSurfaceReconstruction
)
vtk_module_add_module(vtkCGALSR
  ${FORCE_STATIC_MODULES_STRING}
  CLASSES ${vtkcgalsr_files}
)
if (${CGAL_VERSION} VERSION_GREATER_EQUAL 5.1)
  vtk_module_link(vtkCGALSR PRIVATE CGAL::Eigen3_support)
else()
  vtk_module_link(vtkCGALSR PRIVATE Eigen3::Eigen)
endif()
