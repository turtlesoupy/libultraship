include(FetchContent)

# Emscripten cross-build: no system packages exist, so everything is built
# from source (mirrors android.cmake). GL comes from Emscripten's GLES3 ->
# WebGL2 emulation (-sFULL_ES3 at link), so no GL library is linked here.

#=================== zlib (libzip dependency) ===================
find_package(ZLIB QUIET)
if (NOT ZLIB_FOUND)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        ZLIB
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(ZLIB)
    # zlib's CMake gives the shared and static libs the same libz.a output
    # under some generators — disambiguate so ninja doesn't see two rules.
    if (TARGET zlibstatic)
        set_target_properties(zlibstatic PROPERTIES OUTPUT_NAME zstatic)
    endif()
    if (NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
        target_link_libraries(ZLIB::ZLIB INTERFACE zlibstatic)
        target_include_directories(ZLIB::ZLIB INTERFACE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
    endif()
endif()

#=================== SDL2 ===================
# Built from source: SDL2 has first-class Emscripten support and this keeps
# the SDL2::SDL2 CMake target shape the rest of the build expects.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.32.10
)
FetchContent_MakeAvailable(SDL2)
if (NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 ALIAS SDL2-static)
endif()

#=================== nlohmann-json ===================
find_package(nlohmann_json QUIET)
if (NOT ${nlohmann_json_FOUND})
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

#=================== tinyxml2 ===================
find_package(tinyxml2 QUIET)
if (NOT ${tinyxml2_FOUND})
    set(tinyxml2_BUILD_TESTING OFF)
    FetchContent_Declare(
        tinyxml2
        GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
        GIT_TAG 11.0.0
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(tinyxml2)
endif()

#=================== spdlog ===================
find_package(spdlog QUIET)
if (NOT ${spdlog_FOUND})
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.16.0
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(spdlog)
endif()

#=================== libzip ===================
find_package(libzip QUIET)
if (NOT ${libzip_FOUND})
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_TOOLS OFF)
    set(BUILD_REGRESS OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_DOC OFF)
    set(BUILD_OSSFUZZ OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(ENABLE_COMMONCRYPTO OFF)
    set(ENABLE_GNUTLS OFF)
    set(ENABLE_MBEDTLS OFF)
    set(ENABLE_OPENSSL OFF)
    set(ENABLE_WINDOWS_CRYPTO OFF)
    set(ENABLE_BZIP2 OFF)
    set(ENABLE_LZMA OFF)
    set(ENABLE_ZSTD OFF)
    FetchContent_Declare(
        libzip
        GIT_REPOSITORY https://github.com/nih-at/libzip.git
        GIT_TAG v1.11.4
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(libzip)
    list(APPEND ADDITIONAL_LIB_INCLUDES ${libzip_SOURCE_DIR}/lib ${libzip_BINARY_DIR})
endif()

#=================== ImGui ===================
target_link_libraries(ImGui PUBLIC SDL2::SDL2)
add_compile_definitions(IMGUI_IMPL_OPENGL_ES3)
