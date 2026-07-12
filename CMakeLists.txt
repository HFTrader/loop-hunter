# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Henrique Bucher
cmake_minimum_required(VERSION 3.20)
project(LoopHunter LANGUAGES CXX)

# IMPORTANT: an LLVM pass plugin must be built against the SAME LLVM release as
# the clang/opt that will load it (no cross-version ABI stability). Point CMake
# at that LLVM:
#   cmake -DLLVM_DIR=$(llvm-config-20 --cmakedir) -B build
find_package(LLVM REQUIRED CONFIG)

if (LLVM_PACKAGE_VERSION VERSION_LESS 16.0)
  message(FATAL_ERROR
    "LoopHunter needs LLVM >= 16 (CycleInfo/GenericCycleInfo); found ${LLVM_PACKAGE_VERSION}")
endif()
message(STATUS "LoopHunter: building against LLVM ${LLVM_PACKAGE_VERSION} (${LLVM_DIR})")

add_library(LoopHunter MODULE LoopHunter.cpp)

target_include_directories(LoopHunter SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(LoopHunter PRIVATE ${LLVM_DEFINITIONS})
target_compile_features(LoopHunter PRIVATE cxx_std_17)
set_target_properties(LoopHunter PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
  COMPILE_FLAGS "-fno-rtti"
  # match libLoopHunter.so naming regardless of platform prefix
  PREFIX "lib")

# Plugins don't link the LLVM libs; opt/clang already provide them.
if (APPLE)
  set_target_properties(LoopHunter PROPERTIES
    LINK_FLAGS "-undefined dynamic_lookup")
endif()

install(TARGETS LoopHunter LIBRARY DESTINATION lib)
install(FILES scan-codebase.py
        DESTINATION bin
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
