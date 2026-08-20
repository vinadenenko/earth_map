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
        earth_map/0.1.0@utils/stable
        OPTIONS
            # protobuf/*:fPIC=True
        GENERATORS
            CMakeDeps
            CMakeToolchain
    )

    set(PROFILE_BUILD_ ${PROJECT_SOURCE_DIR}/conan_profiles/default-debug)
    set(PROFILE_HOST_ ${PROFILE_BUILD_})

    if (ANDROID)
        set(PROFILE_HOST_ ${PROJECT_SOURCE_DIR}/conan_profiles/armv8-debug)
    endif()

    conan_cmake_install(PATH_OR_REFERENCE ${CMAKE_BINARY_DIR}
                        OUTPUT_FOLDER ${CMAKE_BINARY_DIR}/conan
                        BUILD missing
                        PROFILE_BUILD ${PROFILE_BUILD_}
                        PROFILE_HOST ${PROFILE_HOST_})
endfunction()
