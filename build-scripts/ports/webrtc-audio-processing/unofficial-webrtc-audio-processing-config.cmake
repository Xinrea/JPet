include(CMakeFindDependencyMacro)
find_dependency(absl CONFIG)

if(NOT TARGET unofficial::webrtc-audio-processing::webrtc-audio-processing)
  get_filename_component(_wap_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
  find_library(_wap_release NAMES webrtc-audio-processing-2 libwebrtc-audio-processing-2
      PATHS "${_wap_prefix}/lib" NO_DEFAULT_PATH REQUIRED)
  add_library(unofficial::webrtc-audio-processing::webrtc-audio-processing STATIC IMPORTED)
  set_target_properties(unofficial::webrtc-audio-processing::webrtc-audio-processing PROPERTIES
      IMPORTED_LOCATION "${_wap_release}"
      INTERFACE_INCLUDE_DIRECTORIES "${_wap_prefix}/include/webrtc-audio-processing-2")
  if(WIN32)
    set_property(TARGET unofficial::webrtc-audio-processing::webrtc-audio-processing APPEND PROPERTY
        INTERFACE_COMPILE_DEFINITIONS WEBRTC_WIN)
    set_property(TARGET unofficial::webrtc-audio-processing::webrtc-audio-processing APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES winmm)
  else()
    set_property(TARGET unofficial::webrtc-audio-processing::webrtc-audio-processing APPEND PROPERTY
        INTERFACE_COMPILE_DEFINITIONS WEBRTC_POSIX)
  endif()
  set_property(TARGET unofficial::webrtc-audio-processing::webrtc-audio-processing APPEND PROPERTY
      INTERFACE_LINK_LIBRARIES absl::base absl::flags absl::strings absl::numeric
      absl::synchronization absl::bad_optional_access)
  unset(_wap_release CACHE)
  unset(_wap_prefix)
endif()
