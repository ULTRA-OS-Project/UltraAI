# UltraAI — provider-agnostic AI capability module for ULTRA OS.
# Part of ULTRA OS · MIT license · Cloverleaf UG
cmake_minimum_required(VERSION 3.16)
project(UltraAI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------- options

option(ULTRAAI_BUILD_TESTS   "Build UltraAI unit tests"                       OFF)
option(ULTRAAI_USE_ULTRANET  "Link UltraNet for network adapters (opt-in)"    OFF)
option(ULTRAAI_USE_ULTRAVAULT "Link UltraVault for named-secret lookup"       OFF)

# Adapters are independently opt-in via ULTRAAI_ADAPTER_<NAME>.
option(ULTRAAI_ADAPTER_MOCK  "Build the in-process mock adapters"             ON)
# Future network adapters (require ULTRAAI_USE_ULTRANET):
#   option(ULTRAAI_ADAPTER_ANTHROPIC   ...)
#   option(ULTRAAI_ADAPTER_OPENAI      ...)
#   option(ULTRAAI_ADAPTER_LOCAL_LLAMA ...)

# ---------------------------------------------------------------- library

set(ULTRAAI_SOURCES
    src/UltraAICommon.cpp
    src/FactoryTextLLM.cpp
    src/FactoryEmbeddings.cpp
    src/FactorySpeechToText.cpp
    src/FactoryTextToSpeech.cpp
    src/FactoryImageGen.cpp
    src/FactoryVisionAnalyzer.cpp
    src/FactoryTranslator.cpp
    src/FactoryVideoGen.cpp
    src/FactoryMusicGen.cpp
    src/FactoryCodeAssist.cpp
)

if(ULTRAAI_ADAPTER_MOCK)
    list(APPEND ULTRAAI_SOURCES adapters/mock/MockAdapters.cpp)
else()
    list(APPEND ULTRAAI_SOURCES src/NoBuiltinAdaptersStub.cpp)
endif()

add_library(ultraai STATIC ${ULTRAAI_SOURCES})
add_library(UltraAI::ultraai ALIAS ultraai)

target_include_directories(ultraai
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(ultraai PRIVATE /W4)
else()
    target_compile_options(ultraai PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------- sibling modules
# Integrations no-op gracefully when the targets aren't visible to CMake.

if(ULTRAAI_USE_ULTRANET)
    if(TARGET UltraNet::ultranet)
        target_link_libraries(ultraai PRIVATE UltraNet::ultranet)
        target_compile_definitions(ultraai PRIVATE ULTRAAI_HAS_ULTRANET=1)
        message(STATUS "UltraAI: UltraNet integration enabled")
    else()
        message(STATUS "UltraAI: ULTRAAI_USE_ULTRANET=ON but UltraNet::ultranet "
                       "target not found — continuing without it")
    endif()
endif()

if(ULTRAAI_USE_ULTRAVAULT)
    if(TARGET UltraVault::ultravault)
        target_link_libraries(ultraai PRIVATE UltraVault::ultravault)
        target_compile_definitions(ultraai PRIVATE ULTRAAI_HAS_ULTRAVAULT=1)
        message(STATUS "UltraAI: UltraVault integration enabled")
    else()
        message(STATUS "UltraAI: ULTRAAI_USE_ULTRAVAULT=ON but UltraVault::ultravault "
                       "target not found — continuing without it")
    endif()
endif()

# ---------------------------------------------------------------- tests

if(ULTRAAI_BUILD_TESTS)
    enable_testing()

    function(ultraai_add_test name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE ultraai)
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    ultraai_add_test(ultraai_test_core  tests/TestCore.cpp)
    ultraai_add_test(ultraai_test_text  tests/TestTextCapabilities.cpp)
    ultraai_add_test(ultraai_test_media tests/TestMediaCapabilities.cpp)
endif()

# ---------------------------------------------------------------- install

include(GNUInstallDirs)
install(TARGETS ultraai
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
