cmake_minimum_required(VERSION 3.22)

project(JpegTextures)
set(CMAKE_CXX_STANDARD 23)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(cmake/common.cmake)

##############################################
## SUBPROJECT: JpegTextures
##############################################

set(TARGET_NAME JpegTextures) 

# Paths to TurboJPEG
set(TURBOJPEG_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/libs/jpg/include")
set(TURBOJPEG_LIBRARY "${CMAKE_SOURCE_DIR}/libs/jpg/turbojpeg-static.lib") 
# If needed: set(JPEG_LIBRARY "${CMAKE_SOURCE_DIR}/libs/jpg/jpeg-static.lib")

set(JpegTextures_SOURCES
	src/main.cpp
	src/SplatEditor.cpp
	src/GLRenderer.cpp
	src/unsuck_platform_specific.cpp
	src/ImageLoader.cpp
	src/OpenVRHelper.cpp
)

if (WIN32)
    list(APPEND JpegTextures_SOURCES src/GLRenderer_win32.cpp)
elseif (UNIX)
    list(APPEND JpegTextures_SOURCES src/GLRenderer_linux.cpp)
endif()

add_executable(${TARGET_NAME} ${JpegTextures_SOURCES})

if (WIN32)
    set_property(TARGET ${TARGET_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$(SolutionDir)..")
endif()

# Include paths
target_include_directories(${TARGET_NAME} PRIVATE
    ./src
    ./libs
    ${TURBOJPEG_INCLUDE_DIR}
)

# Link TurboJPEG (and libjpeg if needed)
target_link_libraries(${TARGET_NAME}
    ${TURBOJPEG_LIBRARY}
    CUDA::nvJitLink
)

# Add other third-party libs
ADD_CUDA(${TARGET_NAME})
ADD_OPENGL(${TARGET_NAME})
ADD_GLM(${TARGET_NAME})
ADD_IMGUI(${TARGET_NAME})
ADD_IMPLOT(${TARGET_NAME})
ADD_IMGUIZMO(${TARGET_NAME})

# I give up, no idea how to properly include OpenVR, let's just include it for windows-only like this.
target_include_directories(${TARGET_NAME} PRIVATE ./libs/openvr/headers)
if (WIN32)
	target_link_libraries(${TARGET_NAME} ../libs/openvr/lib/win64/openvr_api)
	add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy "$(SolutionDir)../libs/openvr/bin/win64/openvr_api.dll" "$(OutDir)"
		COMMENT "POST BUILD: copying openvr folder")
else()
	add_compile_definitions(NO_OPENVR)
endif (WIN32)

##############################################
## SOME FINAL STUFF
##############################################

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT JpegTextures)
