cmake_minimum_required(VERSION 3.16)

project(guideXOSPacManNativeElf LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GUIDEXOS_APP_ARCH "amd64" CACHE STRING "guideXOS app target architecture")
set_property(CACHE GUIDEXOS_APP_ARCH PROPERTY STRINGS amd64)
if(NOT GUIDEXOS_APP_ARCH STREQUAL "amd64")
    message(FATAL_ERROR "The first guideXOS PacMan milestone currently supports amd64 only.")
endif()

set(GUIDEXOS_SERVER_ROOT "D:/dev/guideXOSServer" CACHE PATH "guideXOS Server repository root used for app staging")
set(GUIDEXOS_PACKAGE_ROOT "D:/Apps" CACHE PATH "guideXOS package root scanned by the hosted app registry")
set(GUIDEXOS_SDK_ROOT "${GUIDEXOS_SERVER_ROOT}/sdk")
set(GUIDEXOS_APP_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(GUIDEXOS_LOCAL_BIN_DIR "${GUIDEXOS_APP_ROOT}/bin/${GUIDEXOS_APP_ARCH}")
set(GUIDEXOS_STAGED_APP_DIR "${GUIDEXOS_PACKAGE_ROOT}/PacMan")

set(GUIDEXOS_CLANG "C:/Program Files/LLVM/bin/clang++.exe" CACHE FILEPATH "LLVM clang++ used for the freestanding ELF objects")
set(GUIDEXOS_LLD "C:/Program Files/LLVM/bin/ld.lld.exe" CACHE FILEPATH "LLVM lld used for the freestanding ELF link")
if(NOT EXISTS "${GUIDEXOS_CLANG}" OR NOT EXISTS "${GUIDEXOS_LLD}")
    message(FATAL_ERROR "PacMan requires LLVM clang++ and ld.lld to produce a real Native ELF. Set GUIDEXOS_CLANG and GUIDEXOS_LLD.")
endif()

set(GUIDEXOS_BUILD_OBJ_DIR "${CMAKE_CURRENT_BINARY_DIR}/native-objects")
set(GUIDEXOS_ELF "${GUIDEXOS_LOCAL_BIN_DIR}/pacman.elf")
set(GUIDEXOS_MAIN_OBJECT "${GUIDEXOS_BUILD_OBJ_DIR}/main.o")
set(GUIDEXOS_GAME_OBJECT "${GUIDEXOS_BUILD_OBJ_DIR}/game.o")
set(GUIDEXOS_LEVEL_OBJECT "${GUIDEXOS_BUILD_OBJ_DIR}/level.o")
set(GUIDEXOS_RENDERER_OBJECT "${GUIDEXOS_BUILD_OBJ_DIR}/renderer.o")
set(GUIDEXOS_LOADER_OBJECT "${GUIDEXOS_BUILD_OBJ_DIR}/bitmap_loader.o")
set(GUIDEXOS_OBJECTS "${GUIDEXOS_MAIN_OBJECT}" "${GUIDEXOS_GAME_OBJECT}" "${GUIDEXOS_LEVEL_OBJECT}" "${GUIDEXOS_RENDERER_OBJECT}" "${GUIDEXOS_LOADER_OBJECT}")

add_custom_command(OUTPUT "${GUIDEXOS_MAIN_OBJECT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GUIDEXOS_BUILD_OBJ_DIR}"
    COMMAND "${GUIDEXOS_CLANG}" --target=x86_64-unknown-elf -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -c "${GUIDEXOS_APP_ROOT}/src/main.cpp" -I"${GUIDEXOS_SDK_ROOT}/include" -I"${GUIDEXOS_APP_ROOT}/src" -o "${GUIDEXOS_MAIN_OBJECT}"
    DEPENDS src/main.cpp src/game_types.h src/game_state.h src/game.h src/bitmap_loader.h src/renderer.h
)
add_custom_command(OUTPUT "${GUIDEXOS_GAME_OBJECT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GUIDEXOS_BUILD_OBJ_DIR}"
    COMMAND "${GUIDEXOS_CLANG}" --target=x86_64-unknown-elf -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -c "${GUIDEXOS_APP_ROOT}/src/game.cpp" -I"${GUIDEXOS_SDK_ROOT}/include" -I"${GUIDEXOS_APP_ROOT}/src" -o "${GUIDEXOS_GAME_OBJECT}"
    DEPENDS src/game.cpp src/game.h src/game_state.h src/level.h src/game_types.h
)
add_custom_command(OUTPUT "${GUIDEXOS_LEVEL_OBJECT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GUIDEXOS_BUILD_OBJ_DIR}"
    COMMAND "${GUIDEXOS_CLANG}" --target=x86_64-unknown-elf -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -c "${GUIDEXOS_APP_ROOT}/src/level.cpp" -I"${GUIDEXOS_SDK_ROOT}/include" -I"${GUIDEXOS_APP_ROOT}/src" -o "${GUIDEXOS_LEVEL_OBJECT}"
    DEPENDS src/level.cpp src/level.h src/game_state.h src/game_types.h
)
add_custom_command(OUTPUT "${GUIDEXOS_RENDERER_OBJECT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GUIDEXOS_BUILD_OBJ_DIR}"
    COMMAND "${GUIDEXOS_CLANG}" --target=x86_64-unknown-elf -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -c "${GUIDEXOS_APP_ROOT}/src/renderer.cpp" -I"${GUIDEXOS_SDK_ROOT}/include" -I"${GUIDEXOS_APP_ROOT}/src" -o "${GUIDEXOS_RENDERER_OBJECT}"
    DEPENDS src/renderer.cpp src/game_types.h src/game_state.h src/renderer.h src/level.h
)
add_custom_command(OUTPUT "${GUIDEXOS_LOADER_OBJECT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GUIDEXOS_BUILD_OBJ_DIR}"
    COMMAND "${GUIDEXOS_CLANG}" --target=x86_64-unknown-elf -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -c "${GUIDEXOS_APP_ROOT}/src/bitmap_loader.cpp" -I"${GUIDEXOS_SDK_ROOT}/include" -I"${GUIDEXOS_APP_ROOT}/src" -o "${GUIDEXOS_LOADER_OBJECT}"
    DEPENDS src/bitmap_loader.cpp src/bitmap_loader.h src/game_types.h
)
add_custom_command(OUTPUT "${GUIDEXOS_ELF}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GUIDEXOS_LOCAL_BIN_DIR}"
    COMMAND "${GUIDEXOS_LLD}" -m elf_x86_64 -static -e gx_main ${GUIDEXOS_OBJECTS} -o "${GUIDEXOS_ELF}"
    DEPENDS ${GUIDEXOS_OBJECTS}
    COMMENT "Linking genuine static amd64 Native ELF"
)
add_custom_target(pacman-native ALL DEPENDS "${GUIDEXOS_ELF}")

add_executable(pacman-game-tests tests/game_logic_test.cpp src/game.cpp src/level.cpp)
target_include_directories(pacman-game-tests PRIVATE "${GUIDEXOS_SDK_ROOT}/include" "${GUIDEXOS_APP_ROOT}/src")
set_target_properties(pacman-game-tests PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)

add_custom_command(TARGET pacman-native POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GUIDEXOS_STAGED_APP_DIR}/bin/${GUIDEXOS_APP_ARCH}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GUIDEXOS_STAGED_APP_DIR}/resources"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${GUIDEXOS_ELF}" "${GUIDEXOS_STAGED_APP_DIR}/bin/${GUIDEXOS_APP_ARCH}/pacman.elf"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${GUIDEXOS_APP_ROOT}/app.json" "${GUIDEXOS_STAGED_APP_DIR}/app.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${GUIDEXOS_APP_ROOT}/resources/generated/level1.gximg" "${GUIDEXOS_STAGED_APP_DIR}/resources/level1.gximg"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${GUIDEXOS_APP_ROOT}/resources/generated/pacpics.gximg" "${GUIDEXOS_STAGED_APP_DIR}/resources/pacpics.gximg"
    COMMENT "Staging guideXOS PacMan package under the hosted /Apps package root"
)
