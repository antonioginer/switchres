PLATFORM := $(shell uname)

MAIN = switchres_main
TARGET_LIB = libswitchres
SRC = monitor.cpp modeline.cpp switchres.cpp display.cpp
OBJS = $(SRC:.cpp=.o)

CROSS_COMPILE ?=
CXX ?= g++
AR ?= ar
LDFLAGS =
FINAL_CXX=$(CROSS_COMPILE)$(CXX)
FINAL_AR=$(CROSS_COMPILE)$(AR)
CPPFLAGS = -O3 -Wall -Wextra

# Linux
ifeq  ($(PLATFORM),Linux)
SRC += display_linux.cpp
CPPFLAGS += -fPIC
LIBS =
REMOVE = rm -f 
STATIC_LIB_EXT = a
DYNAMIC_LIB_EXT = so

# Windows
else ifneq (,$(findstring NT,$(PLATFORM)))
SRC += display_windows.cpp custom_video.cpp custom_video_ati_family.cpp custom_video_ati.cpp custom_video_adl.cpp custom_video_pstrip.cpp 
CFLAGS = -O3 -static -static-libgcc -static-libstdc++ 
LIBS = 
REMOVE = del /f
STATIC_LIB_EXT = lib
DYNAMIC_LIB_EXT = dll
endif

%.o : %.cpp
	$(FINAL_CXX) -c $(CPPFLAGS) $< -o $@

all: $(SRC:.cpp=.o) $(MAIN).cpp
	@echo $(OSFLAG)
	$(FINAL_CXX) $(CPPFLAGS) $(CXXFLAGS) $(SRC:.cpp=.o) $(MAIN).cpp $(LIBS) -o $(MAIN)

$(TARGET_LIB): $(OBJS)
	$(FINAL_CXX) $(LDFLAGS) $(CPPFLAGS) -shared -o $@.$(DYNAMIC_LIB_EXT) $^
	$(FINAL_AR) rcs $@.$(STATIC_LIB_EXT) $(^)

clean:
	$(REMOVE) $(OBJS) $(MAIN) $(TARGET_LIB).*
