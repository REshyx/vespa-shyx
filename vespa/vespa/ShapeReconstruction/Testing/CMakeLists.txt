
vtk_add_test_cxx(vtkCGALSRCxxTests no_data_tests
  NO_DATA NO_VALID NO_OUTPUT
  TestSRAdvancingFrontSurfaceReconstruction.cxx
  TestSRPoissonSurfaceReconstructionDelaunay.cxx

  ${PROJECT_SOURCE_DIR}/Data/Testing/
)

vtk_test_cxx_executable(vtkCGALSRCxxTests no_data_tests)
