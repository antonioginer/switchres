PLATFORM := $(shell uname)

MAIN = switchres
SRC = monitor.cpp modeline.cpp util.cpp

CC = g++

# Linux
ifeq  ($(PLATFORM),Linux)
SRC += switchres_sdl.cpp 
CFLAGS = -O3
LIBS =
REMOVE = rm -f 

# Windows
else ifneq (,$(findstring NT,$(PLATFORM)))
SRC += switchres_linux.cpp 
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
