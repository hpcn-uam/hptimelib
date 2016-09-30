#################################
# HPTL CONFIG
#################################

option(HPTL_DEBUG "Enable HPTL debug text" ON)
#set   (HPTL_DEBUG "https" CACHE STRING "The server api URI-protocol")

#check RDTSC (only Intel)
if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64") 
  option(HPTL_TSC "Enable TSC" ON)
endif()

include(CheckLibraryExists)
CHECK_LIBRARY_EXISTS(rt clock_gettime "time.h" HPTL_CLOCKREALTIME)

#################################
# CONFIG FILES
#################################
#Config file
#configure_file (
#  "${PROJECT_SOURCE_DIR}/include/config.hpp.in"
#  "${PROJECT_BINARY_DIR}/include/config.hpp"
#  )
configure_file (
  "${PROJECT_SOURCE_DIR}/include/config.h.in"
  "${PROJECT_SOURCE_DIR}/include/config.h"
  )
