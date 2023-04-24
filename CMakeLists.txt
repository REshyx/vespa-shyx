cmake_minimum_required(VERSION 3.8)
project(vespa VERSION 0.1.0)

## Config

set(CMAKE_CXX_STANDARD 11)

include(GNUInstallDirs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")

option(BUILD_SHARED_LIBS "Build shared library" ON)
set(VESPA_BUILD_PV_PLUGIN OFF CACHE BOOL "Build VESPA ParaView plugin")

include(CTest)

## Deps

find_package(CGAL REQUIRED)

find_package(Eigen3 REQUIRED 3.2.0)
if (CGAL_VERSION VERSION_GREATER "5.1")
  # this file only exists in CGAL 5.1 and later
  message(STATUS "include(CGAL_Eigen3_support)")
  include(CGAL_Eigen3_support)
else()
  include_directories(${EIGEN3_INCLUDE_DIR})
endif()

## VTK Module
## ----------

if (VESPA_BUILD_PV_PLUGIN)
  if(VTK_DIR AND NOT ParaView_DIR)
    # old VTK_DIR
    unset(VTK_DIR CACHE)
  endif()
  find_package(ParaView REQUIRED)
else()
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
endif()

option(FORCE_STATIC_MODULES "Build VTK modules statically" OFF)
mark_as_advanced(FORCE_STATIC_MODULES)
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

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/CMake/vespa.cmake.in
    ${PROJECT_BINARY_DIR}/vespa-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vespa
)

write_basic_package_version_file(
    ${PROJECT_BINARY_DIR}/vespa-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(
  FILES       ${CMAKE_BINARY_DIR}/vespa-config.cmake
              ${CMAKE_BINARY_DIR}/vespa-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vespa
  COMPONENT   development)

## ParaView plugin
## ---------------

if (VESPA_BUILD_PV_PLUGIN)

  # disable unsupported python wrapping
  if (NOT TARGET VTK::PythonUsed)
    message(STATUS "ParaView has python disabled, no python wrapping available.")
    set(BUILD_PYTHON_WRAPPERS OFF CACHE BOOL "Python wrapping (disabled)" FORCE)
  endif()

  set("_paraview_plugin_default_${CMAKE_PROJECT_NAME}" ON)
  paraview_plugin_scan(
    PLUGIN_FILES      "${CMAKE_SOURCE_DIR}/ParaViewPlugin/paraview.plugin"
    ENABLE_BY_DEFAULT ON
    PROVIDES_PLUGINS  vespa_plugins
    REQUIRES_MODULES  vespa_required_modules)

  foreach (module IN LISTS vespa_required_modules)
    if (NOT TARGET "${module}")
      message("${CMAKE_PROJECT_NAME}: missing required module ${module}")
      return ()
    endif ()
  endforeach ()

  paraview_plugin_build(
    TARGET VESPAPVPLUGIN
    PLUGINS ${vespa_plugins}
    INSTALL_HEADERS OFF
    RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY_DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY_SUBDIRECTORY "${PARAVIEW_PLUGIN_SUBDIR}"
  )

endif()


## Python wrapping
## ---------------

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
