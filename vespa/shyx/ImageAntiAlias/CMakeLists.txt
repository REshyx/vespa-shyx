set(classes
  vtkSHYXImageAntiAlias)
vtk_module_add_module(vtkSHYXImageAntiAlias
  FORCE_STATIC
  CLASSES ${classes})

set(_vmtk_root "${VMTK_DIR}")
if(NOT EXISTS "${_vmtk_root}/include/vmtk/vtkvmtkITKFilterUtilities.h")
  message(FATAL_ERROR
    "VMTK ITK utilities not found at \"${_vmtk_root}/include/vmtk/vtkvmtkITKFilterUtilities.h\". "
    "SHYX Image AntiAlias uses the ITK that ships with VMTK. Set VMTK_DIR to your VMTK install root.")
endif()

file(GLOB _vespa_itk_configs "${_vmtk_root}/lib/cmake/ITK-*/ITKConfig.cmake")
if(NOT _vespa_itk_configs)
  message(FATAL_ERROR
    "ITKConfig.cmake not found under \"${_vmtk_root}/lib/cmake/ITK-*\". "
    "Image AntiAlias needs the ITK installed with VMTK (same prefix as VMTK_DIR).")
endif()
list(GET _vespa_itk_configs 0 _vespa_itk_config)
get_filename_component(_vespa_itk_dir "${_vespa_itk_config}" DIRECTORY)

# Same ITK as VMTK, not a second install. PATHS/NO_DEFAULT_PATH is not enough if
# a stray ITK_DIR is already in the CMake cache.
if(ITK_DIR AND NOT ITK_DIR STREQUAL "${_vespa_itk_dir}")
  message(STATUS "VESPA: Image AntiAlias uses VMTK ITK (${_vespa_itk_dir}), not ITK_DIR=${ITK_DIR}")
endif()
set(ITK_DIR "${_vespa_itk_dir}" CACHE PATH "ITK from VMTK_DIR (SHYX Image AntiAlias)" FORCE)
find_package(ITK REQUIRED COMPONENTS ITKAntiAlias)

target_include_directories(vtkSHYXImageAntiAlias
  PRIVATE
    "${_vmtk_root}/include/vmtk"
    ${ITK_INCLUDE_DIRS})
target_link_libraries(vtkSHYXImageAntiAlias PRIVATE ${ITK_LIBRARIES})

if(ITK_REQUIRED_CXX_FLAGS)
  separate_arguments(_vespa_itk_cxx_flags NATIVE_COMMAND "${ITK_REQUIRED_CXX_FLAGS}")
  target_compile_options(vtkSHYXImageAntiAlias PRIVATE ${_vespa_itk_cxx_flags})
endif()

if(MSVC)
  target_compile_options(vtkSHYXImageAntiAlias PRIVATE /utf-8)
endif()

message(STATUS "VESPA: SHYX Image AntiAlias using ITK ${ITK_VERSION} from ${_vespa_itk_dir}")

vespa_plugin_xml(SHYXImageAntiAlias.xml)
