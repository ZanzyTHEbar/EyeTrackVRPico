# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

# build native_app_glue as a static lib
set(APP_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
include_directories(${APP_GLUE_DIR})
add_library( app-glue STATIC ${APP_GLUE_DIR}/android_native_app_glue.c)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

file(GLOB LOCAL_HEADERS  "cube_xr/*.h")
file(GLOB LOCAL_SOURCE   "cube_xr/*.cpp")

LINK_DIRECTORIES(lib/${ANDROID_ABI})
include_directories(lib/include)
include_directories(cube_xr/glm)


#add_subdirectory(cube_xr/cglm/ EXCLUDE_FROM_ALL)

add_library( # Sets the name of the library.
        cube_xr
		SHARED
        ${LOCAL_SOURCE}
        ${LOCAL_HEADERS}
		)

source_group("Headers" FILES ${LOCAL_HEADERS})

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
                       cube_xr
                       pxr_api
                       android
                       EGL
                       GLESv3
                       app-glue
                       log
		               #cglm_headers
                       )