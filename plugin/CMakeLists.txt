cmake_minimum_required(VERSION 3.22)
project(GigmaPlugin VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GIGMA_HAS_AOO "Link against AOO + Opus for network audio" OFF)

if(NOT DEFINED JUCE_PATH)
  message(FATAL_ERROR "Set -DJUCE_PATH=/path/to/JUCE")
endif()

add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(GigmaPlugin
  COMPANY_NAME "Veritas Studio"
  BUNDLE_ID "in.veritas.gigma"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD TRUE
  PLUGIN_MANUFACTURER_CODE Vrts
  PLUGIN_CODE Gigm
  FORMATS AU VST3
  PRODUCT_NAME "Gigma"
)

target_sources(GigmaPlugin
  PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/GigmaSessionClient.cpp
    Source/GigmaAooEngine.cpp
)

target_compile_definitions(GigmaPlugin
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    GIGMA_DEFAULT_BROKER_URL="https://gigma.veritas.org.in"
    $<IF:$<BOOL:${GIGMA_HAS_AOO}>,GIGMA_HAS_AOO=1,GIGMA_HAS_AOO=0>
)

if(GIGMA_HAS_AOO)
  if(NOT DEFINED AOO_PATH)
    message(FATAL_ERROR "Set -DAOO_PATH=/path/to/aoo (upstream https://git.iem.at/aoo/aoo)")
  endif()
  target_include_directories(GigmaPlugin PRIVATE ${AOO_PATH}/include ${AOO_PATH}/lib)
  target_link_libraries(GigmaPlugin PRIVATE aoo opus)
endif()

target_link_libraries(GigmaPlugin
  PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
