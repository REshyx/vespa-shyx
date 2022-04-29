if (TARGET VTK::vtkpython)
  # import
  add_test(NAME "import_Delaunay"
    COMMAND
      "$<TARGET_FILE:VTK::vtkpython>"
      "${CMAKE_CURRENT_LIST_DIR}/import_Delaunay.py")
  set_property(TEST "import_Delaunay" APPEND
    PROPERTY
      ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/${python_destination}")
  if (WIN32)
    set(test_path "$ENV{PATH};${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
    string(REPLACE ";" "\;" test_path "${test_path}")
    set_property(TEST "import_Delaunay" APPEND
      PROPERTY
        ENVIRONMENT "PATH=${test_path}")
  endif ()

  # execute
  add_test(NAME "execute_Delaunay"
    COMMAND
      "$<TARGET_FILE:VTK::vtkpython>"
      "${CMAKE_CURRENT_LIST_DIR}/execute_Delaunay.py")
  set_property(TEST "execute_Delaunay" APPEND
    PROPERTY
      ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/${python_destination}")
  if (WIN32)
    set(test_path "$ENV{PATH};${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
    string(REPLACE ";" "\;" test_path "${test_path}")
    set_property(TEST "execute_Delaunay" APPEND
      PROPERTY
        ENVIRONMENT "PATH=${test_path}")
  endif ()
endif ()

vtk_add_test_cxx(vtkCGALDelaunayCxxTests no_data_tests
  NO_DATA NO_VALID NO_OUTPUT
  TestDelaunayExecution.cxx

  ${PROJECT_SOURCE_DIR}/Data/Testing/
)
vtk_test_cxx_executable(vtkCGALDelaunayCxxTests no_data_tests)
