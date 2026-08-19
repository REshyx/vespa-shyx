set(classes
  vtkSHYXSelectionAppendPatches)
vtk_module_add_module(vtkSHYXSelectionAppendPatches
  FORCE_STATIC
  CLASSES ${classes}
)

if(MSVC)
  target_compile_options(vtkSHYXSelectionAppendPatches PRIVATE /utf-8)
endif()

vespa_plugin_xml(SHYXSelectionAppendPatches.xml)
