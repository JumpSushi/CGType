# Configure with [fxsdk build-fx] or [fxsdk build-cg], which provide the
# toolchain file and module path of the fxSDK

cmake_minimum_required(VERSION 3.15)
project(KeyboardTest)

include(GenerateG1A)
include(GenerateG3A)
include(Fxconv)
find_package(Gint 2.9 REQUIRED)

set(SOURCES
  test-keyboard.c
  usb-hid-kbd.c
)

# No assets needed for this simple test
set(ASSETS
)

fxconv_declare_assets(${ASSETS} WITH_METADATA)

add_executable(kbdtest ${SOURCES} ${ASSETS})
target_compile_options(kbdtest PRIVATE -Wall -Wextra -Os)
target_link_libraries(kbdtest Gint::Gint)

if("${FXSDK_PLATFORM_LONG}" STREQUAL fxCG50)
  generate_g3a(TARGET kbdtest OUTPUT "KbdTest.g3a"
    NAME "USB Keyboard" ICONS assets-cg/icon-uns.png assets-cg/icon-sel.png)
endif()
