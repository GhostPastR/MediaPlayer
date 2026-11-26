cmake_minimum_required(VERSION 3.21)

project(Playrer LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt5 COMPONENTS Widgets Multimedia REQUIRED)

add_executable(Playrer
    main.cpp
    widget.cpp
    widget.h
    videowidget.cpp
    videowidget.h
    decoderworker.cpp
    decoderworker.h
    seekslider.cpp
    seekslider.h
    videodecoder.cpp
    videodecoder.h
    packetqueue.cpp
    packetqueue.h
    pausedecoder.cpp
    pausedecoder.h
    audiodecoder.h
    audiodecoder.cpp
    mediaqueue.cpp
    mediaqueue.h
    playerthread.cpp
    playerthread.h
    mediastruct.h
    miniaudio.c
    audiodevice.cpp
    audiodevice.h
    mediaclock.h
    mediaclock.cpp
    rubberbandprocessor.cpp
    rubberbandprocessor.h
    barrier.h
    barrier.cpp
    widget.ui

    ../rubberband-default/single/RubberBandSingle.cpp
)

target_compile_options(Playrer PRIVATE
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-O0;-g>
)

target_link_libraries(Playrer PRIVATE Qt5::Widgets Qt5::Multimedia)

set(FFMPEG_ROOT "C:/ffmpeg")
target_include_directories(Playrer PRIVATE "${FFMPEG_ROOT}/include")
target_link_directories(Playrer PRIVATE "${FFMPEG_ROOT}/lib/x86_64")
target_link_libraries(Playrer PRIVATE
    avformat
    avcodec
    avutil
    swscale
    swresample
    avfilter
)

set(RUBBERBAND_ROOT "../rubberband-default")
target_include_directories(Playrer PRIVATE "${RUBBERBAND_ROOT}/rubberband")

set_target_properties(Playrer PROPERTIES
    WIN32_EXECUTABLE $<IF:$<CONFIG:Debug>,FALSE,TRUE>
)

#if(WIN32)
#    get_target_property(_qmake Qt5::qmake IMPORTED_LOCATION)
#    get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
#    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin}")
#    message(STATUS "windeployqt: ${WINDEPLOYQT_EXECUTABLE}")

#    set(DEPLOY_DIR "${CMAKE_BINARY_DIR}/deploy")

#    if(WINDEPLOYQT_EXECUTABLE)
#        add_custom_command(TARGET Playrer POST_BUILD
#            COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}"
#            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --dir "${DEPLOY_DIR}" "$<TARGET_FILE:Playrer>"
#            COMMENT "windeployqt -> ${DEPLOY_DIR}"
#            VERBATIM
#        )
#    endif()

#    if(EXISTS "${FFMPEG_ROOT}/bin")
#        add_custom_command(TARGET Playrer POST_BUILD
#            COMMAND ${CMAKE_COMMAND} -E copy_directory
#                "${FFMPEG_ROOT}/bin" "${DEPLOY_DIR}"
#            COMMENT "FFmpeg DLLs -> ${DEPLOY_DIR}"
#        )
#    endif()
#endif()

add_compile_definitions(__STDC_CONSTANT_MACROS)
