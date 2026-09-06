cmake_minimum_required(VERSION 3.21)

project(ThaiKaraokeOverlay VERSION 1.0.0 LANGUAGES CXX)

set(THAI_OVERLAY_SOURCES
    src/main.cpp
    src/app/overlay_application.h
    src/app/overlay_application.cpp
    src/app/settings_window.h
    src/app/settings_window.cpp
    src/app/workspace_window.cpp
    src/services/history_service.cpp
    src/config/app_config.h
    src/config/app_config.cpp
    src/core/models.h
    src/core/text.h
    src/core/text.cpp
    src/services/ocr_service.h
    src/services/ocr_service.cpp
    src/services/translation_service.h
    src/services/translation_service.cpp
)

add_executable(ThaiKaraokeOverlay WIN32 ${THAI_OVERLAY_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src"
             PREFIX "Source"
             FILES ${THAI_OVERLAY_SOURCES})

target_compile_features(ThaiKaraokeOverlay PRIVATE cxx_std_20)
target_include_directories(ThaiKaraokeOverlay PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_definitions(ThaiKaraokeOverlay PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(ThaiKaraokeOverlay PRIVATE
    advapi32
    comctl32
    winhttp
    user32
    gdi32
    shell32
    windowsapp
    ole32
)

add_custom_command(TARGET ThaiKaraokeOverlay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config.example.ini"
            "$<TARGET_FILE_DIR:ThaiKaraokeOverlay>/config.example.ini"
)

if(MSVC)
    target_compile_options(ThaiKaraokeOverlay PRIVATE /W4 /permissive- /utf-8)
endif()

install(TARGETS ThaiKaraokeOverlay RUNTIME DESTINATION .)
install(FILES config.example.ini DESTINATION .)

option(POINT_TRANSLATOR_TESTS "Build service integration tests" OFF)
if(POINT_TRANSLATOR_TESTS)
    add_executable(PointTranslatorServiceTests tests/service_tests.cpp
        src/services/history_service.cpp src/services/translation_service.cpp src/core/text.cpp)
    target_compile_features(PointTranslatorServiceTests PRIVATE cxx_std_20)
    target_include_directories(PointTranslatorServiceTests PRIVATE src)
    target_compile_definitions(PointTranslatorServiceTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(PointTranslatorServiceTests PRIVATE /W4 /utf-8)
    target_link_libraries(PointTranslatorServiceTests PRIVATE windowsapp shell32 ole32 winhttp)
endif()
