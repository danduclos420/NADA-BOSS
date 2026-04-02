cmake_minimum_required(VERSION 3.22)
project(NADABOSS VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to JUCE
set(JUCE_PATH "C:/JUCE" CACHE PATH "Path to JUCE")

# Optional ONNX Runtime
option(USE_ONNX "Enable ONNX Runtime for AI" OFF)
set(ONNX_INCLUDE_DIR "C:/onnxruntime/include" CACHE PATH "ONNX include dir")
set(ONNX_LIB_DIR     "C:/onnxruntime/lib"     CACHE PATH "ONNX lib dir")

add_subdirectory(${JUCE_PATH} JUCE)

juce_add_plugin(NADABOSS
    COMPANY_NAME "NADA Studio"
    PLUGIN_MANUFACTURER_CODE Nada
    PLUGIN_CODE NdBs
    FORMATS VST3
    PRODUCT_NAME "NADA BOSS Vocal Suite"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES "Fx" "EQ" "Dynamics" "Reverb"
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(NADABOSS PRIVATE
    Source/dsp/PluginProcessor.cpp
    Source/ui/PluginEditor.cpp
)

target_compile_definitions(NADABOSS PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
)

if(USE_ONNX)
    target_compile_definitions(NADABOSS PUBLIC USE_ONNX=1)
    target_include_directories(NADABOSS PRIVATE ${ONNX_INCLUDE_DIR})
    target_link_directories(NADABOSS PRIVATE ${ONNX_LIB_DIR})
    target_link_libraries(NADABOSS PRIVATE onnxruntime)
endif()

target_link_libraries(NADABOSS
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_graphics
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)