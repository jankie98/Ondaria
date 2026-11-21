cmake_minimum_required (VERSION 3.22)

project (Sonora
         VERSION 0.1.0
         DESCRIPTION "DAW open source con session view, arrangement e host plugin multi-formato"
         LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set_property (GLOBAL PROPERTY USE_FOLDERS YES)

if (MSVC)
    set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option (SONORA_BUILD_TESTS "Compila i test automatici" ON)
set (SONORA_VST2_SDK_DIR "" CACHE PATH "Cartella dell'SDK VST2 (opzionale): abilita l'hosting dei plugin VST2")

#==============================================================================
# Dipendenze: JUCE (dentro Tracktion Engine), Tracktion Engine, CLAP
set (TE_ADD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory (external/tracktion_engine/modules/juce)
add_subdirectory (external/tracktion_engine/modules)
add_subdirectory (external/clap EXCLUDE_FROM_ALL)

if (SONORA_VST2_SDK_DIR)
    juce_set_vst2_sdk_path ("${SONORA_VST2_SDK_DIR}")
endif()

#==============================================================================
# Impostazioni comuni a tutti i target che compilano i moduli JUCE.
function (sonora_configure_target target)
    target_compile_features (${target} PRIVATE cxx_std_20)
    set_target_properties (${target} PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)

    target_include_directories (${target} PRIVATE "${CMAKE_SOURCE_DIR}/src")

    target_compile_definitions (${target} PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LV2=1
        $<$<PLATFORM_ID:Linux>:JUCE_PLUGINHOST_LADSPA=1>
        $<$<BOOL:${SONORA_VST2_SDK_DIR}>:JUCE_PLUGINHOST_VST=1>
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
        SONORA_VERSION="${PROJECT_VERSION}")

    target_link_libraries (${target} PRIVATE clap juce::juce_recommended_config_flags)

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries (${target} PRIVATE "-latomic" ${CMAKE_DL_LIBS})
    endif()

    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options (${target} PRIVATE -Wno-unknown-pragmas)
    endif()
endfunction()

# Sorgenti dell'host dei plugin, compilati in ogni target che li usa
# (i moduli JUCE sono librerie INTERFACE e vanno compilati per target).
set (SONORA_HOST_SOURCES
    src/host/ClapPluginFormat.cpp
    src/host/ClapPluginFormat.h
    src/host/PluginFormats.cpp
    src/host/PluginFormats.h
    src/host/IsolatedPluginScanner.cpp
    src/host/IsolatedPluginScanner.h
    src/host/ScanProtocol.h)
list (TRANSFORM SONORA_HOST_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/" OUTPUT_VARIABLE SONORA_HOST_SOURCES_ABS)

# Logica della session view (senza interfaccia), usata dall'app e dai test.
set (SONORA_SESSION_SOURCES
    src/session/Arrangement.cpp
    src/session/Arrangement.h
    src/session/Notes.cpp
    src/session/Notes.h
    src/session/Recording.cpp
    src/session/Recording.h
    src/session/Session.cpp
    src/session/Session.h)
list (TRANSFORM SONORA_SESSION_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/" OUTPUT_VARIABLE SONORA_SESSION_SOURCES_ABS)

# Caratteri dell'interfaccia (SIL Open Font License, vedi resources/fonts).
juce_add_binary_data (SonoraFonts
    HEADER_NAME SonoraFonts.h
    NAMESPACE SonoraFonts
    SOURCES
        resources/fonts/Inter-Regular.ttf
        resources/fonts/Inter-Medium.ttf
        resources/fonts/Inter-SemiBold.ttf
        resources/fonts/JetBrainsMono-Regular.ttf)
set_target_properties (SonoraFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

#==============================================================================
# Scanner dei plugin: processo separato che carica un solo file di plugin.
juce_add_console_app (sonora-plugin-scanner PRODUCT_NAME "sonora-plugin-scanner")
target_sources (sonora-plugin-scanner PRIVATE src/scanner/ScannerMain.cpp ${SONORA_HOST_SOURCES})
sonora_configure_target (sonora-plugin-scanner)
target_compile_definitions (sonora-plugin-scanner PRIVATE JUCE_GUI_BASICS_INCLUDE_XHEADERS=1)
target_link_libraries (sonora-plugin-scanner PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils)

#==============================================================================
# Applicazione principale
juce_add_gui_app (Sonora
    PRODUCT_NAME "Sonora"
    COMPANY_NAME "Sonora"
    BUNDLE_ID "org.sonora.daw"
    MICROPHONE_PERMISSION_ENABLED TRUE
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

target_sources (Sonora PRIVATE
    src/app/ArrangementView.cpp
    src/app/ArrangementView.h
    src/app/Main.cpp
    src/app/MainComponent.cpp
    src/app/MainComponent.h
    src/app/PianoRoll.cpp
    src/app/PianoRoll.h
    src/app/CommandPalette.cpp
    src/app/CommandPalette.h
    src/app/DeviceChainComponent.cpp
    src/app/DeviceChainComponent.h
    src/app/PluginBrowserComponent.cpp
    src/app/PluginBrowserComponent.h
    src/app/PluginWindow.cpp
    src/app/PluginWindow.h
    src/app/SessionView.cpp
    src/app/SessionView.h
    src/app/SonoraBehaviour.h
    src/app/Theme.cpp
    src/app/Theme.h
    src/app/TopBar.cpp
    src/app/TopBar.h
    src/app/Widgets.cpp
    src/app/Widgets.h
    ${SONORA_SESSION_SOURCES}
    ${SONORA_HOST_SOURCES})

sonora_configure_target (Sonora)

target_link_libraries (Sonora PRIVATE
    SonoraFonts
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_warning_flags)

# Lo scanner viene copiato accanto all'eseguibile dell'app.
add_dependencies (Sonora sonora-plugin-scanner)
add_custom_command (TARGET Sonora POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:sonora-plugin-scanner>"
            "$<TARGET_FILE_DIR:Sonora>")

#==============================================================================
if (SONORA_BUILD_TESTS)
    enable_testing()
    add_subdirectory (tests)
endif()
