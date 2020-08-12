.PHONY=run/game clean

default: all

# Pick one of:
#   linux
#   macos
#   windows

UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  OS=macos
  BUILDFLAGS=-DPLATFORM_MACOS
else ifeq ($(UNAME),Linux)
  OS=linux
  BUILDFLAGS=-DPLATFORM_LINUX
else ifeq (,$(findstring MINGW32_NT,$(UNAME)))
  OS=win
  BUILDFLAGS=-DPLATFORM_WIN
else
  $(error unknown os $(UNAME))
endif

# Build information
CXX=clang
LIBFREETYPE_VERSION=2.10.2

BUILD_DIR=build
BUILD_TARGET=engine
BUILD_LIBRARY_TARGET=game
BUILD_CONFIG=debug

CXXFLAGS=-std=c++11 -Wall -Wextra -Wno-unknown-pragmas -Wno-unused-variable -Wno-writable-strings -Wno-unused-parameter -Wno-unused-function -fno-rtti -fno-exceptions -Isource -Isource/game -Ithird-party/freetype-$(LIBFREETYPE_VERSION)/include -Lthird-party/freetype-$(LIBFREETYPE_VERSION)/objs $(BUILDFLAGS)

# Build flags
ifeq ($(BUILD_CONFIG),debug)
  # Debug flags
  CXXFLAGS+=--debug -DBUILD_DEBUG=1

  # ASAN (NOTE: Allocates 20.0 TB of VMEM so will obscure actual memory usage)
  CXXFLAGS+=-fsanitize=address -fno-omit-frame-pointer -D_REENTRANT
else ifeq ($(BUILD_CONFIG),release)
  CXXFLAGS+=-O3
else
  $(error unknown build config $(BUILD_CONFIG))
endif

# Unity build
LIBRARY_FILES=source/common/language_layer.h \
	source/common/memory_arena.h \
	source/common/watched_file.h \
	source/game/game.h \
	source/game/game.cc \
	source/game/renderer.h \
	source/game/renderer.cc \
	source/game/opengl_procedure_list.h \
	source/game/shaders.h \
        source/game/shaders.cc \
        source/game/textures.h \
        source/game/textures.cc \
        source/game/fonts.h \
        source/game/fonts.cc \
        source/game/sounds.h \
        source/game/sounds.cc \
	source/game/mixer.h \
        source/game/mixer.cc \
        source/game/ui/ui.h \
        source/game/ui/ui.cc \
	source/game/ui/debug_console.h \
        source/game/ui/debug_console.cc

LIBRARY_BUILD_MAIN=source/game/game.cc

ifeq ($(OS),linux)
  GAME_EXECUTABLE=$(BUILD_DIR)/$(BUILD_TARGET)
  GAME_LIBRARY=$(BUILD_DIR)/lib$(BUILD_LIBRARY_TARGET).so
  PLATFORM_BUILD_MAIN=source/platform/linux/linux_main.cc
  # X11 (Window Manager)
  # OpenGL (Hardware-accelerated Graphics)
  # ALSA (Audio)
  LDFLAGS+=-lm -ldl -lfreetype -lpthread \
	$(shell pkg-config x11 --libs) $(shell pkg-config xinerama --libs) \
	$(shell pkg-config gl --libs) $(shell pkg-config glu --libs) $(shell pkg-config glx --libs) \
	$(shell pkg-config alsa --libs)
  CXXFLAGS_LIBRARY=-shared -fPIC
else ifeq ($(OS),macos)
  GAME_EXECUTABLE=$(BUILD_DIR)/$(BUILD_TARGET)
  GAME_LIBRARY=$(BUILD_DIR)/lib$(BUILD_LIBRARY_TARGET).dylib
  PLATFORM_BUILD_MAIN=source/platform/macos/macos_main.m
  LDFLAGS+=-ldl -framework Cocoa -framework OpenGL -framework CoreAudio
  CXXFLAGS_LIBRARY=-dynamiclib
else ifeq ($(OS),win)
  GAME_EXECUTABLE=$(BUILD_DIR)/$(BUILD_TARGET)
  GAME_LIBRARY=$(BUILD_DIR)/$(BUILD_LIBRARY_TARGET).dll
  PLATFORM_BUILD_MAIN=source/platform/windows/windows_main.cc
else
  $(error unsupported os $(OS))
endif

all: $(GAME_LIBRARY) $(GAME_EXECUTABLE)

$(GAME_EXECUTABLE): build $(PLATFORM_BUILD_MAIN) source/platform/linux/linux_audio.cc
	@echo "Building Platform..."
	@$(CXX) $(CXXFLAGS) -o $(GAME_EXECUTABLE) $(PLATFORM_BUILD_MAIN) $(LDFLAGS)

$(GAME_LIBRARY): build $(LIBRARY_BUILD_MAIN) $(LIBRARY_FILES)
	@echo "Creating Lockfile"
	@touch $(BUILD_DIR)/build.lock
	@echo "Building Game..."
	@$(CXX) $(CXXFLAGS) $(CXXFLAGS_LIBRARY) -o $(GAME_LIBRARY) $(LIBRARY_BUILD_MAIN) $(LDFLAGS)
	@echo "Removing Lockfile."
	@rm -rf $(BUILD_DIR)/build.lock

run/game: $(GAME_LIBRARY) $(GAME_EXECUTABLE) $(PLATFORM_BUILD_MAIN) $(LIBRARY_FILES)
	cd $(BUILD_DIR) && LSAN_OPTIONS=suppressions=../linux_lsan_suppressions.supp ./$(BUILD_TARGET)

build:
	@mkdir -p build

clean:
	rm -rf $(GAME_EXECUTABLE)
	rm -rf $(GAME_LIBRARY)
