# Plugin CLAP di prova (librerie dinamiche con estensione .clap)
foreach (name TestGainPlugin CrashingPlugin)
    add_library (${name} MODULE plugins/${name}.cpp)
    target_link_libraries (${name} PRIVATE clap)
    set_target_properties (${name} PROPERTIES PREFIX "" SUFFIX ".clap"
                           CXX_VISIBILITY_PRESET hidden
                           LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/clap-plugins")
endforeach()

juce_add_console_app (SonoraHostTests PRODUCT_NAME "SonoraHostTests")
target_sources (SonoraHostTests PRIVATE HostTests.cpp ${SONORA_HOST_SOURCES_ABS})
sonora_configure_target (SonoraHostTests)
target_link_libraries (SonoraHostTests PRIVATE juce::juce_audio_processors juce::juce_audio_utils)
target_compile_definitions (SonoraHostTests PRIVATE
    JUCE_UNIT_TESTS=1
    SONORA_TEST_GAIN_PLUGIN="$<TARGET_FILE:TestGainPlugin>"
    SONORA_TEST_CRASH_PLUGIN="$<TARGET_FILE:CrashingPlugin>"
    SONORA_SCANNER_PATH="$<TARGET_FILE:sonora-plugin-scanner>")
add_dependencies (SonoraHostTests TestGainPlugin CrashingPlugin sonora-plugin-scanner)

add_test (NAME SonoraHostTests COMMAND SonoraHostTests)

# Test di integrazione con Tracktion Engine
juce_add_console_app (SonoraEngineTests PRODUCT_NAME "SonoraEngineTests")
target_sources (SonoraEngineTests PRIVATE EngineTests.cpp SessionTests.cpp NotesTests.cpp ArrangementTests.cpp RecordingTests.cpp CaptureTests.cpp ${SONORA_SESSION_SOURCES_ABS} ${SONORA_HOST_SOURCES_ABS})
sonora_configure_target (SonoraEngineTests)
target_link_libraries (SonoraEngineTests PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils)
target_compile_definitions (SonoraEngineTests PRIVATE
    JUCE_UNIT_TESTS=1
    SONORA_TEST_GAIN_PLUGIN="$<TARGET_FILE:TestGainPlugin>"
    SONORA_TEST_CRASH_PLUGIN="$<TARGET_FILE:CrashingPlugin>"
    SONORA_SCANNER_PATH="$<TARGET_FILE:sonora-plugin-scanner>")
add_dependencies (SonoraEngineTests TestGainPlugin CrashingPlugin sonora-plugin-scanner)

add_test (NAME SonoraEngineTests COMMAND SonoraEngineTests)
