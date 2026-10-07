cmake_minimum_required(VERSION 3.27)

option(
  CORE_CRL_MT
  "Use Cubism Core that is multithread-specific and DLL-specific version"
  OFF
)

# Set app name.
set(APP_NAME JPet)

# JVERSION can be set by -D option.
if(NOT DEFINED JVERSION)
  set(JVERSION "3.0.1-beta.2")
endif()

message(STATUS "JPet version: ${JVERSION}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set directory paths.
set(RES_PATH ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(THIRD_PARTY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
set(GLFW_PATH ${THIRD_PARTY_PATH}/glfw)
set(FMOD_PATH ${THIRD_PARTY_PATH}/fmod)
set(ROCKSDB_PATH ${THIRD_PARTY_PATH}/rocksdb)
set(SDK_ROOT_PATH ${THIRD_PARTY_PATH}/CubismSdkForNative)
set(CORE_PATH ${SDK_ROOT_PATH}/Core)
set(FRAMEWORK_PATH ${SDK_ROOT_PATH}/Framework)

# Set project.
project(${APP_NAME} CXX)

include(cmake/CloudConfig.cmake)
jpet_configure_cloud("${CMAKE_CURRENT_BINARY_DIR}/generated/JPetCloudConfig.hpp")

if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "JPet supports Apple Silicon only")
  if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "macOS builds support arm64 only; use -DCMAKE_OSX_ARCHITECTURES=arm64")
  endif()
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

if(APPLE)
  set(GLEW_USE_STATIC_LIBS ON)
else()
  set(GLEW_USE_STATIC_LIBS OFF)
endif()

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(GLEW REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(cryptopp CONFIG REQUIRED)
find_package(croncpp CONFIG REQUIRED)
find_package(semver CONFIG REQUIRED)
find_path(TOMLPLUSPLUS_INCLUDE_DIR toml++/toml.hpp REQUIRED)
if(WIN32)
  find_package(unofficial-wintoast CONFIG REQUIRED)
  find_package(unofficial-webview2 CONFIG REQUIRED)
elseif(APPLE)
  find_package(RocksDB CONFIG REQUIRED)
else()
  message(FATAL_ERROR "Supported platforms: Windows and macOS arm64")
endif()

# Set Visual Studio startup project.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${APP_NAME})

# Define output directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin/${APP_NAME})

# Set configuration (Release and Debug only).
set(CMAKE_CONFIGURATION_TYPES Debug Release
  CACHE STRING "Configurations" FORCE
)

# Reconfigure when build inputs, including the cloud domain, change.
set(CMAKE_SUPPRESS_REGENERATION OFF)

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
if(APPLE)
  set(ARCH arm64)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(ARCH x86_64)
  set(FMOD_ARCH x64)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
  set(ARCH x86_64)
  set(FMOD_ARCH x64)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86")
  set(ARCH x86)
  set(FMOD_ARCH x86)
else()
  message(FATAL_ERROR "Unknown architecture")
endif()

# Add Cubism Core.
# Import as static library.
add_library(Live2DCubismCore STATIC IMPORTED)

if(APPLE)
  set_target_properties(Live2DCubismCore PROPERTIES
    IMPORTED_LOCATION "${CORE_PATH}/lib/macos/arm64/libLive2DCubismCore.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CORE_PATH}/include"
  )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  message(NOTICE "Using MSVC")

  if(CORE_CRL_MD)
    set(CRT MD)
  else()
    set(CRT MT)
  endif()

  set(COMPILER 143)
  add_definitions(-DUNICODE -D_UNICODE)

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
else()
  # Find library path.
  set(CORE_LIB_SUFFIX ${CORE_PATH}/dll/windows/${ARCH})
  set_target_properties(Live2DCubismCore
    PROPERTIES
    IMPORTED_LOCATION
    ${CORE_LIB_SUFFIX}/Live2DCubismCore.lib
    INTERFACE_INCLUDE_DIRECTORIES ${CORE_PATH}/include
  )
endif()

# Add GLEW ,GLFW.
add_subdirectory(${GLFW_PATH} ${CMAKE_CURRENT_BINARY_DIR}/glfw)

if(WIN32)
  add_subdirectory(${ROCKSDB_PATH} ${CMAKE_CURRENT_BINARY_DIR}/rocksdb)
endif()

# Specify Cubism Framework rendering.
set(FRAMEWORK_SOURCE OpenGL)

# Add Cubism Native Framework.
add_subdirectory(${FRAMEWORK_PATH})

# Add rendering definition to framework.
if(APPLE)
  target_compile_definitions(Framework PUBLIC CSM_TARGET_MAC_GL CSM_TARGET_COCOS GL_SILENCE_DEPRECATION)
else()
  target_compile_definitions(Framework PUBLIC CSM_TARGET_WIN_GL)
endif()

# Add include path of GLEW to framework.
if(NOT APPLE)
  target_include_directories(Framework PUBLIC ${GLEW_PATH}/include)
endif()

# Link libraries to framework.
target_link_libraries(Framework Live2DCubismCore)
if(NOT APPLE)
  target_link_libraries(Framework GLEW::GLEW)
endif()

# Find opengl libraries.
find_package(OpenGL REQUIRED)

# Make executable app.
# if Release, should use WIN32 to hide console window.
if(APPLE)
  add_executable(${APP_NAME} MACOSX_BUNDLE)
  set_target_properties(${APP_NAME} PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "${APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "cn.vjoi.jpet"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${JVERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${JVERSION}"
    OBJCXX_STANDARD 17
    OBJCXX_STANDARD_REQUIRED YES
  )
  target_compile_options(${APP_NAME} PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_executable(${APP_NAME} WIN32)
else()
  add_executable(${APP_NAME})
endif()

# Add source files.
add_subdirectory(src)

if(MSVC)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
  set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()

# Link libraries to app.
target_link_libraries(${APP_NAME}
  PRIVATE
  Framework
  OpenSSL::SSL
  OpenSSL::Crypto
  httplib::httplib
  glfw
  nlohmann_json::nlohmann_json
  cpr::cpr
  cryptopp::cryptopp
  croncpp::croncpp
  semver::semver
  Threads::Threads
  OpenGL::GL
)

# Specify include directories.
target_include_directories(${APP_NAME} PRIVATE ${GLEW_INCLUDE_DIRS} ${Stb_INCLUDE_DIR} ${TOMLPLUSPLUS_INCLUDE_DIR}
  "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(APPLE)
  target_link_libraries(${APP_NAME} PRIVATE RocksDB::rocksdb
    "-framework AppKit" "-framework WebKit" "-framework AVFoundation"
    "-framework Foundation" "-framework UserNotifications")
  target_link_libraries(${APP_NAME} PRIVATE "-framework Security")
  target_link_libraries(${APP_NAME} PRIVATE "-weak_framework ScreenCaptureKit" "-framework ImageIO")
else()
  target_link_libraries(${APP_NAME} PRIVATE GLEW::GLEW)
  target_include_directories(${APP_NAME} PRIVATE ${FMOD_PATH}/core/inc)
  target_link_directories(${APP_NAME} PRIVATE ${FMOD_PATH}/core/lib/${FMOD_ARCH}/)
  target_link_libraries(${APP_NAME} PRIVATE rocksdb fmod_vc Dbghelp Crypt32
    Winhttp Winmm Advapi32 Gdiplus Ole32 Gdi32
    unofficial::wintoast::wintoast unofficial::webview2::webview2)
endif()

# Build in multi-process.
if(MSVC)
  target_compile_options(${APP_NAME} PRIVATE /MP /utf-8)
  target_compile_options(rocksdb PRIVATE /MP /utf-8)
  target_link_libraries(${APP_NAME} PRIVATE
    debug -NODEFAULTLIB:libcmtd.lib optimized -NODEFAULTLIB:libcmt.lib)
endif()

# Copy resource directory to build directory, except for panel

if(APPLE)
  set(APP_RESOURCES "$<TARGET_BUNDLE_CONTENT_DIR:${APP_NAME}>/Resources/resources")
else()
  set(APP_RESOURCES "$<TARGET_FILE_DIR:${APP_NAME}>/resources")
endif()
if(NOT EXISTS "${RES_PATH}/panel/dist/index.html")
  message(FATAL_ERROR "Build the settings panel first: cd resources/panel && npm ci && npm run build")
endif()

foreach(DIRECTORY audios imgs circle-menu joi panel/dist updater)
  string(REPLACE "/" "_" DIRECTORY_TARGET "${DIRECTORY}")
  add_custom_target(${APP_NAME}_resource_${DIRECTORY_TARGET} ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_RESOURCES}/${DIRECTORY}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${RES_PATH}/${DIRECTORY}" "${APP_RESOURCES}/${DIRECTORY}"
    VERBATIM)
  add_dependencies(${APP_NAME} ${APP_NAME}_resource_${DIRECTORY_TARGET})
endforeach()
if(WIN32)
  add_custom_target(${APP_NAME}_resource_logo ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RES_PATH}/logo.ico" "${APP_RESOURCES}/logo.ico"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${FMOD_PATH}/core/lib/${FMOD_ARCH}/fmod.dll" $<TARGET_FILE_DIR:${APP_NAME}>)
  add_dependencies(${APP_NAME} ${APP_NAME}_resource_logo)
endif()

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
  NOMINMAX
  _HAS_STD_BYTE=0
  VERSION="${JVERSION}"
)
# if build Debug, add debug definition.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(${APP_NAME} PRIVATE JPET_DEBUG)
endif()

option(JPET_BUILD_UPDATE_TESTS "Build release selection and checksum tests" OFF)
option(JPET_BUILD_VOICE_TESTS "Build push-to-talk voice protocol tests" OFF)
if(JPET_BUILD_VOICE_TESTS)
  enable_testing()
  add_executable(voice_session_test tests/voice_session_test.cpp src/VoiceSession.cpp)
  set_target_properties(voice_session_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
  target_include_directories(voice_session_test PRIVATE src)
  target_compile_options(voice_session_test PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
  target_link_libraries(voice_session_test PRIVATE nlohmann_json::nlohmann_json Threads::Threads)
  add_test(NAME voice_session COMMAND voice_session_test)
  add_executable(voice_tools_test tests/voice_tools_test.cpp src/VoiceTools.cpp src/VoiceSession.cpp)
  set_target_properties(voice_tools_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
  target_include_directories(voice_tools_test PRIVATE src)
  target_compile_options(voice_tools_test PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
  target_link_libraries(voice_tools_test PRIVATE nlohmann_json::nlohmann_json Threads::Threads cpr::cpr)
  add_test(NAME voice_tools COMMAND voice_tools_test)
  if(APPLE)
    add_executable(voice_audio_mac_test tests/voice_audio_mac_test.mm)
    set_target_properties(voice_audio_mac_test PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
      OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON)
    target_include_directories(voice_audio_mac_test PRIVATE src)
    target_compile_options(voice_audio_mac_test PRIVATE -fobjc-arc)
    target_link_libraries(voice_audio_mac_test PRIVATE "-framework AVFoundation" "-framework Foundation" objc)
    add_test(NAME voice_audio_mac COMMAND voice_audio_mac_test)
    add_executable(voice_desktop_mac_test tests/voice_desktop_mac_test.mm src/VoiceDesktopMac.mm)
    set_target_properties(voice_desktop_mac_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
      OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON)
    target_include_directories(voice_desktop_mac_test PRIVATE src)
    target_compile_options(voice_desktop_mac_test PRIVATE -fobjc-arc)
    target_link_libraries(voice_desktop_mac_test PRIVATE "-framework AppKit" "-framework ImageIO" "-weak_framework ScreenCaptureKit" objc)
    add_test(NAME voice_desktop_mac COMMAND voice_desktop_mac_test)
    option(JPET_BUILD_VOICE_NETWORK_PROBE "Build opt-in read-only voice tool integration probe (uses configured accounts)" OFF)
    if(JPET_BUILD_VOICE_NETWORK_PROBE)
      add_executable(voice_tools_network_probe tests/voice_tools_network_probe.mm src/VoiceToolsNetwork.cpp
        src/VoiceTools.cpp src/VoiceSession.cpp src/VoiceDesktopMac.mm)
      set_target_properties(voice_tools_network_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
        OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON)
      target_include_directories(voice_tools_network_probe PRIVATE src ${TOMLPLUSPLUS_INCLUDE_DIR})
      target_compile_options(voice_tools_network_probe PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
      target_link_libraries(voice_tools_network_probe PRIVATE nlohmann_json::nlohmann_json cpr::cpr httplib::httplib
        cryptopp::cryptopp OpenSSL::SSL OpenSSL::Crypto RocksDB::rocksdb Threads::Threads "-framework Foundation" "-framework Security" "-framework LocalAuthentication"
        "-framework AppKit" "-framework ImageIO" "-weak_framework ScreenCaptureKit" objc)
    endif()
  endif()
endif()
if(JPET_BUILD_UPDATE_TESTS)
  enable_testing()
  add_executable(update_release_test tests/update_release_test.cpp)
  target_include_directories(update_release_test PRIVATE src)
  target_compile_options(update_release_test PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
  target_link_libraries(update_release_test PRIVATE OpenSSL::Crypto nlohmann_json::nlohmann_json semver::semver)
  add_test(NAME update_release COMMAND update_release_test)
endif()
