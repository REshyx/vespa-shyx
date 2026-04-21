set(classes
  vtkSHYXMeshChecker)
vtk_module_add_module(vtkSHYXMeshChecker
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXMeshChecker PRIVATE CGAL::Eigen3_support)

if (TARGET TBB::tbb)
  # Avoid a separate INTERFACE library: it is not in the vespa export set and breaks install(EXPORT).
  target_compile_definitions(vtkSHYXMeshChecker PRIVATE VTKSHYXMESHCHECKER_HAVE_TBB)
  vtk_module_link(vtkSHYXMeshChecker PRIVATE TBB::tbb)
endif ()
