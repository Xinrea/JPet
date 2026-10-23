# JPet links APM statically; abseil follows the triplet (a DLL on Windows).
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_download_distfile(ARCHIVE
    URLS "https://freedesktop.org/software/pulseaudio/webrtc-audio-processing/webrtc-audio-processing-${VERSION}.tar.xz"
    FILENAME "webrtc-audio-processing-${VERSION}.tar.xz"
    SHA512 534af17db1082e900ebd964042679685f26e8452c2e08c6f29660d0484bd5b079c804104600e972281d1ddcbca7762c63cc58b561b175b73447d069dd00f1a61
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")

# abseil's pkg-config files omit ABSL_CONSUME_DLL, which code using the
# abseil DLL needs for its exported data symbols.
if(EXISTS "${CURRENT_INSTALLED_DIR}/lib/pkgconfig/abseil_dll.pc")
    string(APPEND VCPKG_C_FLAGS " -DABSL_CONSUME_DLL")
    string(APPEND VCPKG_CXX_FLAGS " -DABSL_CONSUME_DLL")
endif()

vcpkg_configure_meson(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -Dneon=auto
        -Dinline-sse=true
)
vcpkg_install_meson()
vcpkg_fixup_pkgconfig()

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/unofficial-webrtc-audio-processing-config.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/unofficial-webrtc-audio-processing")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
