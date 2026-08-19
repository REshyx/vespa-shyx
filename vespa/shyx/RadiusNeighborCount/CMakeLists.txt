set(classes
  vtkSHYXRadiusNeighborCount)

vtk_module_add_module(vtkSHYXRadiusNeighborCount
  FORCE_STATIC
  CLASSES ${classes})

vespa_plugin_xml(SHYXRadiusNeighborCount.xml)
