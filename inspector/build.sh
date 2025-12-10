#!/usr/bin/env bash

SLANG_BUILD_DIR=$(pwd)/slang/build/cmake

function buildSlang() {
    echo "Enter slang build..."
    cd slang
    cmake -DCMAKE_INSTALL_PREFIX=${SLANG_BUILD_DIR} \
        -DSLANG_CMAKECONFIG_INSTALL_DIR=${SLANG_BUILD_DIR} \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -B build -GNinja
    ninja -C build
    cmake --install build
    cd ..
}

function buildInspector() {
    mkdir -p build && cd build
    cmake .. -Dslang_DIR=${SLANG_BUILD_DIR} -DCMAKE_BUILD_TYPE=Release
    cmake --build .
}

buildSlang
buildInspector