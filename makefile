PLATFORM := $(shell uname)

MAIN = switchres_main
SRC = monitor.cpp modeline.cpp switchres.cpp

CXX ?= g++

# Linux
ifeq  ($(PLATFORM),Linux)
#SRC += switchres_sdl.cpp 
CPPFLAGS = -O3
LIBS =
REMOVE = rm -f 

# Windows
else ifneq (,$(findstring NT,$(PLATFORM)))
SRC += display_windows.cpp custom_video.cpp custom_video_ati.cpp custom_video_adl.cpp custom_video_pstrip.cpp custom_video_ati_family.cpp
CFLAGS = -O3 -static -static-libgcc -static-libstdc++ 
LIBS = 
REMOVE = del /f
endif

%.o : %.cpp
		$(CXX) -c $(CPPFLAGS) $< -o $@

all: $(SRC:.cpp=.o) $(MAIN).cpp
		@echo $(OSFLAG)
		$(CXX) $(CPPFLAGS) $(SRC:.cpp=.o) $(MAIN).cpp $(LIBS) -o $(MAIN)

clean:
		$(REMOVE) $(SRC:.cpp=.o) $(MAIN)
