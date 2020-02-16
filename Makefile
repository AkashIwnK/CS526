#Assumes that the PATH variable is already set to path to clang/clang++

CC = clang
CXX  = clang++

SOURCES = $(wildcard *.cpp)

OPTIMIZATION = -O0

CC_FLAGS =  `llvm-config --cflags --ldflags --libs --system-libs` -g $(OPTIMIZATION) -fno-rtti

OBJECT_FILES = $(SOURCES:%.cpp=%.o)

EXEBASE = SROA
SHARED_LIB = $(addsuffix .so, $(EXEBASE))

.SUFFIXES: .o .cpp .so

.PHONY = all

all: $(OBJECT_FILES)
	$(CXX) -shared -o $(SHARED_LIB) $(OBJECT_FILES)

%.o: %.cpp
	$(CXX) -o $@ -c $< -fPIC -w  $(CC_FLAGS)

clean:
	rm -rf *.o *.so
