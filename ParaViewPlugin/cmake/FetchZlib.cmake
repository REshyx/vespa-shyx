# Static zlib for the boundary-assignment zip packer.
# Do not link VTK::zlib: a ParaView built with the bundled zlib imports
# vtkzlib-pv6.0.dll, which official ParaView 6.0.x does not ship (it has zlib1.dll).
# This copy is compiled without Z_PREFIX, so its symbols are deflate/crc32 and do not
# bind to vtkzlib's z_deflate exports even if that import lib is on the link line.

include(FetchContent)
FetchContent_Declare(vespa_zlib
  URL https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz
  URL_HASH SHA256=17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(vespa_zlib)
if (NOT vespa_zlib_POPULATED)
  FetchContent_Populate(vespa_zlib)
endif ()

add_library(vespa_zlib STATIC
  "${vespa_zlib_SOURCE_DIR}/adler32.c"
  "${vespa_zlib_SOURCE_DIR}/crc32.c"
  "${vespa_zlib_SOURCE_DIR}/deflate.c"
  "${vespa_zlib_SOURCE_DIR}/trees.c"
  "${vespa_zlib_SOURCE_DIR}/zutil.c")
target_include_directories(vespa_zlib PUBLIC "${vespa_zlib_SOURCE_DIR}")
if (MSVC)
  target_compile_definitions(vespa_zlib PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(vespa_zlib PRIVATE /W0)
endif ()
set_target_properties(vespa_zlib PROPERTIES
  EXCLUDE_FROM_ALL TRUE
  FOLDER "ThirdParty")
