set(proj OpenXR)

# Set dependency list
set(${proj}_DEPENDENCIES "")

# Sanity checks
if(DEFINED OpenXR_INCLUDE_DIR AND NOT EXISTS ${OpenXR_INCLUDE_DIR})
  message(FATAL_ERROR "OpenXR_INCLUDE_DIR variable is defined but corresponds to nonexistent directory")
endif()
if(DEFINED OpenXR_LIBRARY AND NOT EXISTS ${OpenXR_LIBRARY})
  message(FATAL_ERROR "OpenXR_LIBRARY variable is defined but corresponds to nonexistent path")
endif()

if(${CMAKE_PROJECT_NAME}_USE_SYSTEM_${proj})
  unset(OpenXR_INCLUDE_DIR CACHE)
  unset(OpenXR_LIBRARY CACHE)
  find_package(OpenXR REQUIRED)
endif()

# Include dependent projects if any
ExternalProject_Include_Dependencies(${proj} PROJECT_VAR proj DEPENDS_VAR ${proj}_DEPENDENCIES)

if((NOT OpenXR_INCLUDE_DIR OR NOT OpenXR_LIBRARY) AND NOT ${CMAKE_PROJECT_NAME}_USE_SYSTEM_${proj})

  # OpenXR
  ExternalProject_SetIfNotDefined(
    ${SUPERBUILD_TOPLEVEL_PROJECT}_${proj}_GIT_REPOSITORY
    https://github.com/KhronosGroup/OpenXR-SDK-Source.git
    QUIET
    )

  ExternalProject_SetIfNotDefined(
    ${SUPERBUILD_TOPLEVEL_PROJECT}_${proj}_GIT_TAG
    "release-1.0.24" # release-1.0.24
    QUIET
    )

  set(${proj}_SOURCE_DIR ${CMAKE_BINARY_DIR}/${proj})
  set(${proj}_INSTALL_DIR ${CMAKE_BINARY_DIR}/${proj}-install)

  ExternalProject_Add(${proj}
    ${${proj}_EP_ARGS}
    GIT_REPOSITORY "${${SUPERBUILD_TOPLEVEL_PROJECT}_${proj}_GIT_REPOSITORY}"
    GIT_TAG "${${SUPERBUILD_TOPLEVEL_PROJECT}_${proj}_GIT_TAG}"
    SOURCE_DIR ${${proj}_SOURCE_DIR}
    BINARY_DIR ${proj}-build
    INSTALL_DIR ${${proj}_INSTALL_DIR}
    CMAKE_CACHE_ARGS
      # Compiler settings
      -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
      -DCMAKE_CXX_FLAGS:STRING=${ep_common_cxx_flags}
      -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
      -DCMAKE_C_FLAGS:STRING=${ep_common_c_flags}
      -DCMAKE_CXX_STANDARD:STRING=${CMAKE_CXX_STANDARD}
      -DCMAKE_CXX_STANDARD_REQUIRED:BOOL=${CMAKE_CXX_STANDARD_REQUIRED}
      -DCMAKE_CXX_EXTENSIONS:BOOL=${CMAKE_CXX_EXTENSIONS}

      # Options
      -DDYNAMIC_LOADER:BOOL=ON
      -DBUILD_TESTS:BOOL=OFF

      # Install directories
      -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
    DEPENDS
      ${${proj}_DEPENDENCIES}
    )

  ExternalProject_GenerateProjectDescription_Step(${proj}
    VERSION ${_version}
    )

  set(OpenXR_DIR ${${proj}_INSTALL_DIR}/cmake)
  set(OpenXR_INCLUDE_DIR "${${proj}_INSTALL_DIR}/include/openxr")
  if(WIN32)
    set(OpenXR_LIBRARY "${${proj}_INSTALL_DIR}/lib/openxr_loader.lib")
  elseif(APPLE)
    set(OpenXR_LIBRARY "${${proj}_INSTALL_DIR}/bin/osx64/OpenXR.framework")
  elseif(UNIX)
    set(OpenXR_LIBRARY "${${proj}_INSTALL_DIR}/bin/libopenxr_loader.so")
  endif()
  mark_as_superbuild(OpenXR_LIBRARY:FILEPATH)

  #-----------------------------------------------------------------------------
  # Launcher setting specific to build tree
  set(${proj}_LIBRARY_PATHS_LAUNCHER_BUILD ${${proj}_INSTALL_DIR}/bin)
  mark_as_superbuild(
    VARS ${proj}_LIBRARY_PATHS_LAUNCHER_BUILD
    LABELS "LIBRARY_PATHS_LAUNCHER_BUILD"
    )

else()
  ExternalProject_Add_Empty(${proj} DEPENDS ${${proj}_DEPENDENCIES})
endif()

ExternalProject_Message(${proj} "OpenXR_INCLUDE_DIR:${OpenXR_INCLUDE_DIR}")
ExternalProject_Message(${proj} "OpenXR_LIBRARY:${OpenXR_LIBRARY}")

mark_as_superbuild(
  VARS
    ${proj}_INCLUDE_DIR:PATH
    ${proj}_LIBRARY:FILEPATH
  )
