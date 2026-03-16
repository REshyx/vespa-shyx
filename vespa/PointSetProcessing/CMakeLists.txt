set(vtkcgalpsp_files
  vtkCGALPCAEstimateNormals
  vtkCGALXYZReader
)

vtk_module_add_module(vtkCGALPSP
  ${FORCE_STATIC_MODULES_STRING}
  CLASSES ${vtkcgalpsp_files}
)
if(TARGET CGAL::TBB_support)
  vtk_module_link(vtkCGALPSP PRIVATE CGAL::TBB_support)
endif()
