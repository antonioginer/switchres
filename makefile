PLATFORM := $(shell uname)

MAIN = switchres_main
STANDALONE = switchres
TARGET_LIB = libswitchres
DRMHOOK_LIB = libdrmhook
GRID = grid
SRC = monitor.cpp modeline.cpp switchres.cpp display.cpp custom_video.cpp log.cpp switchres_wrapper.cpp edid.cpp
OBJS = $(SRC:.cpp=.o)

CROSS_COMPILE ?=
CXX ?= g++
AR ?= ar
LDFLAGS = -shared
FINAL_CXX=$(CROSS_COMPILE)$(CXX)
FINAL_AR=$(CROSS_COMPILE)$(AR)
CPPFLAGS = -O3 -Wall -Wextra

PKG_CONFIG=pkg-config
INSTALL=install
LN=ln

DESTDIR ?=
PREFIX ?= /usr
INCDIR = $(DESTDIR)$(PREFIX)/include
LIBDIR = $(DESTDIR)$(PREFIX)/lib
BINDIR = $(DESTDIR)$(PREFIX)/bin
PKGDIR = $(LIBDIR)/pkgconfig

ifneq ($(DEBUG),)
    CPPFLAGS += -g
endif

# If the version is not set at make, read it from switchres.h
ifeq ($(VERSION),)
    VERSION:=$(shell grep -E "^\#define SWITCHRES_VERSION" switchres.h | grep -oE "[0-9]+\.[0-9]+\.[0-9]+" )
else
    CPPFLAGS += -DSWITCHRES_VERSION="\"$(VERSION)\""
endif
VERSION_MAJOR := $(firstword $(subst ., ,$(VERSION)))
VERSION_MINOR := $(word 2,$(subst ., ,$(VERSION)))
VERSION_PATCH := $(word 3,$(subst ., ,$(VERSION)))

$(info Switchres $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH))

# Linux
ifeq  ($(PLATFORM),Linux)
SRC += display_linux.cpp

HAS_VALID_XRANDR := $(shell $(PKG_CONFIG) --silence-errors --libs xrandr; echo $$?)
ifeq ($(HAS_VALID_XRANDR),1)
    $(info Switchres needs xrandr. X support is disabled)
else
    $(info X support enabled)
    CPPFLAGS += -DSR_WITH_XRANDR
    SRC += custom_video_xrandr.cpp
endif

HAS_VALID_DRMKMS := $(shell $(PKG_CONFIG) --silence-errors --libs "libdrm >= 2.4.98"; echo $$?)
ifeq ($(HAS_VALID_DRMKMS),1)
    $(info Switchres needs libdrm >= 2.4.98. KMS support is disabled)
else
    $(info KMS support enabled)
    CPPFLAGS += -DSR_WITH_KMSDRM
    EXTRA_LIBS = libdrm
    SRC += custom_video_drmkms.cpp
    ifeq ($(SR_WITH_DRMHOOK),1)
        CPPFLAGS += -DSR_WITH_DRMHOOK
    endif
endif

# Wayland (KDE + wlroots) switchres backend
# Requires: wayland-client >= 1.20, wayland-scanner at build time
# Vendored protocol XMLs live in protocols/kde/ and protocols/wlroots/.
KDE_PROTOCOL_DIR = protocols/kde
WLROOTS_PROTOCOL_DIR = protocols/wlroots
WAYLAND_SCANNER ?= wayland-scanner

HAS_VALID_WAYLAND := $(shell $(PKG_CONFIG) --silence-errors --libs "wayland-client >= 1.20"; echo $$?)
HAS_WAYLAND_SCANNER := $(shell command -v wayland-scanner >/dev/null 2>&1 && echo yes)
ifeq ($(HAS_VALID_WAYLAND),1)
    $(info Switchres needs wayland-client >= 1.20. Wayland support is disabled)
else ifneq ($(HAS_WAYLAND_SCANNER),yes)
    $(info Switchres needs wayland-scanner at build time. Wayland support is disabled)
else
    $(info Wayland support enabled)
    CPPFLAGS += -DSR_WITH_KDE -DSR_WITH_WLROOTS
    CPPFLAGS += -I$(KDE_PROTOCOL_DIR) -I$(WLROOTS_PROTOCOL_DIR)
    EXTRA_LIBS += wayland-client
    SRC += custom_video_kde.cpp custom_video_wlroots.cpp

    KDE_PROTOCOLS = \
        $(KDE_PROTOCOL_DIR)/kde-output-device-v2 \
        $(KDE_PROTOCOL_DIR)/kde-output-management-v2

    WLROOTS_PROTOCOLS = \
        $(WLROOTS_PROTOCOL_DIR)/wlr-output-management-unstable-v1

    KDE_GEN_HEADERS = $(addsuffix -client.h, $(KDE_PROTOCOLS))
    KDE_GEN_SOURCES = $(addsuffix -client-protocol.c, $(KDE_PROTOCOLS))
    WLROOTS_GEN_HEADERS = $(addsuffix -client.h, $(WLROOTS_PROTOCOLS))
    WLROOTS_GEN_SOURCES = $(addsuffix -client-protocol.c, $(WLROOTS_PROTOCOLS))

    ALL_OBJS_DEPS += $(KDE_GEN_HEADERS) $(WLROOTS_GEN_HEADERS)
    KDE_OBJS_DEPS += $(KDE_GEN_HEADERS) $(KDE_GEN_SOURCES)
    WLROOTS_OBJS_DEPS += $(WLROOTS_GEN_HEADERS) $(WLROOTS_GEN_SOURCES)
endif

# SDL2 misses a test for drm as drm.h is required
HAS_VALID_SDL2 := $(shell $(PKG_CONFIG) --silence-errors --libs "sdl2 >= 2.0.16"; echo $$?)
ifeq ($(HAS_VALID_SDL2),1)
    $(info Switchres needs SDL2 >= 2.0.16. SDL2 support is disabled)
else
    $(info SDL2 support enabled)
    CPPFLAGS += -DSR_WITH_SDL2 $(pkg-config --cflags sdl2)
    EXTRA_LIBS += sdl2
    SRC += display_sdl2.cpp
endif

ifneq (,$(EXTRA_LIBS))
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags $(EXTRA_LIBS))
LIBS += $(shell $(PKG_CONFIG) --libs $(EXTRA_LIBS))
endif

CPPFLAGS += -fPIC
LIBS += -ldl

REMOVE = rm -f
STATIC_LIB_EXT = a
DYNAMIC_LIB_EXT = so.$(VERSION)
LINKER_NAME := $(TARGET_LIB).so
REAL_SO_NAME := $(LINKER_NAME).$(VERSION)
SO_NAME := $(LINKER_NAME).$(VERSION_MAJOR)
LIB_CPPFLAGS := -Wl,-soname,$(SO_NAME)
# Windows
else ifneq (,$(findstring NT,$(PLATFORM)))
SRC += display_windows.cpp custom_video_ati_family.cpp custom_video_ati.cpp custom_video_adl.cpp custom_video_pstrip.cpp resync_windows.cpp
WIN_ONLY_FLAGS = -static-libgcc -static-libstdc++
CPPFLAGS += -static $(WIN_ONLY_FLAGS)
LIBS =
#REMOVE = del /f
REMOVE = rm -f
STATIC_LIB_EXT = lib
DYNAMIC_LIB_EXT = dll
endif

define SR_PKG_CONFIG
prefix=$(PREFIX)
exec_prefix=$${prefix}
includedir=$${prefix}/include
libdir=$${exec_prefix}/lib

Name: libswitchres
Description: A modeline generator for CRT monitors
Version: $(VERSION)
Cflags: -I$${includedir}/switchres
Libs: -L$${libdir} -ldl -lswitchres
endef


%.o : %.cpp
	$(FINAL_CXX) -c $(CPPFLAGS) $< -o $@

# Pattern rules for generating protocol headers/sources from XMLs.
# These are outside the conditional block so they're always defined.
# They only fire when a prerequisite (KDE_GEN_HEADERS etc.) requests them.
$(KDE_PROTOCOL_DIR)/%-client.h: $(KDE_PROTOCOL_DIR)/%.xml
	$(WAYLAND_SCANNER) client-header $< $@
$(KDE_PROTOCOL_DIR)/%-client-protocol.c: $(KDE_PROTOCOL_DIR)/%.xml
	$(WAYLAND_SCANNER) private-code $< $@
$(WLROOTS_PROTOCOL_DIR)/%-client.h: $(WLROOTS_PROTOCOL_DIR)/%.xml
	$(WAYLAND_SCANNER) client-header $< $@
$(WLROOTS_PROTOCOL_DIR)/%-client-protocol.c: $(WLROOTS_PROTOCOL_DIR)/%.xml
	$(WAYLAND_SCANNER) private-code $< $@

all: $(SRC:.cpp=.o) $(MAIN).cpp $(TARGET_LIB) prepare_pkg_config
	@echo $(OSFLAG)
	$(FINAL_CXX) $(CPPFLAGS) $(CXXFLAGS) $(SRC:.cpp=.o) $(MAIN).cpp $(LIBS) -o $(STANDALONE)

# Wayland: explicit prerequisites for object files that include generated
# protocol headers. These MUST come after 'all:' so they don't become the
# default goal. They add extra prerequisites without overriding the recipe.
custom_video.o: $(ALL_OBJS_DEPS)
custom_video_kde.o: $(KDE_OBJS_DEPS)
custom_video_wlroots.o: $(WLROOTS_OBJS_DEPS)

$(TARGET_LIB): $(OBJS)
	$(FINAL_CXX) $(LDFLAGS) $(CPPFLAGS) $(LIB_CPPFLAGS) -o $@.$(DYNAMIC_LIB_EXT) $^
	$(FINAL_CXX) -c $(CPPFLAGS) -DSR_WIN32_STATIC switchres_wrapper.cpp -o switchres_wrapper.o
	$(FINAL_AR) rcs $@.$(STATIC_LIB_EXT) $(^)

$(DRMHOOK_LIB):
	$(FINAL_CXX) drm_hook.cpp -shared -ldl -fPIC -I/usr/include/libdrm  -o libdrmhook.so

$(GRID):
	$(FINAL_CXX) grid.cpp $(WIN_ONLY_FLAGS) -lSDL2 -lSDL2_ttf -o grid

clean:
	$(REMOVE) $(OBJS) $(STANDALONE) $(TARGET_LIB).*
	$(REMOVE) switchres.pc
	$(REMOVE) protocols/kde/kde-output-device-v2-client.h
	$(REMOVE) protocols/kde/kde-output-device-v2-client-protocol.c
	$(REMOVE) protocols/kde/kde-output-management-v2-client.h
	$(REMOVE) protocols/kde/kde-output-management-v2-client-protocol.c
	$(REMOVE) protocols/wlroots/wlr-output-management-unstable-v1-client.h
	$(REMOVE) protocols/wlroots/wlr-output-management-unstable-v1-client-protocol.c

prepare_pkg_config:
	$(file > switchres.pc,$(SR_PKG_CONFIG))

install:
	$(INSTALL) -Dm644 $(TARGET_LIB).$(DYNAMIC_LIB_EXT) $(LIBDIR)/$(TARGET_LIB).$(DYNAMIC_LIB_EXT)
	$(INSTALL) -Dm644 switchres_defines.h $(INCDIR)/switchres/switchres_defines.h
	$(INSTALL) -Dm644 switchres_wrapper.h $(INCDIR)/switchres/switchres_wrapper.h
	$(INSTALL) -Dm644 switchres.h $(INCDIR)/switchres/switchres.h
	$(INSTALL) -Dm644 switchres.pc $(PKGDIR)/switchres.pc
ifneq ($(SO_NAME),)
	$(LN) -s -f $(REAL_SO_NAME) $(LIBDIR)/$(SO_NAME)
	$(LN) -s -f $(SO_NAME) $(LIBDIR)/$(LINKER_NAME)
endif

uninstall:
	$(REMOVE) $(LIBDIR)/$(TARGET_LIB).*
	$(REMOVE) $(PKGDIR)/switchres.pc

# Tests
TESTS = tests/test_liblink tests/test_dlopen tests/test_restore_modes

.PHONY: tests
tests: $(TESTS)

libswitchres.$(STATIC_LIB_EXT) libswitchres.$(DYNAMIC_LIB_EXT): $(TARGET_LIB)

tests/test_liblink: libswitchres.$(STATIC_LIB_EXT)
tests/test_dlopen: | libswitchres.$(DYNAMIC_LIB_EXT)
tests/test_restore_modes: $(OBJS)

$(TESTS): %: %.cpp
	$(FINAL_CXX) $(CPPFLAGS) $(CXXFLAGS) -I.. $< $(filter %.o %.a %.lib,$^) $(LIBS) -o $@
