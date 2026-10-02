# crosspoint_extract_between(<out_var> <file> <start_marker> <end_marker>)
#
# Sets <out_var> to the text of <file> from the first byte of <start_marker> up
# to, but not including, the first <end_marker> that follows the start marker.
# An <end_marker> of "EOF" extracts to the end of the file.
#
# The start marker must occur exactly once in the file. Any violation is a
# SEND_ERROR with <out_var> set to empty, so one configure reports every moved
# marker. <file> is added to CMAKE_CONFIGURE_DEPENDS.
function(crosspoint_extract_between out_var file start_marker end_marker)
  set(${out_var} "" PARENT_SCOPE)
  if(NOT ARGC EQUAL 4)
    message(SEND_ERROR "${CMAKE_CURRENT_SOURCE_DIR}: crosspoint_extract_between takes exactly 4 arguments, got ${ARGC}")
    return()
  endif()
  get_filename_component(path "${file}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  set(where "${CMAKE_CURRENT_SOURCE_DIR}: production extraction from ${path}")
  if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
    message(SEND_ERROR "${where}: file not found")
    return()
  endif()
  get_property(dependencies DIRECTORY PROPERTY CMAKE_CONFIGURE_DEPENDS)
  if(NOT path IN_LIST dependencies)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
  endif()
  if(start_marker STREQUAL "")
    message(SEND_ERROR "${where}: empty start marker")
    return()
  endif()

  file(READ "${path}" source)
  string(FIND "${source}" "${start_marker}" start)
  if(start LESS 0)
    message(SEND_ERROR "${where}: start marker not found:\n  ${start_marker}")
    return()
  endif()
  string(LENGTH "${start_marker}" start_length)
  math(EXPR after_start "${start} + ${start_length}")
  string(SUBSTRING "${source}" ${after_start} -1 tail)
  string(FIND "${tail}" "${start_marker}" repeat)
  if(NOT repeat LESS 0)
    message(SEND_ERROR "${where}: start marker occurs more than once:\n  ${start_marker}")
    return()
  endif()

  if(end_marker STREQUAL "EOF")
    string(SUBSTRING "${source}" ${start} -1 result)
  else()
    string(FIND "${tail}" "${end_marker}" end)
    if(end LESS 0)
      message(SEND_ERROR
        "${where}: end marker not found after the start marker:\n  start: ${start_marker}\n  end:   ${end_marker}")
      return()
    endif()
    math(EXPR length "${start_length} + ${end}")
    string(SUBSTRING "${source}" ${start} ${length} result)
  endif()
  set(${out_var} "${result}" PARENT_SCOPE)
endfunction()
