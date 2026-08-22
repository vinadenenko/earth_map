function(handle_conan_deps)
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan/conan.cmake")
      message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
      file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/0.18.1/conan.cmake"
                    "${CMAKE_BINARY_DIR}/conan/conan.cmake"
                    TLS_VERIFY ON)
    endif()

    include(${CMAKE_BINARY_DIR}/conan/conan.cmake)

    conan_cmake_configure(
        REQUIRES
        earth_map/0.1.0
        OPTIONS
            earth_map/*:enable_performance_monitoring=True
        GENERATORS
            CMakeDeps
            CMakeToolchain
    )

    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        set(PROFILE_BUILD_ ${PROJECT_SOURCE_DIR}/conan_profiles/default-release)
    else()
        set(PROFILE_BUILD_ ${PROJECT_SOURCE_DIR}/conan_profiles/default-debug)
    endif()
    set(PROFILE_HOST_ ${PROFILE_BUILD_})

    if (ANDROID)
        if (CMAKE_BUILD_TYPE STREQUAL "Release")
            set(PROFILE_HOST_ ${PROJECT_SOURCE_DIR}/conan_profiles/armv8-release)
        else()
            set(PROFILE_HOST_ ${PROJECT_SOURCE_DIR}/conan_profiles/armv8-debug)
        endif()
    endif()

    conan_cmake_install(PATH_OR_REFERENCE ${CMAKE_BINARY_DIR}
                        OUTPUT_FOLDER ${CMAKE_BINARY_DIR}/conan
                        BUILD missing
                        PROFILE_BUILD ${PROFILE_BUILD_}
                        PROFILE_HOST ${PROFILE_HOST_})
endfunction()
