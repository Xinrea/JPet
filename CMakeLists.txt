cmake_minimum_required(VERSION 3.27)

option(
  CORE_CRL_MT
  "Use Cubism Core that is multithread-specific and DLL-specific version"
  OFF
)

# Set app name.
set(APP_NAME JPet)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_TOOLCHAIN_FILE "D:/vcpkg/scripts/buildsystems/vcpkg.cmake")
list(APPEND CMAKE_PREFIX_PATH "D:/vcpkg/installed/x64-windows")

# Set directory paths.
set(RES_PATH ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(THIRD_PARTY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
set(GLFW_PATH ${THIRD_PARTY_PATH}/glfw)
set(FMOD_PATH ${THIRD_PARTY_PATH}/fmod)
set(SDK_ROOT_PATH ${THIRD_PARTY_PATH}/CubismSdkForNative)
set(CORE_PATH ${SDK_ROOT_PATH}/Core)
set(FRAMEWORK_PATH ${SDK_ROOT_PATH}/Framework)

# Set project.
project(${APP_NAME} CXX)

set(GLEW_USE_STATIC_LIBS OFF)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(GLEW REQUIRED)
find_package(unofficial-wintoast CONFIG REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(unofficial-webview2 CONFIG REQUIRED)

# Set Visual Studio startup project.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${APP_NAME})

# Define output directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin/${APP_NAME})

# Set configuration (Release and Debug only).
set(CMAKE_CONFIGURATION_TYPES Debug Release
  CACHE STRING "Configurations" FORCE
)

# Suppress generation of ZERO_CHECK project.
set(CMAKE_SUPPRESS_REGENERATION ON)

# Surpress GLEW and GLFW process.
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_UTILS OFF CACHE BOOL "" FORCE)

# Specify version of compiler.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Detect architecture.
if(CMAKE_EXE_LINKER_FLAGS STREQUAL "/machine:x64")
  set(ARCH x86_64)
  set(FMOD_ARCH x64)
elseif(CMAKE_EXE_LINKER_FLAGS STREQUAL "/machine:X86")
  set(ARCH x86)
  set(FMOD_ARCH x86)
else()
  message(FATAL_ERROR "[${APP_NAME}] Invalid linker flag ${CMAKE_EXE_LINKER_FLAGS}.")
endif()

set(COMPILER 142)
add_definitions(-DUNICODE -D_UNICODE)

# Detect core crt.
if(CORE_CRL_MD)
  set(CRT MD)
else()
  set(CRT MT)
endif()

# Add Cubism Core.
# Import as static library.
add_library(Live2DCubismCore STATIC IMPORTED)

# Find library path.
set(CORE_LIB_SUFFIX ${CORE_PATH}/lib/windows/${ARCH}/${COMPILER})
set_target_properties(Live2DCubismCore
  PROPERTIES
  IMPORTED_LOCATION_DEBUG
  ${CORE_LIB_SUFFIX}/Live2DCubismCore_${CRT}d.lib
  IMPORTED_LOCATION_RELEASE
  ${CORE_LIB_SUFFIX}/Live2DCubismCore_${CRT}.lib
  INTERFACE_INCLUDE_DIRECTORIES ${CORE_PATH}/include
)

# Add GLEW ,GLFW.
add_subdirectory(${GLFW_PATH} ${CMAKE_CURRENT_BINARY_DIR}/glfw)

# Specify Cubism Framework rendering.
set(FRAMEWORK_SOURCE OpenGL)

# Add Cubism Native Framework.
add_subdirectory(${FRAMEWORK_PATH})

# Add rendering definition to framework.
target_compile_definitions(Framework PUBLIC CSM_TARGET_WIN_GL)

# Add include path of GLEW to framework.
target_include_directories(Framework PUBLIC ${GLEW_PATH}/include)

# Link libraries to framework.
target_link_libraries(Framework Live2DCubismCore GLEW::GLEW)

# Find opengl libraries.
find_package(OpenGL REQUIRED)

# Make executable app.
add_executable(${APP_NAME})

# Add source files.
add_subdirectory(src)

# Link libraries to app.
target_link_libraries(${APP_NAME}
  Framework
  GLEW::GLEW
  OpenSSL::SSL
  OpenSSL::Crypto
  unofficial::wintoast::wintoast
  httplib::httplib
  rapidjson
  glfw
  fmod_vc
  unofficial::webview2::webview2

  # Solve the MSVCRT confliction.
  debug -NODEFAULTLIB:libcmtd.lib
  optimized -NODEFAULTLIB:libcmt.lib
)

# Specify include directories.
target_include_directories(${APP_NAME} PRIVATE ${FMOD_PATH}/core/inc ${GLEW_INCLUDE_DIRS} ${Stb_INCLUDE_DIR})
target_link_directories(${APP_NAME} PRIVATE ${FMOD_PATH}/core/lib/${FMOD_ARCH}/ ${GLEW_LIBRARIES})

# Build in multi-process.
target_compile_options(${APP_NAME} PRIVATE /MP)

# Copy resource directory to build directory.
add_custom_command(
  TARGET ${APP_NAME}
  POST_BUILD
  COMMAND
  ${CMAKE_COMMAND} -E
  copy_directory ${RES_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/resources
)

add_custom_command(TARGET ${APP_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${FMOD_PATH}/core/lib/${FMOD_ARCH}/fmod.dll"
  $<TARGET_FILE_DIR:${APP_NAME}>)

# Set project properties.
set_target_properties(${APP_NAME} PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY
  ${CMAKE_CURRENT_BINARY_DIR}/bin/${APP_NAME}/${CMAKE_CFG_INTDIR}
)

# You can change target that renderer draws by enabling following definition.
#
# * USE_RENDER_TARGET
# Renderer draws to target of LAppView.
# * USE_MODEL_RENDER_TARGET
# Renderer draws to target of each LAppModel.
# * default
# Renderer draws to default main framebuffer.
#
# INFO: USE_RENDER_TARGET has higher priority than USE_MODEL_RENDER_TARGET.
#
target_compile_definitions(${APP_NAME}
  PRIVATE
  CPPHTTPLIB_OPENSSL_SUPPORT
  WIN32_LEAN_AND_MEAN
  _HAS_STD_BYTE=0
)