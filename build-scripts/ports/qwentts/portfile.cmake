# JPet loads qwentts.cpp's C library (src/qwen.h) at run time and only needs
# that header plus the shared libraries shipped with the app.
set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled)
set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)
set(VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK enabled)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ServeurpersoCom/qwentts.cpp
    REF 5916bf7c79dbb4e66fc059cdc507a79a23a28bb4
    SHA512 b0463efa3b0ba1c04e61e9032e8654fb9bc179594cfd3f5244e03a9016227c3dccd462ba5498e6ac7c8b16c5c627f482364d17484e029d96347c223a0b04eabb
    HEAD_REF master
)
# The ggml revision qwentts.cpp pins as its submodule.
vcpkg_from_github(
    OUT_SOURCE_PATH GGML_PATH
    REPO ServeurpersoCom/ggml
    REF cc5df49a6b878db2b68b9a7b45976e279594627d
    SHA512 70a30cdb4721bcab81da3f1270151af1b46d26210ede1c4561c789d3dd9adc486c320ddabef8042278e67d0e01b58c740284ade004eea74d73e7db9fdd1c6048
    HEAD_REF master
)

set(options
    -DQWEN_SHARED=ON
    "-DGGML_SOURCE_DIR=${GGML_PATH}"
    -DGGML_NATIVE=OFF
    -DGGML_OPENMP=OFF
    -DGGML_BUILD_TESTS=OFF
    -DGGML_BUILD_EXAMPLES=OFF
)
if(VCPKG_TARGET_IS_WINDOWS)
    # GGML backends are modules next to JPet.exe: ggml picks the CPU variant
    # for the machine and skips Vulkan when no Vulkan driver is installed.
    list(APPEND options -DBUILD_SHARED_LIBS=ON -DGGML_BACKEND_DL=ON -DGGML_CPU_ALL_VARIANTS=ON)
    if("vulkan" IN_LIST FEATURES)
        if(NOT DEFINED ENV{VULKAN_SDK})
            message(FATAL_ERROR "qwentts[vulkan] needs the Vulkan SDK: install it, set VULKAN_SDK and pass it to "
                "vcpkg (VCPKG_KEEP_ENV_VARS=VULKAN_SDK, or VCPKG_ENV_PASSTHROUGH in the triplet)")
        endif()
        list(APPEND options -DGGML_VULKAN=ON)
    endif()
    set(build_target all)
    set(destination bin)
else()
    # One self-contained library: GGML linked in and its Metal shaders embedded.
    list(APPEND options -DBUILD_SHARED_LIBS=OFF -DGGML_METAL=ON -DGGML_METAL_EMBED_LIBRARY=ON
        -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0)
    set(build_target qwen)
    set(destination share/${PORT}/runtime)
endif()

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}" OPTIONS ${options})
vcpkg_cmake_build(TARGET ${build_target})

set(build_dir "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel")
if(VCPKG_TARGET_IS_WINDOWS)
    set(library qwen.dll)
    file(GLOB runtimes RELATIVE "${build_dir}" "${build_dir}/qwen.dll" "${build_dir}/ggml*.dll")
else()
    set(library libqwen.dylib)
    set(runtimes libqwen.dylib)
endif()
foreach(runtime IN LISTS runtimes)
    file(INSTALL "${build_dir}/${runtime}" DESTINATION "${CURRENT_PACKAGES_DIR}/${destination}")
endforeach()
file(INSTALL "${SOURCE_PATH}/src/qwen.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include/qwentts")

list(TRANSFORM runtimes PREPEND "\${_qwentts_prefix}/${destination}/")
configure_file("${CMAKE_CURRENT_LIST_DIR}/unofficial-qwentts-config.cmake.in"
    "${CURRENT_PACKAGES_DIR}/share/unofficial-qwentts/unofficial-qwentts-config.cmake" @ONLY)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE" "${GGML_PATH}/LICENSE")
