# Match the reader-owned parser and flags, rather than a host-installed Expat.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${REPO_ROOT}/platformio.ini)
file(READ ${REPO_ROOT}/platformio.ini firmware_config)
foreach(flag XML_GE XML_CONTEXT_BYTES)
  string(REGEX MATCHALL "-D${flag}=[0-9]+" definitions "${firmware_config}")
  list(LENGTH definitions count)
  if(NOT count EQUAL 1)
    message(FATAL_ERROR "Expected one shared ${flag} definition in platformio.ini")
  endif()
  string(REGEX REPLACE "-D${flag}=" "" ${flag} "${definitions}")
endforeach()
function(add_firmware_expat target context_bytes)
  add_library(${target} STATIC
    ${REPO_ROOT}/lib/expat/xmlparse.c
    ${REPO_ROOT}/lib/expat/xmlrole.c
    ${REPO_ROOT}/lib/expat/xmltok.c)
  target_include_directories(${target} PUBLIC ${REPO_ROOT}/lib/expat)
  target_compile_definitions(${target} PUBLIC XML_GE=${XML_GE} XML_CONTEXT_BYTES=${context_bytes})
endfunction()
add_firmware_expat(FirmwareExpat ${XML_CONTEXT_BYTES})
