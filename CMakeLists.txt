cmake_minimum_required(VERSION 3.16)
project(streamer LANGUAGES CXX)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(streamer ${SOURCES})

target_include_directories(streamer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

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
    gstreamer-1.0
    gobject-2.0
    glib-2.0
    # Add optional sub-libraries here as needed:
    # gstapp-1.0
    # gstvideo-1.0
)

add_custom_command(TARGET streamer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${GST_ROOT}/bin"
        "$<TARGET_FILE_DIR:streamer>"
		
	COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${GST_ROOT}/lib/gstreamer-1.0"
        "$<TARGET_FILE_DIR:streamer>/lib/gstreamer-1.0"
)