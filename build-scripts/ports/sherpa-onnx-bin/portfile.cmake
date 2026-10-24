# JPet loads these libraries with LoadLibrary/dlopen, so ship only C headers
# and Release runtimes, not link libraries or unrelated upstream targets.
set(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled)
set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)
set(VCPKG_BUILD_TYPE release)
# Runtime-loaded libraries must be shared even with JPet's static macOS triplet.
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_OSX_DEPLOYMENT_TARGET 11.0)
# These dylibs travel together into the app bundle, not the package's lib/.
set(VCPKG_FIXUP_MACHO_RPATH OFF)
set(ORT_VERSION 1.28.2)

if(VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_CRT_LINKAGE STREQUAL "dynamic")
    message(FATAL_ERROR "sherpa-onnx-bin requires the Windows dynamic CRT (MD).")
endif()

vcpkg_download_distfile(SOURCE_ARCHIVE
    URLS "https://codeload.github.com/k2-fsa/sherpa-onnx/tar.gz/refs/tags/v${VERSION}"
    FILENAME "sherpa-onnx-v${VERSION}.tar.gz"
    SHA512 a038be34c1c39e09115c28b9cda0eaf74319b534e2edcefbe1224bc67a1e8a550a598cd7e5c31287496f9a18c7b1c96fddcbf70afaadcbc5da35d7ac2c5eb8c7
)
vcpkg_extract_source_archive(SOURCE_PATH
    ARCHIVE "${SOURCE_ARCHIVE}"
    PATCHES sensevoice-hotwords.patch
)

# Reuse only the pinned ONNX Runtime from the official sherpa release. In
# particular its macOS runtime supports macOS 11; Microsoft's binary does not.

if(VCPKG_TARGET_IS_WINDOWS)
    set(name "sherpa-onnx-v${VERSION}-win-x64-shared-MD-Release-no-tts-lib")
    set(hash b78054280aeaabf59d56870da4b74ef785bf28aa199852f6df3bb36a06b4cb0e0e8e9f1faf6af31c82935bbca24d70b24113784337168622af856ebe44f57091)
    set(sherpa_runtime sherpa-onnx-c-api.dll)
    set(ort_runtimes onnxruntime.dll onnxruntime_providers_shared.dll)
    set(ort_runtime onnxruntime.dll)
else()
    set(name "sherpa-onnx-v${VERSION}-osx-arm64-shared-no-tts-lib")
    set(hash 1d920ba14d61e5163a24d5568ef2c59c3e8e3ebcf50bb31360dd89bd21011ba3b981795d17cfff5e3e37d22a9fe7e639c2b1c6175908429f1d9a0502dd7e7cbd)
    set(sherpa_runtime libsherpa-onnx-c-api.dylib)
    set(ort_runtimes libonnxruntime.dylib)
    set(ort_runtime libonnxruntime.dylib)
endif()
vcpkg_download_distfile(LIBRARIES
    URLS "https://github.com/k2-fsa/sherpa-onnx/releases/download/v${VERSION}/${name}.tar.bz2"
    FILENAME "${name}.tar.bz2"
    SHA512 ${hash}
)
vcpkg_extract_source_archive(LIBRARY_PATH ARCHIVE "${LIBRARIES}")

# The ORT headers are platform independent; retain the existing pinned source.
vcpkg_download_distfile(ORT_ARCHIVE
    URLS "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-osx-arm64-${ORT_VERSION}.tgz"
    FILENAME "onnxruntime-osx-arm64-${ORT_VERSION}.tgz"
    SHA512 314b54e31cabd560c4edce87e2d9a15c843a3b88bfad5e9acff09541b1a1d9f5b1934520dcf93f2eed8f047a112ad5a156515d9c4046271b770bbc375518451b
)
vcpkg_extract_source_archive(ORT_PATH ARCHIVE "${ORT_ARCHIVE}")

# Upstream consumes these environment variables, not -D options. Explicitly
# provide both locations so it cannot find a system ORT or download another one.
vcpkg_backup_env_variables(VARS SHERPA_ONNXRUNTIME_INCLUDE_DIR SHERPA_ONNXRUNTIME_LIB_DIR GIT_CEILING_DIRECTORIES)
set(ENV{SHERPA_ONNXRUNTIME_INCLUDE_DIR} "${ORT_PATH}/include")
set(ENV{SHERPA_ONNXRUNTIME_LIB_DIR} "${LIBRARY_PATH}/lib")
# This archive has no Git metadata: don't report JPet's commit as sherpa's.
get_filename_component(source_parent "${SOURCE_PATH}" DIRECTORY)
set(ENV{GIT_CEILING_DIRECTORIES} "${source_parent}")
set(platform_options)
if(VCPKG_TARGET_IS_WINDOWS)
    # Upstream 1.13.8's preinstalled CPU branch sets IMPORTED_LOCATION to the
    # .lib but omits IMPORTED_IMPLIB. Supply that variable without another patch.
    list(APPEND platform_options "-Dlocation_onnxruntime_lib2=${LIBRARY_PATH}/lib/onnxruntime.lib")
else()
    list(APPEND platform_options -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0)
endif()
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        # Upstream pins and verifies its small static build dependencies.
        -DFETCHCONTENT_FULLY_DISCONNECTED=OFF
        -DBUILD_SHARED_LIBS=ON
        -DSHERPA_ONNX_ENABLE_C_API=ON
        -DSHERPA_ONNX_ENABLE_BINARY=OFF
        -DSHERPA_ONNX_BUILD_C_API_EXAMPLES=OFF
        -DSHERPA_ONNX_ENABLE_TTS=OFF
        -DSHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION=OFF
        -DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF
        -DSHERPA_ONNX_ENABLE_WEBSOCKET=OFF
        -DSHERPA_ONNX_ENABLE_PYTHON=OFF
        -DSHERPA_ONNX_ENABLE_JNI=OFF
        -DSHERPA_ONNX_ENABLE_TESTS=OFF
        -DSHERPA_ONNX_ENABLE_GPU=OFF
        -DSHERPA_ONNX_ENABLE_DIRECTML=OFF
        -DSHERPA_ONNX_USE_STATIC_CRT=OFF
        -DSHERPA_ONNX_USE_PRE_INSTALLED_ONNXRUNTIME_IF_AVAILABLE=ON
        # We copy the build output instead of running upstream's full install.
        # Use upstream's @loader_path rpath; remove its extra link path below.
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
        ${platform_options}
)
vcpkg_cmake_build(TARGET sherpa-onnx-c-api)
vcpkg_restore_env_variables(VARS SHERPA_ONNXRUNTIME_INCLUDE_DIR SHERPA_ONNXRUNTIME_LIB_DIR GIT_CEILING_DIRECTORIES)

file(INSTALL "${SOURCE_PATH}/sherpa-onnx/c-api/c-api.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include/sherpa-onnx/c-api")
file(GLOB headers "${ORT_PATH}/include/*.h" "${ORT_PATH}/include/*.inc")
file(INSTALL ${headers} DESTINATION "${CURRENT_PACKAGES_DIR}/include/onnxruntime")
# Nothing links these. On Windows they go to bin/ so they are copied next to
# JPet.exe with the other DLLs; on macOS CMake copies them into the app bundle.
set(release_build "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel")
if(VCPKG_TARGET_IS_WINDOWS)
    set(destination "bin")
    set(sherpa_output "${release_build}/bin/${sherpa_runtime}")
else()
    set(destination "share/${PORT}/runtime")
    set(sherpa_output "${release_build}/lib/${sherpa_runtime}")
endif()
file(INSTALL "${sherpa_output}" DESTINATION "${CURRENT_PACKAGES_DIR}/${destination}")
if(VCPKG_TARGET_IS_OSX)
    # Upstream forces INSTALL_RPATH_USE_LINK_PATH=TRUE, adding the ORT build
    # directory alongside @loader_path. Keep only the adjacent-runtime lookup.
    find_program(INSTALL_NAME_TOOL install_name_tool REQUIRED)
    vcpkg_execute_required_process(
        COMMAND "${INSTALL_NAME_TOOL}" -delete_rpath "${LIBRARY_PATH}/lib"
            "${CURRENT_PACKAGES_DIR}/${destination}/${sherpa_runtime}"
        WORKING_DIRECTORY "${CURRENT_PACKAGES_DIR}"
        LOGNAME fix-runtime-rpath
    )
endif()
foreach(runtime IN LISTS ort_runtimes)
    file(INSTALL "${LIBRARY_PATH}/lib/${runtime}" DESTINATION "${CURRENT_PACKAGES_DIR}/${destination}")
endforeach()
set(runtimes ${sherpa_runtime} ${ort_runtimes})
list(TRANSFORM runtimes PREPEND "\${_sherpa_prefix}/${destination}/")
configure_file("${CMAKE_CURRENT_LIST_DIR}/unofficial-sherpa-onnx-bin-config.cmake.in"
    "${CURRENT_PACKAGES_DIR}/share/unofficial-sherpa-onnx-bin/unofficial-sherpa-onnx-bin-config.cmake" @ONLY)
# Static dependencies are compiled into the C API library. Keep their notices
# as well as the full sherpa license (not just an Apache license reference).
set(deps "${release_build}/_deps")
vcpkg_install_copyright(FILE_LIST
    "${SOURCE_PATH}/LICENSE"
    "${ORT_PATH}/LICENSE"
    "${ORT_PATH}/ThirdPartyNotices.txt"
    "${deps}/kaldi_native_fbank-src/LICENSE"
    "${deps}/kissfft-src/COPYING"
    "${deps}/kissfft-src/LICENSES/BSD-3-Clause"
    "${deps}/kaldi_decoder-src/LICENSE"
    "${deps}/kaldifst-src/LICENSE"
    "${deps}/openfst-src/COPYING"
    "${deps}/eigen-src/LICENSE"
    "${deps}/simple-sentencepiece-src/LICENSE"
    "${deps}/json-src/LICENSE.MIT"
    COMMENT "JPet-patched sherpa-onnx ${VERSION}; ONNX Runtime ${ORT_VERSION}.\nEigen 5.0.1 source: https://gitlab.com/libeigen/eigen/-/archive/5.0.1/eigen-5.0.1.tar.gz"
)
