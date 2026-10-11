cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")

project(LiveRack VERSION 0.1.0 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: static C runtime, so a shared build runs without installing the
# Visual C++ redistributable first.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(LIVERACK_VST2_SDK_PATH "" CACHE PATH "Folder containing the VST2 SDK (pluginterfaces/vst2.x). Leave empty to disable VST2 hosting.")

add_subdirectory(External/JUCE)

if(LIVERACK_VST2_SDK_PATH)
    juce_set_vst2_sdk_path("${LIVERACK_VST2_SDK_PATH}")
    set(LIVERACK_HOST_VST2 1)
else()
    set(LIVERACK_HOST_VST2 0)
endif()

# ---------------------------------------------------------------------------
# Platform layer (Source/Platform): one implementation per OS, and the audio
# drivers / plugin formats each OS gets.
set(LIVERACK_PLATFORM_SOURCES Source/Platform/Platform.cpp)
if(APPLE)
    list(APPEND LIVERACK_PLATFORM_SOURCES Source/Platform/Platform_mac.mm Source/Platform/Platform_posix.cpp)
    set(LIVERACK_PLATFORM_DEFINITIONS JUCE_PLUGINHOST_AU=1)
elseif(WIN32)
    list(APPEND LIVERACK_PLATFORM_SOURCES Source/Platform/Platform_windows.cpp)
    # ASIO: JUCE 8 bundles the SDK headers. WASAPI exclusive for the rest.
    set(LIVERACK_PLATFORM_DEFINITIONS JUCE_PLUGINHOST_AU=0 JUCE_ASIO=1 JUCE_WASAPI=1 JUCE_DIRECTSOUND=0)
else()
    list(APPEND LIVERACK_PLATFORM_SOURCES Source/Platform/Platform_linux.cpp Source/Platform/Platform_posix.cpp)
    # libjack is loaded at run time: JACK is optional on the target machine.
    set(LIVERACK_PLATFORM_DEFINITIONS JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_LV2=1
        JUCE_ALSA=1 JUCE_JACK=1 JUCE_JACK_CLIENT_NAME="LiveRack")
endif()

# ---------------------------------------------------------------------------
# LiveRackCore: JUCE modules + all app logic, compiled once, shared by the app,
# the scanner and the tests.
add_library(LiveRackCore STATIC
    ${LIVERACK_PLATFORM_SOURCES}
    Source/Core/ShowModel.cpp
    Source/Core/PlanBuilder.cpp
    Source/Core/Watchdog.cpp
    Source/Core/WorkerPool.cpp
    Source/Core/AudioEngine.cpp
    Source/Core/InflightLog.cpp
    Source/Core/PluginLayout.cpp
    Source/Core/PortRef.cpp
    Source/Core/ClockBridge.cpp
    Source/Core/DeviceHub.cpp
    Source/Core/DelayLine.cpp
    Source/Core/PlanDeploy.cpp
    Source/Core/PluginCategory.cpp
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
        ${LIVERACK_PLATFORM_DEFINITIONS}
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
    Source/UI/SettingsWindow.cpp)

target_link_libraries(LiveRack PRIVATE LiveRackCore)

# ---------------------------------------------------------------------------
juce_add_console_app(LiveRackScanner PRODUCT_NAME "LiveRackScanner")
target_sources(LiveRackScanner PRIVATE Scanner/ScannerMain.cpp)
target_link_libraries(LiveRackScanner PRIVATE LiveRackCore)

add_executable(LiveRackGuard Guard/GuardMain.cpp)

# Ship the helpers next to the app executable (re-copied on every build, so a
# helper-only change still reaches the package). macOS: inside the bundle,
# re-signed.
if(APPLE)
    add_custom_target(LiveRackBundle ALL
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:LiveRackScanner> $<TARGET_BUNDLE_CONTENT_DIR:LiveRack>/MacOS/
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:LiveRackGuard> $<TARGET_BUNDLE_CONTENT_DIR:LiveRack>/MacOS/
        COMMAND codesign --force --deep --sign - $<TARGET_BUNDLE_DIR:LiveRack>
        VERBATIM)
else()
    add_custom_target(LiveRackBundle ALL
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:LiveRackScanner> $<TARGET_FILE_DIR:LiveRack>/
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:LiveRackGuard> $<TARGET_FILE_DIR:LiveRack>/
        VERBATIM)
endif()
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
    Tests/PortRefTests.cpp
    Tests/ClockBridgeTests.cpp
    Tests/StressTests.cpp
    Tests/PluginCategoryTests.cpp
    Tests/DeviceNamingTests.cpp)
if(APPLE)
    # Manual hardware/plugin benchmarks, written against this rig's plugins.
    target_sources(LiveRackTests PRIVATE Tests/RealPluginTests.cpp Tests/DeviceBenchTests.cpp)
endif()
target_link_libraries(LiveRackTests PRIVATE LiveRackCore)
