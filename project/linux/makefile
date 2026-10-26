ifndef BUILD_DIR

.PHONY: default all clean release test x64 x64-test x64-cache

default all: release

uname_m := $(shell uname -m)
uname_n := $(shell uname -n)
$(info uname -m is $(uname_m))
ifeq ($(uname_m), armv7l)
export GCC_EXTRA_FLAGS := -fno-tree-vectorize
else ifeq ($(uname_m), aarch64)
BOXED_FLAGS := -DASMJIT_STATIC -DBOXEDWINE_64 -DBOXEDWINE_JIT -DBOXEDWINE_JIT_ARMV8 -DSIMDE_NO_NEON -DSIMDE_SSE2_NO_NEON -DBOXEDWINE_MULTI_THREADED -DBOXEDWINE_MEM_CACHE -DBOXEDWINE_HOST_EXCEPTIONS
else ifeq ($(uname_m), i386)
BOXED_FLAGS := -DASMJIT_NO_AARCH64 -DASMJIT_STATIC -DBOXEDWINE_JIT_X86 -DBOXEDWINE_JIT -DBOXEDWINE_MULTI_THREADED
else ifeq ($(uname_m), i686)
BOXED_FLAGS := -DASMJIT_NO_AARCH64 -DASMJIT_STATIC -DBOXEDWINE_JIT_X86 -DBOXEDWINE_JIT -DBOXEDWINE_MULTI_THREADED
else ifeq ($(uname_m), x86_64)
BOXED_FLAGS := -DASMJIT_NO_AARCH64 -DASMJIT_STATIC -DBOXEDWINE_64 -DBOXEDWINE_JIT -DBOXEDWINE_JIT_X64 -DBOXEDWINE_MULTI_THREADED -DBOXEDWINE_MEM_CACHE -DBOXEDWINE_HOST_EXCEPTIONS
endif

release: export BUILD_DIR := Build/Release
release: export EXTRA_CPP_FLAGS := $(BOXED_FLAGS)
test:   export BUILD_DIR := Build/Test
test: export EXTRA_CPP_FLAGS := $(BOXED_FLAGS) -D__TEST
# CPU64/kernel/ELF diagnostics use the interpreter and no native GL/UI.
# Browser graphics retain the WASM build and dedicated bridge harness checks.
x64: export BUILD_DIR := Build/X64Headless
x64: export EXTRA_CPP_FLAGS := -DASMJIT_STATIC -DBOXEDWINE_64 -DBOXEDWINE_GUEST_X64=1 -DBOXEDWINE_MULTI_THREADED -DBOXEDWINE_DISABLE_UI
x64-test: export BUILD_DIR := Build/X64Test
x64-test: export EXTRA_CPP_FLAGS := -DASMJIT_STATIC -DBOXEDWINE_64 -DBOXEDWINE_GUEST_X64=1 -DBOXEDWINE_MULTI_THREADED -DBOXEDWINE_DISABLE_UI -DBOXEDWINE_BLOCK_EXEC=1 -DBOXEDWINE_BLOCK_CACHE_INFRA=1 -D__TEST
x64-cache: export BUILD_DIR := Build/X64HeadlessCache
x64-cache: export EXTRA_CPP_FLAGS := -DASMJIT_STATIC -DBOXEDWINE_64 -DBOXEDWINE_GUEST_X64=1 -DBOXEDWINE_MULTI_THREADED -DBOXEDWINE_DISABLE_UI -DBOXEDWINE_BLOCK_EXEC=1 -DBOXEDWINE_BLOCK_CACHE_INFRA=1

# Respect explicit make -j/MAKEFLAGS. Otherwise cap parallelism to keep the
# native verifier from exhausting RAM on hosts with many logical CPUs.
ifeq ($(filter -j% --jobs%,$(MAKEFLAGS)),)
JOBS ?= 2
MAKEFLAGS += -j$(JOBS)
endif
release test x64 x64-test x64-cache:
	@$(MAKE)

clean:
	$(RM) -r Build

else

TARGET_EXEC ?= boxedwine
PROJDIR := $(realpath $(CURDIR)/../..)
TEST_BUILD_DIR ?= ./build/build_test
$(info MAKEFLAGS $(MAKE_FLAGS))
SOURCES = $(shell find $(PROJDIR)/lib/asmjit/asmjit -type f -name '*.cpp')
SOURCES += $(shell find $(PROJDIR)/source -type d \( -path '$(PROJDIR)/source/test' -o -path '$(PROJDIR)/source/emulation/cpu/tests' \) -prune -o -type f -name '*.cpp' -print)
SOURCES += $(wildcard $(PROJDIR)/platform/linux/*.cpp)
SOURCES += $(wildcard $(PROJDIR)/platform/sdl/*.cpp)
SOURCES += $(wildcard $(PROJDIR)/lib/pugixml/src/*.cpp)
SOURCES += $(PROJDIR)/lib/glew/src/glew.cpp
SOURCES += $(PROJDIR)/lib/imgui/addon/imguitinyfiledialogs.cpp
SOURCES += $(PROJDIR)/lib/imgui/examples/imgui_impl_sdl.cpp
SOURCES += $(PROJDIR)/lib/imgui/examples/imgui_impl_opengl2.cpp
SOURCES += $(PROJDIR)/lib/imgui/imgui_widgets.cpp
SOURCES += $(PROJDIR)/lib/imgui/imgui_draw.cpp
SOURCES += $(PROJDIR)/lib/imgui/imgui.cpp
SOURCES += $(PROJDIR)/lib/tiny-process/process.cpp
SOURCES += $(PROJDIR)/lib/tiny-process/process_unix.cpp
MINIZIP_SOURCES := $(addprefix $(PROJDIR)/lib/zlib/contrib/minizip/,ioapi.c mztools.c unzip.c zip.c)
SOURCES += $(MINIZIP_SOURCES)

SOFT_SOURCES = $(wildcard $(PROJDIR)/lib/softfloat/source/*.c)
SOFT_SOURCES += $(wildcard $(PROJDIR)/lib/softfloat/source/8086-SSE/*.c)

TEST_SOURCES = $(shell find $(PROJDIR)/lib/asmjit/asmjit -type f -name '*.cpp')
TEST_SOURCES += $(wildcard $(PROJDIR)/source/test/*.cpp)
TEST_SOURCES += $(wildcard $(PROJDIR)/source/test/**/*.cpp)
TEST_SOURCES += $(wildcard $(PROJDIR)/source/sdl/*.cpp)
TEST_SOURCES += $(wildcard $(PROJDIR)/source/sdl/**/*.cpp)
TEST_SOURCES += $(wildcard $(PROJDIR)/platform/sdl/*.cpp)
TEST_SOURCES += $(wildcard $(PROJDIR)/platform/linux/*.cpp)
# NB: source/emulation/cpu/tests/* are standalone harnesses with their own main()
# (see scripts/test-jit-lifetime.sh); the emscripten recipe prunes them too
# (scripts/build-jit-wasm.sh). Keep them out of the unified test binary.
TEST_SOURCES += $(shell find $(PROJDIR)/source/emulation -type f -name '*.cpp' -not -path '$(PROJDIR)/source/emulation/cpu/tests/*')
TEST_SOURCES += $(wildcard $(PROJDIR)/source/io/*.cpp)
TEST_SOURCES += $(wildcard $(PROJDIR)/source/kernel/*.cpp)
TEST_SOURCES += $(wildcard $(PROJDIR)/source/kernel/**/*.cpp)
TEST_SOURCES += $(wildcard $(PROJDIR)/source/util/*.cpp)
TEST_SOURCES += $(wildcard $(PROJDIR)/source/opengl/sdl/*.cpp)
# NB: gl64bridge.cpp/vk64bridge.cpp require BOXEDWINE_GUEST_X64 + OpenGL;
# the test build has neither. Stage A tests trap dispatch only.
TEST_SOURCES += $(filter-out $(PROJDIR)/source/opengl/gl64bridge.cpp, $(wildcard $(PROJDIR)/source/opengl/*.cpp))
TEST_SOURCES += $(filter-out $(PROJDIR)/source/vulkan/vk64bridge.cpp, $(wildcard $(PROJDIR)/source/vulkan/*.cpp))
TEST_SOURCES += $(wildcard $(PROJDIR)/source/x11/*.cpp)
TEST_SOURCES += $(wildcard $(PROJDIR)/source/x11wire/*.cpp)
TEST_SOURCES += $(MINIZIP_SOURCES)

ifeq ($(BUILD_DIR), Build/Test)
SRCS := $(TEST_SOURCES)
else ifeq ($(BUILD_DIR), Build/X64Test)
# x64 syscall fixtures need the full native interpreter/kernel source set.
# The generic test bridge stub duplicates msgScratch, which syscall64.cpp owns.
SRCS := $(filter-out $(PROJDIR)/source/test/cpu/test_bridge_stubs.cpp,$(TEST_SOURCES))
else ifneq ($(filter Build/X64Headless Build/X64HeadlessCache,$(BUILD_DIR)),)
SRCS := $(filter-out $(PROJDIR)/source/ui/% $(PROJDIR)/lib/asmjit/% $(PROJDIR)/lib/glew/% $(PROJDIR)/lib/imgui/% $(PROJDIR)/lib/pugixml/% $(PROJDIR)/lib/tiny-process/%,$(SOURCES))
else
SRCS := $(SOURCES)
endif

OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
SOFT_OBJS := $(SOFT_SOURCES:%=$(BUILD_DIR)/%.o)
ALL := $(OBJS)
ALL += $(SOFT_OBJS)
DEPS := $(ALL:.o=.d)

$(OBJS): INCLUDES := -I../../lib/asmjit -I../../include -I../../lib/glew/include -I../../lib/imgui -I../../lib/simde -I../../lib/pugixml/src
$(SOFT_OBJS): INCLUDES := -I../../lib/softfloat/source/include -I../../lib/softfloat/source -I../../lib/softfloat/source/8086-SSE

SDL_CFLAGS = $(shell sdl2-config --cflags)
SDL_LIBS = $(shell sdl2-config --libs)
ifneq ($(filter Build/X64Headless Build/X64HeadlessCache Build/X64Test,$(BUILD_DIR)),)
GRAPHICS_CPP_FLAGS :=
GRAPHICS_LD_FLAGS :=
else
GRAPHICS_CPP_FLAGS := "-DGLH=<SDL_opengl.h>" -DBOXEDWINE_OPENGL_SDL -DBOXEDWINE_OPENGL_IMGUI_V2
GRAPHICS_LD_FLAGS := -lGL
endif
CPPFLAGS ?= -std=c++20 -O2 $(EXTRA_CPP_FLAGS) $(GCC_EXTRA_FLAGS) -Werror=switch -Wall -Wno-invalid-offsetof -Wno-delete-incomplete -Wno-unused-result -Wno-unknown-pragmas -Wno-unused-local-typedefs -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable $(INCLUDES) -DBOXEDWINE_RECORDER -DBOXEDWINE_ZLIB -DBOXEDWINE_HAS_SETJMP -DSDL2=1 $(GRAPHICS_CPP_FLAGS) -DSIMDE_SSE2_NO_NATIVE -DBOXEDWINE_POSIX $(SDL_CFLAGS)
CFLAGS ?= -O2 $(INCLUDES)
LDFLAGS = -L./linux_build/lib -lcurl -lssl -lcrypto -lpthread -lm -lz $(GRAPHICS_LD_FLAGS) -lstdc++ -lstdc++fs $(SDL_LIBS)

#$(TEST_BUILD_DIR)/boxedwineTest: $(TEST_OBJS)
#	$(CC) $(TEST_OBJS) -o $@ $(LDFLAGS)

# Compiler switches can change CPU/FPU layout without touching a header.
# Never mix objects from different ABIs in one Build directory. Keep only the
# current configuration stamp, so switching A -> B -> A also invalidates B.
# Quote each value as shell data (CPPFLAGS contains quotes for GLH).
native_config_quote = $(subst ','"'"',$(1))
NATIVE_CONFIG_KEY := $(shell printf '%s\n' '$(call native_config_quote,$(CC))' '$(call native_config_quote,$(CXX))' '$(call native_config_quote,$(CPPFLAGS))' '$(call native_config_quote,$(CXXFLAGS))' '$(call native_config_quote,$(CFLAGS))' '$(call native_config_quote,$(ASFLAGS))' '$(call native_config_quote,$(LDFLAGS))' | sha256sum | cut -d' ' -f1)
NATIVE_CONFIG_STAMP := $(BUILD_DIR)/.build-config-$(NATIVE_CONFIG_KEY)

$(BUILD_DIR)/$(TARGET_EXEC): $(ALL)
	@$(CC) $(ALL) -o $@ $(LDFLAGS)

# assembly
$(BUILD_DIR)/%.s.o: %.s
	@$(MKDIR_P) $(dir $@)
	@$(AS) $(ASFLAGS) -c $< -o $@

# c source
$(BUILD_DIR)/%.c.o: %.c
	@$(MKDIR_P) $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

# c++ source
$(BUILD_DIR)/%.cpp.o: %.cpp
	@$(MKDIR_P) $(dir $@)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

.PHONY: clean

clean:
	$(RM) -r $(BUILD_DIR)

$(ALL): $(NATIVE_CONFIG_STAMP)

$(NATIVE_CONFIG_STAMP):
	@$(MKDIR_P) $(BUILD_DIR)
	@$(RM) $(BUILD_DIR)/.build-config-*
	@touch $@

-include $(DEPS)

MKDIR_P ?= mkdir -p

endif
