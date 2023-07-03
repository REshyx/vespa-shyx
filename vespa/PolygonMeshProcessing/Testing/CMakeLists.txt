if (TARGET VTK::vtkpython)
  # these tests are VTK only for now

  # import
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

  # execute
  add_test(NAME "execute_IsotropicRemesher"
    COMMAND
      "$<TARGET_FILE:VTK::vtkpython>"
      "${CMAKE_CURRENT_LIST_DIR}/execute_IsotropicRemesher.py")
  set_property(TEST "execute_IsotropicRemesher" APPEND
    PROPERTY
      ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/${python_destination}")
  if (WIN32)
    set(test_path "$ENV{PATH};${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
    string(REPLACE ";" "\;" test_path "${test_path}")
    set_property(TEST "execute_IsotropicRemesher" APPEND
      PROPERTY
        ENVIRONMENT "PATH=${test_path}")
  endif ()
endif ()

vtk_add_test_cxx(vtkCGALPMPCxxTests no_data_tests
  NO_DATA NO_VALID NO_OUTPUT
  TestPMPInstance.cxx
  TestPMPBooleanExecution.cxx
  TestPMPDeformExecution.cxx
  TestPMPFairExecution.cxx
  TestPMPFillExecution.cxx
  TestPMPIsotropicExecution.cxx
  TestPMPMeshCheckerExecution.cxx
  TestPMPMeshSubdivisionExecution.cxx
  TestPMPSmoothingExecution.cxx

  ${PROJECT_SOURCE_DIR}/Data/Testing/
)

if (${CERES_FOUND} )
  vtk_add_test_cxx(vtkCGALPMPCxxTests no_data_tests
    NO_DATA NO_VALID NO_OUTPUT
    TestPMPMeshSmoothingExecution.cxx
  )
endif()

vtk_test_cxx_executable(vtkCGALPMPCxxTests no_data_tests)

if (${CGAL_VERSION} VERSION_GREATER 5.5)
  vtk_add_test_cxx(vtkCGALPMPCxxTests55 no_data_tests
    NO_DATA NO_VALID NO_OUTPUT
    TestPMPAlphaWrapping.cxx

  ${PROJECT_SOURCE_DIR}/Data/Testing/
  )
  vtk_test_cxx_executable(vtkCGALPMPCxxTests55 no_data_tests)
endif()
