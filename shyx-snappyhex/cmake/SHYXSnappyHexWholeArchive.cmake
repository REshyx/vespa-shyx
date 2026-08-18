# MSVC/lld-link drops RTS-only members of SHYXSnappyHex.lib unless the
# whole archive is pulled. Do not WHOLEARCHIVE OpenFOAM.lib / finiteVolume.lib.
function(shyx_snappyhex_whole_archive tgt)
  if(NOT TARGET "${tgt}")
    message(FATAL_ERROR "shyx_snappyhex_whole_archive: unknown target ${tgt}")
  endif()
  if(NOT TARGET SHYXSnappyHex::SHYXSnappyHex)
    message(FATAL_ERROR "SHYXSnappyHex::SHYXSnappyHex is not available")
  endif()
  if(MSVC)
    target_link_options("${tgt}" PRIVATE
      "/WHOLEARCHIVE:$<TARGET_FILE:SHYXSnappyHex::SHYXSnappyHex>"
      "/OPT:NOICF")
  endif()
endfunction()
