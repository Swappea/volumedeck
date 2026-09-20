# Toolchain file for cross-compiling to macOS from Linux using OSXCross

SET(CMAKE_SYSTEM_NAME Darwin)
SET(CMAKE_SYSTEM_PROCESSOR x86_64)

# Specify the cross compiler (darwin21 for macOS 12.x SDK)
SET(CMAKE_C_COMPILER o64-clang)
SET(CMAKE_CXX_COMPILER o64-clang++)

# Where is the target environment
SET(CMAKE_FIND_ROOT_PATH /opt/osxcross/target)

# Search for programs in the build host directories
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# For libraries and headers in the target directories
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# macOS specific flags
SET(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")

# Custom OpenGL headers for cross-compilation (if provided)
# Use only OpenGL headers from custom package, system headers from OSXCross SDK 12.3
IF(DEFINED ENV{MACOS_OPENGL_HEADERS_DIR})
    MESSAGE(STATUS "Using custom OpenGL headers from: $ENV{MACOS_OPENGL_HEADERS_DIR}")
    # Add before system paths to override OSXCross OpenGL headers
    INCLUDE_DIRECTORIES(BEFORE SYSTEM "$ENV{MACOS_OPENGL_HEADERS_DIR}")
ENDIF()

