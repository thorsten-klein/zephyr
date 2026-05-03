# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(getopt)

if(CONFIG_TC_PROVIDES_POSIX_C_LIB_EXT)
  return()
endif()

zephyr_library()
if(NOT CONFIG_NATIVE_LIBC)
  zephyr_library_sources(
    fnmatch.c
    getentropy.c
  )
endif()
zephyr_library_sources(
  getopt_shim.c
)
