set(classes
  vtkSHYXExtractSelectedCellsFilter)
vtk_module_add_module(vtkSHYXExtractSelectedCellsFilter
  FORCE_STATIC
  CLASSES ${classes}
)

vespa_plugin_xml(SHYXExtractSelectedCells.xml)
