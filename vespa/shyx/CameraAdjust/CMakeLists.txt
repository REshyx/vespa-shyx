set(classes
  vtkSHYXCameraAdjust)
vtk_module_add_module(vtkSHYXCameraAdjust
  FORCE_STATIC
  CLASSES ${classes}
)

if(MSVC)
  target_compile_options(vtkSHYXCameraAdjust PRIVATE /utf-8)
endif()

vespa_plugin_xml(SHYXCameraAdjust.xml)
