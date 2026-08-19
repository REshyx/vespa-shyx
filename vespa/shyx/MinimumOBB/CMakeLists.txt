set(classes
  vtkSHYXMinimumOBBFilter)
vtk_module_add_module(vtkSHYXMinimumOBBFilter
  FORCE_STATIC
  CLASSES ${classes}
)

# CGAL::oriented_bounding_box (Optimal_bounding_box) requires Eigen.
vtk_module_link(vtkSHYXMinimumOBBFilter PRIVATE CGAL::Eigen3_support)

vespa_plugin_xml(SHYXMinimumOBB.xml)
