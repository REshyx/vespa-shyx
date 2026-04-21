set(classes
  vtkSHYXVmtkPolyDataCenterlines)

vtk_module_add_module(vtkSHYXVmtkPolyDataCenterlines
  FORCE_STATIC
  CLASSES ${classes})

set(_vmtk_root "${VMTK_DIR}")
if(NOT EXISTS "${_vmtk_root}/include/vmtk/vtkvmtkPolyDataCenterlines.h")
  message(FATAL_ERROR
    "VMTK headers not found at \"${_vmtk_root}/include/vmtk\". Set VMTK_DIR to your VMTK install root.")
endif()

target_include_directories(vtkSHYXVmtkPolyDataCenterlines
  PRIVATE
    "${_vmtk_root}/include/vmtk")

set(_vmtk_vtk_libs
  vtkvmtkCommon
  vtkvmtkComputationalGeometry
  vtkvmtkContrib
  vtkvmtkDifferentialGeometry
  vtkvmtkIO
  vtkvmtkMisc
  vtkvmtkSegmentation
  vtkvmtkITK)

foreach(_vmtk_lib IN LISTS _vmtk_vtk_libs)
  find_library(_vespa_vmtk_lib_${_vmtk_lib} NAMES "${_vmtk_lib}"
    PATHS "${_vmtk_root}/lib" NO_DEFAULT_PATH REQUIRED)
  target_link_libraries(vtkSHYXVmtkPolyDataCenterlines PRIVATE "${_vespa_vmtk_lib_${_vmtk_lib}}")
endforeach()

find_library(_vespa_vmtk_nl NAMES nl PATHS "${_vmtk_root}/lib" NO_DEFAULT_PATH REQUIRED)
target_link_libraries(vtkSHYXVmtkPolyDataCenterlines PRIVATE "${_vespa_vmtk_nl}")

if(MSVC)
  target_compile_options(vtkSHYXVmtkPolyDataCenterlines PRIVATE /utf-8)
endif()
