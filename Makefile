# the compiler: gcc for C program, define as g++ for C++
CC = x86_64-w64-mingw32-g++-posix

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CFLAGS  = -g -Wall -std=c++17 -pthread -static

# the build target executable:
TARGET = indexingProcessor

all: $(TARGET)

$(TARGET): $(TARGET).cpp
	$(CC) $(CFLAGS) -o $(TARGET) $(TARGET).cpp

clean:
	$(RM) $(TARGET)