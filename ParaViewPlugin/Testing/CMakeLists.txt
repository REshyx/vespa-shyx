if (TARGET ParaView::paraview)
  # This tests shows how to use either
  # inline compares and baseline compares
  set(DATA_ROOT ${CMAKE_SOURCE_DIR}/Data)
  set (TestAlphaWrapping_USES_DIRECT_DATA ON) 
  paraview_add_client_tests(
    LOAD_PLUGIN VESPAPlugin
    PLUGIN_PATH $<TARGET_FILE_DIR:VESPAPlugin>
    BASELINE_DIR ${CMAKE_SOURCE_DIR}/Data/Baseline
    TEST_SCRIPTS TestAlphaWrapping.xml)
  set_tests_properties(pv.TestAlphaWrapping PROPERTIES ENVIRONMENT "PARAVIEW_DATA_ROOT=${DATA_ROOT}")
endif()

