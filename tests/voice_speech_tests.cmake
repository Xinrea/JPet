# Include inside the root project's JPET_BUILD_VOICE_TESTS block, after finding
# unofficial-sherpa-onnx-bin. Uses only its headers, never its runtime libraries.
get_filename_component(_jpet_speech_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_executable(voice_speech_test
  "${CMAKE_CURRENT_LIST_DIR}/voice_speech_test.cpp"
  "${_jpet_speech_root}/src/VoiceSpeech.cpp")
target_include_directories(voice_speech_test PRIVATE "${_jpet_speech_root}/src")
target_compile_features(voice_speech_test PRIVATE cxx_std_17)
target_compile_options(voice_speech_test PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
target_link_libraries(voice_speech_test PRIVATE unofficial::sherpa-onnx-bin::headers ${CMAKE_DL_LIBS})
set_target_properties(voice_speech_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")

foreach(_variant IN ITEMS compatible stock wrong_version incomplete)
  add_library(voice_speech_mock_${_variant} SHARED "${CMAKE_CURRENT_LIST_DIR}/voice_speech_mock.cpp")
  target_compile_features(voice_speech_mock_${_variant} PRIVATE cxx_std_17)
  target_compile_options(voice_speech_mock_${_variant} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
  target_link_libraries(voice_speech_mock_${_variant} PRIVATE unofficial::sherpa-onnx-bin::headers)
  set_target_properties(voice_speech_mock_${_variant} PROPERTIES
    # Export only implemented symbols, preserving intentionally incomplete mocks.
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
  add_dependencies(voice_speech_test voice_speech_mock_${_variant})
endforeach()
target_compile_definitions(voice_speech_mock_stock PRIVATE JPET_MOCK_STOCK)
target_compile_definitions(voice_speech_mock_wrong_version PRIVATE JPET_MOCK_WRONG_VERSION)
target_compile_definitions(voice_speech_mock_incomplete PRIVATE JPET_MOCK_INCOMPLETE)

foreach(_scenario IN ITEMS success missing_hotwords empty_hotwords blank_hotwords create_failure
                           stock wrong_version incomplete missing_library)
  if(_scenario STREQUAL "missing_library")
    set(_library "${CMAKE_CURRENT_BINARY_DIR}/tests/absent-sherpa-runtime")
  elseif(_scenario MATCHES "^(stock|wrong_version|incomplete)$")
    set(_library "$<TARGET_FILE:voice_speech_mock_${_scenario}>")
  else()
    set(_library "$<TARGET_FILE:voice_speech_mock_compatible>")
  endif()
  add_test(NAME voice_speech_${_scenario} COMMAND voice_speech_test
    "${_scenario}" "${_library}"
    "${_jpet_speech_root}/resources/models/sensevoice-hotwords.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/tests/voice-speech-fixtures/${_scenario}")
  set_tests_properties(voice_speech_${_scenario} PROPERTIES TIMEOUT 15)
endforeach()
