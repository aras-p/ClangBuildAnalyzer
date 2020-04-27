INCLUDES=
CFLAGS:=$(INCLUDES) -O2 -DNDEBUG=1 $(CFLAGS)
CXXFLAGS:=$(INCLUDES) -O2 -DNDEBUG=1 -std=c++17 $(CXXFLAGS)

C_SRCS= \
src/external/inih/ini.c \
src/external/xxHash/xxhash.c

CPP_SRCS= \
src/Analysis.cpp \
src/Arena.cpp \
src/BuildEvents.cpp \
src/Colors.cpp \
src/main.cpp \
src/Utils.cpp \
src/external/enkiTS/TaskScheduler.cpp \
src/external/inih/cpp/INIReader.cpp \
src/external/llvm-Demangle/lib/Demangle.cpp \
src/external/llvm-Demangle/lib/ItaniumDemangle.cpp \
src/external/llvm-Demangle/lib/MicrosoftDemangle.cpp \
src/external/llvm-Demangle/lib/MicrosoftDemangleNodes.cpp \
src/external/simdjson/simdjson.cpp

C_OBJS=$(C_SRCS:%.c=build/%.o)
CPP_OBJS=$(CPP_SRCS:%.cpp=build/%.o)

all: build/ClangBuildAnalyzer

clean:
	rm -f build/ClangBuildAnalyzer $(C_OBJS) $(CPP_OBJS)

build/ClangBuildAnalyzer: $(C_OBJS) $(CPP_OBJS)
	$(CXX) -o $@ $(C_OBJS) $(CPP_OBJS) $(LDFLAGS) -lrt -lpthread

build/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ -c $<

build/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ -c $<

srcdir = $(realpath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
vpath %.c $(srcdir)
vpath %.cpp $(srcdir)
