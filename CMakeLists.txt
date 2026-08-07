cmake_minimum_required(VERSION 3.16)
project(streamer LANGUAGES CXX)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(streamer ${SOURCES})

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

FetchContent_MakeAvailable(json)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
)

FetchContent_MakeAvailable(httplib)

target_include_directories(streamer PRIVATE
    "src"
)

# Try to find system-installed libwebsockets first
find_path(LWS_INCLUDE_DIR NAMES libwebsockets.h PATH_SUFFIXES include include/libwebsockets)
find_library(LWS_LIBRARY NAMES websockets libwebsockets lws PATH_SUFFIXES lib lib64)

if(NOT LWS_INCLUDE_DIR OR NOT LWS_LIBRARY)
    message(STATUS "libwebsockets not found on system - fetching and building into _deps via FetchContent")
    include(FetchContent)
    include(ExternalProject)

    set(LWS_GIT "https://github.com/warmcat/libwebsockets.git")
    set(LWS_TAG "v4.3.5")

    # installation location for the external project
    set(LWS_INSTALL_DIR ${CMAKE_BINARY_DIR}/_deps/libwebsockets-install)

    ExternalProject_Add(libwebsockets_ep
        GIT_REPOSITORY ${LWS_GIT}
        GIT_TAG ${LWS_TAG}
        PREFIX ${CMAKE_BINARY_DIR}/_deps/libwebsockets
        # configure options
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${LWS_INSTALL_DIR}
            -DBUILD_SHARED_LIBS=OFF
            -DLWS_WITH_SHARED=OFF
            -DLWS_WITHOUT_TESTAPPS=ON
            -DLWS_WITHOUT_TEST_SERVER=ON
            -DLWS_WITH_SSL=OFF
            -DLWS_WITHOUT_CLIENT=ON
            -DLWS_WITHOUT_BUILTIN_GETIFADDRS=OFF
            -DLWS_WITHOUT_SMD=ON
            -DLWS_WITH_PLUGINS=OFF
            -DLWS_WITH_HTTP2=OFF
            -DLWS_WITH_ZLIB=OFF
            -DLWS_WITHOUT_EXTENSIONS=ON
        UPDATE_COMMAND ""
    )

    # Optionally apply an external patch file before configure (if present)
    set(LWS_PATCH_FILE "${CMAKE_SOURCE_DIR}/va_args_windows.patch")
    if(EXISTS ${LWS_PATCH_FILE})
        ExternalProject_Add_Step(libwebsockets_ep apply_patch
            COMMAND ${CMAKE_COMMAND} -E echo "Applying patch ${LWS_PATCH_FILE}"
            COMMAND ${CMAKE_COMMAND} -E env git apply "${LWS_PATCH_FILE}"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/_deps/libwebsockets/src/libwebsockets_ep
            DEPENDEES update
        )
    endif()

    # After installation, include and lib will be in LWS_INSTALL_DIR
    set(LWS_INCLUDE_DIR ${LWS_INSTALL_DIR}/include)
    # Create an IMPORTED target that will be satisfied after the external project installs
    # libwebsockets produces websockets_static.lib on MSVC builds
    set(LWS_LIB_PATH ${LWS_INSTALL_DIR}/lib/websockets_static.lib)
    add_library(LWS::websockets STATIC IMPORTED)
    set_target_properties(LWS::websockets PROPERTIES IMPORTED_LOCATION "${LWS_LIB_PATH}")

    # Ensure the main target depends on the external project being built
    add_dependencies(streamer libwebsockets_ep)
endif()

if(DEFINED LWS_TARGET)
    target_link_libraries(streamer PRIVATE ${LWS_TARGET})
else()
    target_include_directories(streamer PRIVATE ${LWS_INCLUDE_DIR})
    # link against imported target if present, otherwise fallback to LWS::websockets
    if(TARGET LWS::websockets)
        target_link_libraries(streamer PRIVATE LWS::websockets)
    else()
        target_link_libraries(streamer PRIVATE ${LWS_LIBRARY})
    endif()
endif()

set(GST_ROOT $ENV{GSTREAMER_1_0_ROOT_MSVC_X86_64})

if(NOT GST_ROOT)
    message(FATAL_ERROR "GStreamer Windows SDK not found! Please check your environment variables or installation.")
endif()

# Bind GStreamer include folders (GStreamer needs BOTH paths below to find all headers)
target_include_directories(streamer PRIVATE 
    ${GST_ROOT}/include/gstreamer-1.0
    ${GST_ROOT}/lib/gstreamer-1.0/include
    ${GST_ROOT}/include/glib-2.0
    ${GST_ROOT}/lib/glib-2.0/include
)

# Bind GStreamer static library paths so CMake knows where `.lib` files live
target_link_directories(streamer PRIVATE ${GST_ROOT}/lib)

# Link the core GStreamer libraries you need
target_link_libraries(streamer PRIVATE 
    nlohmann_json::nlohmann_json
    gstreamer-1.0
    gobject-2.0
    glib-2.0
    ws2_32
    # Add optional sub-libraries here as needed:
    # gstapp-1.0
    # gstvideo-1.0
)

#add_custom_command(TARGET streamer POST_BUILD
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#        "${GST_ROOT}/bin"
#        "$<TARGET_FILE_DIR:streamer>"
#
#	COMMAND ${CMAKE_COMMAND} -E copy_directory
#        "${GST_ROOT}/lib/gstreamer-1.0"
#        "$<TARGET_FILE_DIR:streamer>/lib/gstreamer-1.0"
#)