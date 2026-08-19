# Collect Server Manager XML from VTK modules for VESPAPlugin.
# Call vespa_plugin_xml() from a module CMakeLists.txt after vtk_module_add_module.
# Relative paths are resolved against CMAKE_CURRENT_SOURCE_DIR (the module directory).
# Modules excluded from vtk_module_scan never run this, so optional backends
# (CGAL / VMTK / Snappy / ParaView-only representations) stay aligned automatically.

function(vespa_plugin_xml)
  if (NOT VESPA_BUILD_PV_PLUGIN)
    return()
  endif()
  set(_abs)
  foreach(_f IN LISTS ARGN)
    if (IS_ABSOLUTE "${_f}")
      list(APPEND _abs "${_f}")
    else()
      list(APPEND _abs "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
    endif()
  endforeach()
  set_property(GLOBAL APPEND PROPERTY VESPA_PLUGIN_SERVER_MANAGER_XML ${_abs})
endfunction()
