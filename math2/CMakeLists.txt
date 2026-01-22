# CMakeLists.txt for CGType
# Build with: fxsdk build-cg

cmake_minimum_required(VERSION 3.15)
project(CGType)

include(GenerateG1A)
include(GenerateG3A)
include(Fxconv)
find_package(Gint 2.9 REQUIRED)

set(SOURCES
  math2-editor.c
  math2.c
  usb-hid-kbd.c
)

set(ASSETS
)

fxconv_declare_assets(${ASSETS} WITH_METADATA)

add_executable(cgtype ${SOURCES} ${ASSETS})
target_compile_options(cgtype PRIVATE -Wall -Wextra -Os)
target_link_libraries(cgtype Gint::Gint)

if("${FXSDK_PLATFORM_LONG}" STREQUAL fxCG50)
  generate_g3a(TARGET cgtype OUTPUT "CGType.g3a"
    NAME "CGType" ICONS assets-cg/icon-uns.png assets-cg/icon-sel.png)
endif()
