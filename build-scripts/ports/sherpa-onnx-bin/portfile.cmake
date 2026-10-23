# JPet loads sherpa-onnx and ONNX Runtime at run time (LoadLibrary/dlopen) and
# only needs their C headers plus the shared libraries shipped with the app.
# The macOS build targets macOS 11, unlike Microsoft's prebuilt ONNX Runtime.
set(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled)
set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)
set(VCPKG_POLICY_SKIP_ARCHITECTURE_CHECK enabled)
set(VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK enabled)
set(ORT_VERSION 1.28.2)

if(VCPKG_TARGET_IS_WINDOWS)
    set(name "sherpa-onnx-v${VERSION}-win-x64-shared-MD-Release-no-tts-lib")
    set(hash b78054280aeaabf59d56870da4b74ef785bf28aa199852f6df3bb36a06b4cb0e0e8e9f1faf6af31c82935bbca24d70b24113784337168622af856ebe44f57091)
    set(runtimes sherpa-onnx-c-api.dll onnxruntime.dll onnxruntime_providers_shared.dll)
    set(ort_runtime onnxruntime.dll)
else()
    set(name "sherpa-onnx-v${VERSION}-osx-arm64-shared-no-tts-lib")
    set(hash 1d920ba14d61e5163a24d5568ef2c59c3e8e3ebcf50bb31360dd89bd21011ba3b981795d17cfff5e3e37d22a9fe7e639c2b1c6175908429f1d9a0502dd7e7cbd)
    set(runtimes libsherpa-onnx-c-api.dylib libonnxruntime.dylib)
    set(ort_runtime libonnxruntime.dylib)
endif()
vcpkg_download_distfile(LIBRARIES
    URLS "https://github.com/k2-fsa/sherpa-onnx/releases/download/v${VERSION}/${name}.tar.bz2"
    FILENAME "${name}.tar.bz2"
    SHA512 ${hash}
)
vcpkg_extract_source_archive(LIBRARY_PATH ARCHIVE "${LIBRARIES}")

vcpkg_download_distfile(SHERPA_HEADER
    URLS "https://raw.githubusercontent.com/k2-fsa/sherpa-onnx/v${VERSION}/sherpa-onnx/c-api/c-api.h"
    FILENAME "sherpa-onnx-${VERSION}-c-api.h"
    SHA512 5c66716c1ac89eea0f58f14863a201f850f36124ff711cdd7a0c300caf9cf96e925585989fb2f1b8900dae66065a53aee35edc94d8132a0decb8aba032d8c217
)
# The headers are platform independent; take them from the macOS release.
vcpkg_download_distfile(ORT_ARCHIVE
    URLS "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-osx-arm64-${ORT_VERSION}.tgz"
    FILENAME "onnxruntime-osx-arm64-${ORT_VERSION}.tgz"
    SHA512 314b54e31cabd560c4edce87e2d9a15c843a3b88bfad5e9acff09541b1a1d9f5b1934520dcf93f2eed8f047a112ad5a156515d9c4046271b770bbc375518451b
)
vcpkg_extract_source_archive(ORT_PATH ARCHIVE "${ORT_ARCHIVE}")

file(INSTALL "${SHERPA_HEADER}" DESTINATION "${CURRENT_PACKAGES_DIR}/include/sherpa-onnx/c-api" RENAME c-api.h)
file(GLOB headers "${ORT_PATH}/include/*.h" "${ORT_PATH}/include/*.inc")
file(INSTALL ${headers} DESTINATION "${CURRENT_PACKAGES_DIR}/include/onnxruntime")
# Nothing links these. On Windows they go to bin/ so they are copied next to
# JPet.exe with the other DLLs; on macOS CMake copies them into the app bundle.
if(VCPKG_TARGET_IS_WINDOWS)
    set(destination "bin")
else()
    set(destination "share/${PORT}/runtime")
endif()
foreach(runtime IN LISTS runtimes)
    file(INSTALL "${LIBRARY_PATH}/lib/${runtime}" DESTINATION "${CURRENT_PACKAGES_DIR}/${destination}")
endforeach()
list(TRANSFORM runtimes PREPEND "\${_sherpa_prefix}/${destination}/")
configure_file("${CMAKE_CURRENT_LIST_DIR}/unofficial-sherpa-onnx-bin-config.cmake.in"
    "${CURRENT_PACKAGES_DIR}/share/unofficial-sherpa-onnx-bin/unofficial-sherpa-onnx-bin-config.cmake" @ONLY)
file(WRITE "${CURRENT_BUILDTREES_DIR}/sherpa-onnx-notice.txt"
    "sherpa-onnx ${VERSION} (https://github.com/k2-fsa/sherpa-onnx) is licensed under the Apache License 2.0.\n")
vcpkg_install_copyright(FILE_LIST "${CURRENT_BUILDTREES_DIR}/sherpa-onnx-notice.txt" "${ORT_PATH}/LICENSE" "${ORT_PATH}/ThirdPartyNotices.txt")
