set(classes
  vtkSHYXPartitionedCollectionToOpenFOAM)
vtk_module_add_module(vtkSHYXPartitionedCollectionToOpenFOAM
  FORCE_STATIC
  CLASSES ${classes}
)

if(MSVC)
  target_compile_options(vtkSHYXPartitionedCollectionToOpenFOAM PRIVATE /utf-8)
endif()

vespa_plugin_xml(SHYXPartitionedCollectionToOpenFOAM.xml)
