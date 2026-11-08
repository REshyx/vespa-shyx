# Moves a build product out of the way when a running application still has it
# loaded. Used as a PRE_LINK step for VESPAPlugin: a ParaView instance that has
# the plugin loaded keeps VESPAPlugin.dll mapped, so the linker reports
#   LINK : fatal error LNK1104: cannot open file '...VESPAPlugin.dll'
# even though the C++ sources compiled fine.
#
# Windows lets a mapped image be renamed but not overwritten, so the file is
# renamed (never copied) and the linker then creates a fresh one. The running
# application keeps using the renamed copy until it is restarted.
#
# Inputs: -DOUTPUT_FILE=<path to the built DLL>

if (NOT DEFINED OUTPUT_FILE OR OUTPUT_FILE STREQUAL "")
  message(FATAL_ERROR "ReleaseLockedOutput.cmake: OUTPUT_FILE is required.")
endif ()

if (NOT EXISTS "${OUTPUT_FILE}")
  return()
endif ()

# Succeeds (silently) when nothing has the file open. CMake's file(REMOVE) does
# not report failures, which is exactly what we want here.
file(REMOVE "${OUTPUT_FILE}")
if (NOT EXISTS "${OUTPUT_FILE}")
  return()
endif ()

string(TIMESTAMP _stamp "%H%M%S")
set(_moved "${OUTPUT_FILE}.old-running-${_stamp}")
file(RENAME "${OUTPUT_FILE}" "${_moved}")
if (EXISTS "${OUTPUT_FILE}")
  message(FATAL_ERROR
    "ReleaseLockedOutput.cmake: ${OUTPUT_FILE} is locked and could not be renamed. "
    "Close the application that has the plugin loaded and build again.")
endif ()
message(STATUS "VESPAPlugin: a running process had ${OUTPUT_FILE} open; "
  "moved it to ${_moved}. Restart ParaView to load the new build.")
