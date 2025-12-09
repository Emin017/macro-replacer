SLANG_BUILD_DIR := $(PWD)/slang/build/cmake

init:
	cd slang && \
	cmake -DCMAKE_INSTALL_PREFIX=$(SLANG_BUILD_DIR) \
		-DSLANG_CMAKECONFIG_INSTALL_DIR=$(SLANG_BUILD_DIR) \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-B build -GNinja && \
	ninja -C build && \
	cmake --install build

compile:
	mkdir -p build && cd build
	cmake .. -Dslang_DIR=$(SLANG_BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build .

.PHONY: init compile
.default: compile