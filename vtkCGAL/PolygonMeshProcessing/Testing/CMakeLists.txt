if (TARGET VTK::vtkpython)
  add_test(NAME "import_vtkCGALPMP"
    COMMAND
      "$<TARGET_FILE:VTK::vtkpython>"
      "${CMAKE_CURRENT_LIST_DIR}/import_vtkCGALPMP.py")
  set_property(TEST "import_vtkCGALPMP" APPEND
    PROPERTY
      ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/${python_destination}")
  if (WIN32)
    set(test_path "$ENV{PATH};${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
    string(REPLACE ";" "\;" test_path "${test_path}")
    set_property(TEST "import_vtkCGALPMP" APPEND
      PROPERTY
        ENVIRONMENT "PATH=${test_path}")
  endif ()
endif ()

vtk_add_test_cxx(vtkCGALCxxTests no_data_tests
  NO_DATA NO_VALID NO_OUTPUT
  TestPMPInstance.cxx
)
vtk_test_cxx_executable(vtkCGALCxxTests no_data_tests)
