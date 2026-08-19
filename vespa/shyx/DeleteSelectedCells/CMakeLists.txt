set(classes
  vtkSHYXDeleteSelectedCellsFilter)
vtk_module_add_module(vtkSHYXDeleteSelectedCellsFilter
  FORCE_STATIC
  CLASSES ${classes}
)

vespa_plugin_xml(SHYXDeleteSelectedCells.xml)
