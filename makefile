PLATFORM := $(shell uname)

MAIN = switchres_main
SRC = monitor.cpp modeline.cpp switchres.cpp display.cpp

CC = g++

# Linux
ifeq  ($(PLATFORM),Linux)
SRC += display_linux.cpp
CFLAGS = -O3
LIBS =
REMOVE = rm -f 

# Windows
else ifneq (,$(findstring NT,$(PLATFORM)))
SRC += display_windows.cpp custom_video.cpp custom_video_ati_family.cpp custom_video_ati.cpp custom_video_adl.cpp custom_video_pstrip.cpp 
CFLAGS = -O3 -static -static-libgcc -static-libstdc++ 
LIBS = 
REMOVE = del /f
endif

%.o : %.cpp
		$(CC) -c $(CFLAGS) $< -o $@ 

all: $(SRC:.cpp=.o) $(MAIN).cpp
		@echo $(OSFLAG)
		$(CC) $(CFLAGS) $(SRC:.cpp=.o) $(MAIN).cpp $(LIBS) -o $(MAIN)

clean:
		$(REMOVE) $(SRC:.cpp=.o) $(MAIN)
