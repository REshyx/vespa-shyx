set(classes
  vtkSHYXShapeSmoothing)
vtk_module_add_module(vtkSHYXShapeSmoothing
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXShapeSmoothing PRIVATE CGAL::Eigen3_support)

# Optional: enable area smoothing inside CGAL::angle_and_area_smoothing by linking Ceres
# (CGAL gates this with CGAL_PMP_USE_CERES_SOLVER). The CGAL header degrades gracefully
# without Ceres (angle smoothing still works, area smoothing is skipped at runtime).
if (TARGET Ceres::ceres)
  target_compile_definitions(vtkSHYXShapeSmoothing PRIVATE CGAL_PMP_USE_CERES_SOLVER)
  vtk_module_link(vtkSHYXShapeSmoothing PRIVATE Ceres::ceres)
endif ()
