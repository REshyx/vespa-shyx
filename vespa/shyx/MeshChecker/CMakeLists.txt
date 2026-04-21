set(classes
  vtkSHYXMeshChecker)
vtk_module_add_module(vtkSHYXMeshChecker
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXMeshChecker PRIVATE CGAL::Eigen3_support)

if (TARGET TBB::tbb)
  if (NOT TARGET _vtkSHYXMeshChecker_tbb)
    add_library(_vtkSHYXMeshChecker_tbb INTERFACE)
    target_compile_definitions(_vtkSHYXMeshChecker_tbb INTERFACE VTKSHYXMESHCHECKER_HAVE_TBB)
  endif ()
  vtk_module_link(vtkSHYXMeshChecker PRIVATE _vtkSHYXMeshChecker_tbb TBB::tbb)
endif ()
