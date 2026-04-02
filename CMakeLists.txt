cmake_minimum_required(VERSION 3.22)
project(NADA_BOSS VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# IMPORTANT: Change these paths to match your machine
# ============================================================
set(JUCE_PATH "E:/telechargement/juce-8.0.12-windows/JUCE" CACHE PATH "Path to JUCE")

# ONNX Runtime - set to "OFF" if you don't have it yet
option(USE_ONNX "Enable ONNX Runtime for AI" OFF)
set(ONNX_INCLUDE_DIR "C:/onnxruntime/include" CACHE PATH "ONNX include dir")
set(ONNX_LIB_DIR     "C:/onnxruntime/lib"     CACHE PATH "ONNX lib dir")
# ============================================================

add_subdirectory(${JUCE_PATH} JUCE)

juce_add_plugin(NADA_BOSS
    COMPANY_NAME            "NADA Studio"
    PLUGIN_MANUFACTURER_CODE Nada
    PLUGIN_CODE             NdBs
    FORMATS                 VST3
    PRODUCT_NAME            "NADA BOSS Vocal Suite"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES         "Fx" "EQ" "Dynamics" "Reverb"
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(NADA_BOSS PRIVATE
    Source/dsp/PluginProcessor.cpp
    Source/ui/PluginEditor.cpp
)

target_compile_definitions(NADA_BOSS PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
)

if(USE_ONNX)
    target_compile_definitions(NADA_BOSS PUBLIC USE_ONNX=1)
    target_include_directories(NADA_BOSS PRIVATE ${ONNX_INCLUDE_DIR})
    target_link_directories(NADA_BOSS PRIVATE ${ONNX_LIB_DIR})
    target_link_libraries(NADA_BOSS PRIVATE onnxruntime)
endif()

target_link_libraries(NADA_BOSS
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