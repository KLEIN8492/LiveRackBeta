cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")

project(LiveRack VERSION 0.1.0 LANGUAGES C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIVERACK_VST2_SDK_PATH "" CACHE PATH "Folder containing the VST2 SDK (pluginterfaces/vst2.x). Leave empty to disable VST2 hosting.")

add_subdirectory(External/JUCE)

if(LIVERACK_VST2_SDK_PATH)
    juce_set_vst2_sdk_path("${LIVERACK_VST2_SDK_PATH}")
    set(LIVERACK_HOST_VST2 1)
else()
    set(LIVERACK_HOST_VST2 0)
endif()

# ---------------------------------------------------------------------------
# LiveRackCore: JUCE modules + all app logic, compiled once, shared by the app,
# the scanner and the tests.
add_library(LiveRackCore STATIC
    Source/Core/ShowModel.cpp
    Source/Core/PlanBuilder.cpp
    Source/Core/Watchdog.cpp
    Source/Core/WorkerPool.cpp
    Source/Core/AudioEngine.cpp
    Source/Core/InflightLog.cpp
    Source/Core/PluginLayout.cpp
    Source/Core/MacActivity.mm
)

target_link_libraries(LiveRackCore
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

target_compile_definitions(LiveRackCore
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST=${LIVERACK_HOST_VST2}
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_APPLICATION_NAME_STRING="LiveRack"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    INTERFACE
        $<TARGET_PROPERTY:LiveRackCore,COMPILE_DEFINITIONS>)

target_include_directories(LiveRackCore
    PUBLIC Source
    INTERFACE $<TARGET_PROPERTY:LiveRackCore,INCLUDE_DIRECTORIES>)

option(LIVERACK_BUILD_APP "Build the GUI app and helpers (off = core + tests only)" ON)

if(LIVERACK_BUILD_APP)
# ---------------------------------------------------------------------------
juce_add_gui_app(LiveRack
    PRODUCT_NAME "LiveRack"
    BUNDLE_ID "com.liverack.app"
    COMPANY_NAME "LiveRack"
    ICON_BIG "${CMAKE_SOURCE_DIR}/Resources/AppIcon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "LiveRack processes live audio inputs through plugins."
    HARDENED_RUNTIME_ENABLED FALSE)

target_sources(LiveRack PRIVATE
    Source/Host/PluginHost.cpp
    Source/Host/OutOfProcessScanner.cpp
    Source/Host/PluginWindow.cpp
    Source/App/Main.cpp
    Source/App/SessionGuard.cpp
    Source/UI/MainComponent.cpp
    Source/UI/TopBar.cpp
    Source/UI/RackList.cpp
    Source/UI/RackView.cpp
    Source/UI/RoutingView.cpp
    Source/UI/PluginBrowser.cpp
    Source/UI/SettingsView.cpp)

target_link_libraries(LiveRack PRIVATE LiveRackCore)

# ---------------------------------------------------------------------------
juce_add_console_app(LiveRackScanner PRODUCT_NAME "LiveRackScanner")
target_sources(LiveRackScanner PRIVATE Scanner/ScannerMain.cpp)
target_link_libraries(LiveRackScanner PRIVATE LiveRackCore)

add_executable(LiveRackGuard Guard/GuardMain.cpp)

# Ship the helpers inside the app bundle (re-copied and re-signed on every build,
# so a helper-only change still reaches the bundle).
add_custom_target(LiveRackBundle ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:LiveRackScanner> $<TARGET_BUNDLE_CONTENT_DIR:LiveRack>/MacOS/
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:LiveRackGuard> $<TARGET_BUNDLE_CONTENT_DIR:LiveRack>/MacOS/
    COMMAND codesign --force --deep --sign - $<TARGET_BUNDLE_DIR:LiveRack>
    VERBATIM)
add_dependencies(LiveRackBundle LiveRack LiveRackScanner LiveRackGuard)
endif()

# ---------------------------------------------------------------------------
juce_add_console_app(LiveRackTests PRODUCT_NAME "LiveRackTests")
target_sources(LiveRackTests PRIVATE
    Tests/TestMain.cpp
    Tests/PlanBuilderTests.cpp
    Tests/WatchdogTests.cpp
    Tests/EngineTests.cpp
    Tests/InflightLogTests.cpp
    Tests/ShowModelTests.cpp
    Tests/RealPluginTests.cpp
    Tests/DeviceBenchTests.cpp)
target_link_libraries(LiveRackTests PRIVATE LiveRackCore)
