# Parse OpenFOAM Make/files and create a STATIC library with a flat lnInclude.

function(foam_quote_include target dir)
  if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # -I would put string.H / List.H / Vector.H on the angle-include path.
    # -iquote keeps #include <string> on the CRT and "string.H" on Foam.
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
      target_compile_options(${target} PUBLIC "/clang:-iquote${dir}")
    else()
      target_compile_options(${target} PUBLIC "SHELL:-iquote ${dir}")
    endif()
  else()
    target_include_directories(${target} PUBLIC "$<BUILD_INTERFACE:${dir}>")
  endif()
endfunction()

function(foam_parse_make_files make_files src_dir out_srcs)
  if(NOT EXISTS "${make_files}")
    message(FATAL_ERROR "Make/files not found: ${make_files}")
  endif()
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(_out "${CMAKE_CURRENT_BINARY_DIR}/foam_srcs")
  file(MAKE_DIRECTORY "${_out}")
  string(SHA1 _key "${make_files}")
  set(_out "${_out}/${_key}.cmake")
  set(_copy "")
  if(WIN32)
    set(_copy "${CMAKE_BINARY_DIR}/ofsrc/${_key}")
  endif()
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/parse_make_files.py"
            "${make_files}" "${src_dir}" "${_out}" "${_copy}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _msg
    ERROR_VARIABLE _emsg)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "parse_make_files.py failed: ${_emsg} ${_msg}")
  endif()
  include("${_out}")
  set(${out_srcs} "${_foam_srcs}" PARENT_SCOPE)
  set(_foam_src_dirs "${_foam_src_dirs}" PARENT_SCOPE)
endfunction()

function(foam_make_lninclude src_dir ln_dir)
  file(MAKE_DIRECTORY "${ln_dir}")
  if(WIN32)
    execute_process(
      COMMAND fsutil.exe file setCaseSensitiveInfo "${ln_dir}" enable
      OUTPUT_QUIET ERROR_QUIET)
  endif()
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    execute_process(
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/make_lninclude.py"
              "${src_dir}" "${ln_dir}"
      RESULT_VARIABLE _ln_rc)
    if(NOT _ln_rc EQUAL 0)
      message(FATAL_ERROR "make_lninclude.py failed for ${src_dir}")
    endif()
  else()
    file(GLOB_RECURSE _hdrs
      "${src_dir}/*.H" "${src_dir}/*.h" "${src_dir}/*.hxx" "${src_dir}/*.hpp"
      "${src_dir}/*.I" "${src_dir}/*.txx" "${src_dir}/*.C")
    foreach(_h IN LISTS _hdrs)
      get_filename_component(_name "${_h}" NAME)
      if(NOT EXISTS "${ln_dir}/${_name}")
        file(COPY "${_h}" DESTINATION "${ln_dir}")
      endif()
    endforeach()
  endif()
endfunction()

function(foam_add_static_library target src_dir)
  cmake_parse_arguments(ARG "" "" "DEPS" ${ARGN})
  set(_make "${src_dir}/Make/files")
  foam_parse_make_files("${_make}" "${src_dir}" _srcs)
  if(NOT _srcs)
    message(FATAL_ERROR "No sources for ${target} in ${_make}")
  endif()
  set(_ln "${FOAM_LNINCLUDE_ROOT}/${target}")
  foam_make_lninclude("${src_dir}" "${_ln}")

  add_library(${target} STATIC ${_srcs})
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME "${target}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  )
  set_source_files_properties(${_srcs} PROPERTIES LANGUAGE CXX)
  if(_foam_src_dirs)
    foreach(_s _d IN ZIP_LISTS _srcs _foam_src_dirs)
      set_source_files_properties("${_s}" PROPERTIES INCLUDE_DIRECTORIES "${_d}")
    endforeach()
  endif()

  foam_quote_include(${target} "${_ln}")
  target_include_directories(${target} PRIVATE "${src_dir}")
  target_link_libraries(${target} PUBLIC ${ARG_DEPS})
  target_compile_features(${target} PUBLIC cxx_std_17)

  list(LENGTH _srcs _n)
  message(STATUS "Foam STATIC ${target}: ${_n} sources")
endfunction()

# Overlay adapter TUs into a Foam lnInclude so globals.C #include "foo.C"
# picks them up. Must run after foam_add_static_library (which copies the
# pristine tree first). Do not also compile these as extra TUs.
function(foam_overlay_lninclude ln_dir src dest_name)
  if(NOT EXISTS "${src}")
    message(FATAL_ERROR "adapter overlay missing: ${src}")
  endif()
  configure_file("${src}" "${ln_dir}/${dest_name}" COPYONLY)
endfunction()
