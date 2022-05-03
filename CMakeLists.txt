cmake_minimum_required(VERSION 3.8)
project (vespa)

## Config

set(CMAKE_CXX_STANDARD 11)

include(GNUInstallDirs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")

option(BUILD_SHARED_LIBS "Build shared library" ON)

include(CTest)

## Deps

find_package(VTK
  COMPONENTS
    CommonCore
    CommonDataModel
    CommonExecutionModel
    FiltersCore
    FiltersExtraction
    FiltersGeometry
    Python
  OPTIONAL_COMPONENTS
    CommonSystem
    IOXML
    TestingCore)

find_package(CGAL REQUIRED)

find_package(Eigen3 REQUIRED 3.2.0)
if (CGAL_VERSION VERSION_GREATER "5.1")
  # this file only exists in CGAL 5.1 and later
  include(CGAL_Eigen3_support)
else()
  include_directories(${EIGEN3_INCLUDE_DIR})
endif()

## VTK Module
option(FORCE_STATIC_MODULES "Build VTK modules statically" OFF)
if (FORCE_STATIC_MODULES)
  set(FORCE_STATIC_MODULES_STRING FORCE_STATIC)
else ()
  set(FORCE_STATIC_MODULES_STRING)
endif()

vtk_module_find_modules(vtkcgal_module_files "${CMAKE_CURRENT_SOURCE_DIR}/vespa")

vtk_module_scan(
  MODULE_FILES     ${vtkcgal_module_files}
  PROVIDES_MODULES vtkcgal_provided_modules
  WANT_BY_DEFAULT  ON
  ENABLE_TESTS     ON)

vtk_module_python_default_destination(python_destination)

vtk_module_build(
  MODULES         ${vtkcgal_provided_modules}
  INSTALL_EXPORT  vespa
  INSTALL_HEADERS ON
  CMAKE_DESTINATION   "${CMAKE_INSTALL_LIBDIR}/cmake/vespa"
  HEADERS_DESTINATION "include")


## Install

set(vespaExport ${vtkcgal_provided_modules})
export(
  TARGETS   ${vespaExport}
  NAMESPACE VTK::
  FILE "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/vespa/vespa-targets.cmake")
install(
  EXPORT      vespa
  NAMESPACE   VTK::
  FILE        vespa-targets.cmake
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vespa")


## Python wrapping

option(BUILD_PYTHON_WRAPPERS "Build python wrapping" ON)

if(BUILD_PYTHON_WRAPPERS)

  vtk_module_wrap_python(
    MODULES         ${vtkcgal_provided_modules}
    WRAPPED_MODULES vtkcgal_wrapped_modules
    TARGET          VESPA::Python
    INSTALL_EXPORT  vespaPython
    PYTHON_PACKAGE  "vespa"
    MODULE_DESTINATION  "${python_destination}"
    CMAKE_DESTINATION   "${CMAKE_INSTALL_LIBDIR}/cmake/vespaPython"
    LIBRARY_DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INSTALL_HEADERS ON
    BUILD_STATIC    OFF)

  # Generate __init__.py
  set(python_modules)
  foreach(module ${vtkcgal_wrapped_modules})
    list(APPEND python_modules "'${module}'")
  endforeach()

  set(InitContent "__all__ = ${python_modules}")

  file(GENERATE
    OUTPUT  "${CMAKE_BINARY_DIR}/${python_destination}/vespa/__init__.py"
    CONTENT "${InitContent}")
  install(
    FILES       "${CMAKE_BINARY_DIR}/${python_destination}/vespa/__init__.py"
    DESTINATION "${python_destination}/vespa/")

  # Install vespaPython
  export(
    EXPORT    vespaPython
    NAMESPACE VESPA::
    FILE "${CMAKE_BINARY_DIR}/${python_destination}/vespaPython/vespaPython-targets.cmake")
  install(
    EXPORT    vespaPython
    NAMESPACE VESPA::
    FILE      vespaPython-targets.cmake
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vespaPython")

endif()

