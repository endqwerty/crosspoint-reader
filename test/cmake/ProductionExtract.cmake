find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CROSSPOINT_EXTRACT_FUNCTIONS "${CMAKE_CURRENT_LIST_DIR}/extract_functions.py")

# Resolves <file> against the calling directory, checks it exists and adds it to
# CMAKE_CONFIGURE_DEPENDS so edits re-extract. Sets <out_var> empty on failure.
function(_crosspoint_production_source out_var file)
  set(${out_var} "" PARENT_SCOPE)
  get_filename_component(path "${file}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
    message(SEND_ERROR "${CMAKE_CURRENT_SOURCE_DIR}: production extraction from ${path}: file not found")
    return()
  endif()
  get_property(dependencies DIRECTORY PROPERTY CMAKE_CONFIGURE_DEPENDS)
  if(NOT path IN_LIST dependencies)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
  endif()
  set(${out_var} "${path}" PARENT_SCOPE)
endfunction()

# crosspoint_extract_functions(<out_var> <file> <signature>...)
#
# Sets <out_var> to the complete definition of each function named by a
# <signature>, in the order given, separated by blank lines. A signature is the
# exact source text that starts a definition, such as "void Foo::bar(" or
# "int Foo::count() const", and must match exactly one definition. Declarations,
# calls, comments and literals do not count. Each definition runs from the start
# of its signature's line to its closing brace, so edits to neighbouring
# functions do not affect it. Limits are in extract_functions.py.
#
# Any failure is a SEND_ERROR with <out_var> set to empty, so one configure
# reports every moved signature. <file> and the extractor are added to
# CMAKE_CONFIGURE_DEPENDS.
function(crosspoint_extract_functions out_var file)
  set(${out_var} "" PARENT_SCOPE)
  if(ARGC LESS 3)
    message(SEND_ERROR "${CMAKE_CURRENT_SOURCE_DIR}: crosspoint_extract_functions needs at least one signature")
    return()
  endif()
  _crosspoint_production_source(path "${file}")
  if(NOT path)
    return()
  endif()
  get_property(dependencies DIRECTORY PROPERTY CMAKE_CONFIGURE_DEPENDS)
  if(NOT CROSSPOINT_EXTRACT_FUNCTIONS IN_LIST dependencies)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CROSSPOINT_EXTRACT_FUNCTIONS}")
  endif()
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CROSSPOINT_EXTRACT_FUNCTIONS}" "${path}" ${ARGN}
    OUTPUT_VARIABLE result
    ERROR_VARIABLE error
    RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(SEND_ERROR "${CMAKE_CURRENT_SOURCE_DIR}: production extraction from ${error}")
    return()
  endif()
  set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

# crosspoint_extract_between(<out_var> <file> <start_marker> <end_marker>)
#
# For snippets that are not whole functions: a statement range inside a
# function, a declaration or a constant block. Prefer crosspoint_extract_functions
# for whole functions, because an end marker naming the next function breaks
# when that neighbour is renamed or another function is inserted.
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
  _crosspoint_production_source(path "${file}")
  if(NOT path)
    return()
  endif()
  set(where "${CMAKE_CURRENT_SOURCE_DIR}: production extraction from ${path}")
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
