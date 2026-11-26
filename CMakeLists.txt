cmake_minimum_required(VERSION 3.22)
project(SwellFET76 VERSION 0.2.0 LANGUAGES C CXX)

# Swell FET 76 : compresseur analogique. Interface entièrement rendue par le code (textures procédurales,
# ombres, verre, aiguille VU physique), aucune image bitmap ni SVG.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
# Pour compiler hors ligne avec une copie locale : -DFETCHCONTENT_SOURCE_DIR_JUCE=deps/juce
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 9.0.5 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
# Monocypher (BSD-2 / CC0) : vérification Ed25519 des fichiers de licence Swell.
# Pas de CMakeLists chez Monocypher : SOURCE_SUBDIR inexistant = télécharger seulement, la cible est déclarée ici.
FetchContent_Declare(monocypher GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git GIT_TAG 4.0.3 GIT_SHALLOW TRUE SOURCE_SUBDIR none)
FetchContent_MakeAvailable(monocypher)
add_library(monocypher STATIC "${monocypher_SOURCE_DIR}/src/monocypher.c" "${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c")
target_include_directories(monocypher PUBLIC "${monocypher_SOURCE_DIR}/src" "${monocypher_SOURCE_DIR}/src/optional")
set_target_properties(monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(COMP_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND COMP_FORMATS AU)
endif()

set(COMMON_SOURCES
  Source/Common/AnalogLook.cpp
  Source/Common/AnalogComponents.cpp
  Source/Common/AnalogProcessorBase.cpp
  Source/Common/PresetBar.cpp
  Source/Licence/SwellLicence.cpp)

function(swell_comp target code product bundle icon)
  # Ne jamais changer PLUGIN_CODE / BUNDLE_ID après la sortie : les sessions perdraient le plugin.
  juce_add_plugin(${target}
    COMPANY_NAME "Swell-audio" BUNDLE_ID "fr.swell-audio.${bundle}"
    PLUGIN_MANUFACTURER_CODE Swel PLUGIN_CODE ${code}
    FORMATS ${COMP_FORMATS} PRODUCT_NAME "${product}" ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/${icon}"
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
  target_compile_definitions(${target} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
  target_include_directories(${target} PRIVATE Source)
  target_link_libraries(${target}
    PRIVATE monocypher juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endfunction()

swell_comp(SwellFET76 Sf76 "Swell FET 76" fet76 fet76-icon.png)
target_sources(SwellFET76 PRIVATE ${COMMON_SOURCES} Source/Fet/FetProcessor.cpp Source/Fet/FetEditor.cpp)

# Outils : tests audio et capture de l'interface (rendu hors ligne en PNG).
enable_testing()
juce_add_console_app(SnapshotFET76 PRODUCT_NAME "Snapshot FET76")
target_sources(SnapshotFET76 PRIVATE Tools/Snapshot.cpp)
juce_add_console_app(TestFET76 PRODUCT_NAME "Test FET76")
target_sources(TestFET76 PRIVATE Tests/AudioTest.cpp)
juce_add_console_app(PresetTestFET76 PRODUCT_NAME "Preset Test FET76")
target_sources(PresetTestFET76 PRIVATE Tests/PresetTest.cpp)
foreach(tool SnapshotFET76 TestFET76 PresetTestFET76)
  target_include_directories(${tool} PRIVATE "${juce_SOURCE_DIR}/modules" Source)
  target_compile_definitions(${tool} PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
  target_link_libraries(${tool} PRIVATE SwellFET76)
endforeach()
add_test(NAME AudioFET76 COMMAND TestFET76)
# Presets d'usine : chacun tient son cahier des charges (niveau, réduction, caractère) sur VOX.wav et des sources synthétiques.
add_test(NAME PresetsFET76 COMMAND PresetTestFET76 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
