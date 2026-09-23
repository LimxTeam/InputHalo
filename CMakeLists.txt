cmake_minimum_required(VERSION 4.4)
project(InputHalo VERSION 1.0.0 LANGUAGES C CXX RC)

set(IH_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/Generated")
file(MAKE_DIRECTORY "${IH_GENERATED_DIR}")
configure_file(Source/Core/Product/ProductInfo.h.in "${IH_GENERATED_DIR}/ProductInfo.h" @ONLY)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" IH_LICENSE_TEXT)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt" IH_NOTICES_TEXT)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Docs/Privacy.txt" IH_PRIVACY_TEXT)
configure_file(Source/Core/Product/LegalText.h.in "${IH_GENERATED_DIR}/LegalText.h" @ONLY)
configure_file(Source/Platform/Windows/InputHalo.manifest.in "${IH_GENERATED_DIR}/InputHalo.manifest" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS LICENSE THIRD_PARTY_NOTICES.txt Docs/Privacy.txt)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "InputHalo requires Windows x64 and MSVC.")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
option(INPUTHALO_BUILD_TESTS "Build configuration tests" ON)
option(INPUTHALO_ENABLE_VALIDATION "Enable Vulkan validation in Debug builds" ON)

find_package(Vulkan 1.4.357 EXACT REQUIRED)
# Compile the Volk implementation shipped with the pinned SDK using our CRT settings.
if(NOT EXISTS "${Vulkan_INCLUDE_DIR}/Volk/volk.c")
    message(FATAL_ERROR "Vulkan SDK 1.4.357.0 must include Volk sources.")
endif()

include(FetchContent)
set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "Support Unicode Windows log paths" FORCE)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.3 GIT_SHALLOW TRUE)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(spdlog json)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
set(HB_BUILD_RASTER OFF CACHE BOOL "" FORCE)
set(HB_BUILD_VECTOR OFF CACHE BOOL "" FORCE)
set(HB_BUILD_GPU OFF CACHE BOOL "" FORCE)
set(HB_HAVE_FREETYPE OFF CACHE BOOL "" FORCE)
set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(freetype GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-14-3 GIT_SHALLOW TRUE)
FetchContent_Declare(harfbuzz GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    GIT_TAG 14.2.0 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(freetype harfbuzz)
target_compile_options(freetype PRIVATE /utf-8)

add_library(ih_options INTERFACE)
target_include_directories(ih_options INTERFACE "${IH_GENERATED_DIR}")
target_compile_options(ih_options INTERFACE /W4 /permissive- /Zc:__cplusplus /utf-8 /EHsc)
target_compile_definitions(ih_options INTERFACE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

add_library(ih_input STATIC Source/Input/Keys.cpp Source/Input/InputState.cpp)
target_include_directories(ih_input PUBLIC Source)
target_link_libraries(ih_input PUBLIC ih_options)

add_library(ih_settings STATIC
    Source/Settings/SettingsModel.cpp
    Source/Settings/ProfileStore.cpp
    Source/Core/Config/ConfigStore.cpp)
target_include_directories(ih_settings PUBLIC Source)
target_link_libraries(ih_settings PUBLIC ih_options ih_input nlohmann_json::nlohmann_json)

add_library(ih_platform STATIC
    Source/Platform/Windows/Win32Window.cpp
    Source/Platform/Windows/Win32Dpi.cpp
    Source/Platform/Windows/Win32Fonts.cpp
    Source/Platform/Windows/Win32Input.cpp
    Source/Platform/Windows/Win32Monitor.cpp
    Source/Platform/Windows/Win32OverlayWindow.cpp
    Source/Platform/Windows/Win32Compositor.cpp
    Source/Platform/Windows/Win32Paths.cpp)
target_include_directories(ih_platform PUBLIC Source)
target_link_libraries(ih_platform PUBLIC ih_options ih_input PRIVATE user32 shell32 ole32 d3d11 dxgi dcomp wtsapi32)

add_library(ih_render2d STATIC Source/Render2D/Painter.cpp)
target_include_directories(ih_render2d PUBLIC Source)
target_link_libraries(ih_render2d PUBLIC ih_options)
add_library(ih_text STATIC Source/Text/TextEngine.cpp)
target_include_directories(ih_text PUBLIC Source)
target_link_libraries(ih_text PUBLIC ih_options PRIVATE freetype harfbuzz)
add_library(ih_features STATIC Source/Features/OverlayScene.cpp Source/UI/SettingsPanel.cpp Source/UI/AboutPanel.cpp Source/UI/LibraryPanel.cpp)
target_link_libraries(ih_features PUBLIC ih_render2d ih_settings ih_text ih_input)

add_library(ih_vulkan STATIC
    Source/RHI/Vulkan/Volk.cpp
    Source/RHI/Vulkan/VulkanContext.cpp
    Source/RHI/Vulkan/VulkanRenderer2D.cpp
    Source/RHI/Vulkan/VulkanShapeBuffer.cpp
    Source/RHI/Vulkan/VulkanFontAtlas.cpp
    Source/RHI/Vulkan/VulkanCompositionTarget.cpp
    Source/RHI/Vulkan/VulkanSwapchain.cpp)
target_include_directories(ih_vulkan PUBLIC Source "${Vulkan_INCLUDE_DIR}")
target_compile_definitions(ih_vulkan PUBLIC VK_NO_PROTOTYPES VK_USE_PLATFORM_WIN32_KHR
    IH_VULKAN_VALIDATION=$<AND:$<BOOL:${INPUTHALO_ENABLE_VALIDATION}>,$<CONFIG:Debug>>)
target_link_libraries(ih_vulkan PUBLIC ih_options ih_render2d PRIVATE ih_platform spdlog::spdlog)

add_library(ih_overlay STATIC Source/Overlay/OverlayManager.cpp)
target_link_libraries(ih_overlay PUBLIC ih_vulkan ih_platform)

add_executable(InputHalo WIN32
    Source/Platform/Windows/InputHalo.rc
    Source/App/Main.cpp
    Source/App/Application.cpp
    Source/App/PreferenceActions.cpp
    Source/Platform/Windows/Win32Shell.cpp
    Source/Platform/Windows/Win32Preferences.cpp
    Source/Core/Logging/Logging.cpp)
target_link_libraries(InputHalo PRIVATE ih_settings ih_overlay ih_text ih_features spdlog::spdlog advapi32 comdlg32)
target_link_options(InputHalo PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${IH_GENERATED_DIR}/InputHalo.manifest")
add_custom_command(TARGET InputHalo POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:InputHalo>/LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt" "$<TARGET_FILE_DIR:InputHalo>/THIRD_PARTY_NOTICES.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Docs/Privacy.txt" "$<TARGET_FILE_DIR:InputHalo>/PRIVACY.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/licenses" "$<TARGET_FILE_DIR:InputHalo>/Licenses"
    VERBATIM)
set_property(TARGET InputHalo PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

# Milestone 2 feasibility gate. Built explicitly; never run during application startup.
add_executable(InputHaloOverlayProbe EXCLUDE_FROM_ALL
    Tools/Experiments/TransparentOverlay/OverlayProbe.cpp
    Source/Core/Logging/Logging.cpp)
target_link_libraries(InputHaloOverlayProbe PRIVATE ih_platform ih_vulkan
    spdlog::spdlog nlohmann_json::nlohmann_json dwmapi gdi32)

add_executable(InputHaloInteropSpike EXCLUDE_FROM_ALL
    Tools/Experiments/CompositorInterop/Main.cpp
    Tools/Experiments/CompositorInterop/InteropDevice.cpp
    Tools/Experiments/CompositorInterop/InteropTarget.cpp
    Source/Core/Logging/Logging.cpp)
target_link_libraries(InputHaloInteropSpike PRIVATE ih_platform ih_vulkan
    spdlog::spdlog nlohmann_json::nlohmann_json d3d11 dxgi dcomp gdi32)
add_executable(InputHaloInteropBackdrop WIN32 EXCLUDE_FROM_ALL
    Tools/Experiments/CompositorInterop/Backdrop.cpp Source/Core/Logging/Logging.cpp)
target_link_libraries(InputHaloInteropBackdrop PRIVATE ih_platform ih_vulkan spdlog::spdlog)
find_program(INPUTHALO_DXC dxc HINTS "${Vulkan_INCLUDE_DIR}/../Bin" REQUIRED)
set(overlay_shader_output "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Shaders/Renderer2D.vert.spv"
    "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Shaders/Renderer2D.frag.spv")
add_custom_command(OUTPUT ${overlay_shader_output}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Shaders"
    COMMAND "${INPUTHALO_DXC}" -spirv -T vs_6_0 -E vsMain -fspv-target-env=vulkan1.3
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Renderer2D.hlsl" -Fo "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Shaders/Renderer2D.vert.spv"
    COMMAND "${INPUTHALO_DXC}" -spirv -T ps_6_0 -E psMain -fspv-target-env=vulkan1.3
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Renderer2D.hlsl" -Fo "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Shaders/Renderer2D.frag.spv"
    DEPENDS Shaders/Renderer2D.hlsl VERBATIM)
add_custom_target(InputHaloShaders DEPENDS ${overlay_shader_output})
add_dependencies(InputHalo InputHaloShaders)
set(interop_shader_output "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/InteropCircle.spv")
add_custom_command(OUTPUT "${interop_shader_output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
    COMMAND "${INPUTHALO_DXC}" -spirv -T cs_6_0 -E main -fspv-target-env=vulkan1.3
        "${CMAKE_CURRENT_SOURCE_DIR}/Tools/Experiments/CompositorInterop/Circle.hlsl"
        -Fo "${interop_shader_output}"
    DEPENDS Tools/Experiments/CompositorInterop/Circle.hlsl
    VERBATIM)
add_custom_target(InputHaloInteropShaders DEPENDS "${interop_shader_output}")
add_dependencies(InputHaloInteropSpike InputHaloInteropShaders)

if(INPUTHALO_BUILD_TESTS)
    enable_testing()
    add_executable(InputHaloFeatureTests Tests/FeatureTests.cpp)
    target_link_libraries(InputHaloFeatureTests PRIVATE ih_features ih_platform)
    add_test(NAME Features COMMAND InputHaloFeatureTests)
    add_executable(InputHaloTextTests Tests/TextTests.cpp)
    target_link_libraries(InputHaloTextTests PRIVATE ih_text ih_platform ih_render2d)
    add_test(NAME Text COMMAND InputHaloTextTests)
    add_executable(InputHaloRender2DTests Tests/Render2DTests.cpp)
    target_link_libraries(InputHaloRender2DTests PRIVATE ih_render2d)
    add_test(NAME Renderer2D COMMAND InputHaloRender2DTests)
    add_executable(InputHaloRenderer2DSmoke Tests/Renderer2DSmoke.cpp Source/Core/Logging/Logging.cpp)
    target_link_libraries(InputHaloRenderer2DSmoke PRIVATE ih_vulkan ih_platform ih_text spdlog::spdlog)
    add_dependencies(InputHaloRenderer2DSmoke InputHaloShaders)
    add_executable(InputHaloTests Tests/ConfigTests.cpp)
    target_link_libraries(InputHaloTests PRIVATE ih_settings)
    add_test(NAME Config COMMAND InputHaloTests)
    add_executable(InputHaloOverlayAlphaTests Tests/OverlayAlphaTests.cpp)
    target_include_directories(InputHaloOverlayAlphaTests PRIVATE "${Vulkan_INCLUDE_DIR}")
    target_link_libraries(InputHaloOverlayAlphaTests PRIVATE ih_options)
    add_test(NAME OverlayAlphaPolicy COMMAND InputHaloOverlayAlphaTests)
    add_executable(InputHaloVulkanSmoke Tests/VulkanSmoke.cpp Source/Core/Logging/Logging.cpp)
    target_link_libraries(InputHaloVulkanSmoke PRIVATE ih_platform ih_vulkan ih_text spdlog::spdlog)
    add_dependencies(InputHaloVulkanSmoke InputHaloShaders)
    # Hardware-dependent test is intentionally run explicitly, not by the unit-test preset.
    add_executable(InputHaloOverlaySmoke Tests/OverlaySmoke.cpp Source/Core/Logging/Logging.cpp)
    target_link_libraries(InputHaloOverlaySmoke PRIVATE ih_overlay spdlog::spdlog)
    add_dependencies(InputHaloOverlaySmoke InputHaloShaders)
endif()

