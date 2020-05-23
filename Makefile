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

BUILD_DIR=build
BUILD_TARGET=engine
BUILD_LIBRARY_TARGET=game
BUILD_CONFIG=debug

CXXFLAGS=-Wall -std=c++11 -fno-rtti -fno-exceptions -Isource -Isource/game

# Build flags
ifeq ($(BUILD_CONFIG),debug)
  # Debug flags
  CXXFLAGS+=--debug -DBUILD_DEBUG=1

  # ASAN
  CXXFLAGS+=-fsanitize=address -fno-omit-frame-pointer -D_REENTRANT
else ifeq ($(BUILD_CONFIG),release)
  CXXFLAGS+=-O3
else
  $(error unknown build config $(BUILD_CONFIG))
endif

# Unity build
LIBRARY_FILES=source/game/game.h source/game/game.cc
LIBRARY_BUILD_MAIN=source/game/game.cc

ifeq ($(OS),linux)
  GAME_EXECUTABLE=$(BUILD_DIR)/$(BUILD_TARGET)
  GAME_LIBRARY=$(BUILD_DIR)/lib$(BUILD_LIBRARY_TARGET).so
  PLATFORM_BUILD_MAIN=source/platform/linux/linux_main.cc
  LDFLAGS+=-ldl $(shell pkg-config x11 --libs) $(shell pkg-config xinerama --libs) $(shell pkg-config gl --libs) $(shell pkg-config glx --libs)
  CXXFLAGS_LIBRARY=-shared -fPIC
else ifeq ($(OS),macos)
  GAME_EXECUTABLE=$(BUILD_DIR)/$(BUILD_TARGET)
  GAME_LIBRARY=$(BUILD_DIR)/lib$(BUILD_LIBRARY_TARGET).dylib
  PLATFORM_BUILD_MAIN=source/platform/macos/macos_main.cc
  CXXFLAGS_LIBRARY=-dynamiclib
else ifeq ($(OS),win)
  GAME_EXECUTABLE=$(BUILD_DIR)/$(BUILD_TARGET)
  GAME_LIBRARY=$(BUILD_DIR)/$(BUILD_LIBRARY_TARGET).dll
  PLATFORM_BUILD_MAIN=source/platform/windows/windows_main.cc
else
  $(error unsupported os $(OS))
endif

all: $(GAME_LIBRARY) $(GAME_EXECUTABLE)

$(GAME_EXECUTABLE): build $(PLATFORM_BUILD_MAIN)
	@$(CXX) $(CXXFLAGS) -o $(GAME_EXECUTABLE) $(PLATFORM_BUILD_MAIN) $(LDFLAGS)

$(GAME_LIBRARY): build $(LIBRARY_BUILD_MAIN) $(LIBRARY_FILES)
	@$(CXX) $(CXXFLAGS) $(CXXFLAGS_LIBRARY) -o $(GAME_LIBRARY) $(LIBRARY_BUILD_MAIN) $(LDFLAGS)

run/game: $(GAME_LIBRARY) $(GAME_EXECUTABLE)
	cd $(BUILD_DIR) && ./$(BUILD_TARGET)

build:
	@mkdir -p build

clean:
	rm -rf $(GAME_EXECUTABLE)
	rm -rf $(GAME_LIBRARY)
