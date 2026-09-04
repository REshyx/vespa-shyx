set(classes
  vtkSHYXAutoMeshRepair)
vtk_module_add_module(vtkSHYXAutoMeshRepair
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXAutoMeshRepair PRIVATE CGAL::Eigen3_support)

vespa_plugin_xml(SHYXAutoMeshRepair.xml)
