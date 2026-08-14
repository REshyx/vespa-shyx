
vtk_add_test_cxx(vtkCGALPSPCxxTests no_data_tests
  NO_DATA NO_VALID NO_OUTPUT
  TestPSPXYZReader.cxx
  TestPSPPCAEstimateNormals.cxx

  ${PROJECT_SOURCE_DIR}/Data/Testing/
)

vtk_test_cxx_executable(vtkCGALPSPCxxTests no_data_tests)
