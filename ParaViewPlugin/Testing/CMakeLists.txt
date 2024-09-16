if (TARGET ParaView::paraview)

  # Support testing with different ParaView versions
  set(_test_surface_normals "SurfaceNormals")
  if(ParaView_VERSION VERSION_LESS 5.13.0)
    set(_test_surface_normals "PolyDataNormals")
  endif()
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/TestAlphaWrapping.xml.in"
    "${CMAKE_CURRENT_BINARY_DIR}/TestAlphaWrapping.xml")

  # Add a simple alpha wrapping test
  set(DATA_ROOT ${CMAKE_SOURCE_DIR}/Data)
  set (TestAlphaWrapping_USES_DIRECT_DATA ON) 
  paraview_add_client_tests(
    LOAD_PLUGIN VESPAPlugin
    PLUGIN_PATH $<TARGET_FILE_DIR:VESPAPlugin>
    BASELINE_DIR ${CMAKE_SOURCE_DIR}/Data/Baseline
    TEST_SCRIPTS ${CMAKE_CURRENT_BINARY_DIR}/TestAlphaWrapping.xml)
  set_tests_properties(pv.TestAlphaWrapping PROPERTIES ENVIRONMENT "PARAVIEW_DATA_ROOT=${DATA_ROOT}")
endif()

