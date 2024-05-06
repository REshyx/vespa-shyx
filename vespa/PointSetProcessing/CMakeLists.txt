set(vtkcgalpsp_files
  vtkCGALPCAEstimateNormals
)

if (CGAL_VERSION VERSION_GREATER_EQUAL "5.3")
  list(APPEND vtkcgalpsp_files
    vtkCGALXYZReader # CGAL::IO::read_points
  )
else()
  message(WARGNING "CGAL XYZ Reader disabled because CGAL VERSION < 5.3")
endif()

vtk_module_add_module(vtkCGALPSP
  ${FORCE_STATIC_MODULES_STRING}
  CLASSES ${vtkcgalpsp_files}
)
