# minimalist makefile
.SUFFIXES:
#
.SUFFIXES: .cpp .o .c .h

# Architecture-specific flags.
#   x86_64 / i386 : enable SSE4.2 + PCLMULQDQ.
#   aarch64 / arm64 : enable NEON with the crypto extension (PMULL).
UNAME_M := $(shell uname -m)
ifeq ($(filter $(UNAME_M),x86_64 amd64 i386 i686),$(UNAME_M))
ARCH_FLAGS = -msse4.2 -mpclmul -march=native
else ifeq ($(filter $(UNAME_M),aarch64 arm64),$(UNAME_M))
# Apple Silicon defaults to armv8.5+; +crypto is always available there.
# On Linux/AArch64, +crypto is required for PMULL.
ARCH_FLAGS = -march=armv8-a+crypto
else
ARCH_FLAGS = -march=native
endif

ifeq ($(DEBUG),1)
CFLAGS = -fPIC  -std=c99 -ggdb $(ARCH_FLAGS) -funroll-loops -Wstrict-overflow -Wstrict-aliasing -Wall -Wextra -pedantic -Wshadow -fsanitize=undefined  -fno-omit-frame-pointer -fsanitize=address
CXXFLAGS = -fPIC  -std=c++11 -ggdb $(ARCH_FLAGS) -funroll-loops -Wstrict-overflow -Wstrict-aliasing -Wall -Wextra -pedantic -Wshadow -fsanitize=undefined  -fno-omit-frame-pointer -fsanitize=address
else
CFLAGS = -fPIC -std=c99 -O3 $(ARCH_FLAGS) -funroll-loops -Wstrict-overflow -Wstrict-aliasing -Wall -Wextra -pedantic -Wshadow
CXXFLAGS = -fPIC -std=c++11 -O3 $(ARCH_FLAGS) -funroll-loops -Wstrict-overflow -Wstrict-aliasing -Wall -Wextra -pedantic -Wshadow
endif # debug

HEADERS=include/clhash.h

OBJECTS= clhash.o

all: $(OBJECTS) unit cppunit benchmark example cppexample

unit : ./tests/unit.c  $(HEADERS) $(OBJECTS)
	$(CC) $(CFLAGS) -o unit ./tests/unit.c -Iinclude  $(OBJECTS)

cppunit : ./tests/cppunit.cpp  $(HEADERS) $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o cppunit ./tests/cppunit.cpp -Iinclude  $(OBJECTS)


example : ./examples/example.c $(HEADERS) $(OBJECTS)
	$(CC) $(CFLAGS) -o example ./examples/example.c -Iinclude  $(OBJECTS)

cppexample : ./examples/cppexample.cpp $(HEADERS) $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o cppexample ./examples/cppexample.cpp -Iinclude  $(OBJECTS)


benchmark :./benchmarks/benchmark.c $(HEADERS) $(OBJECTS)
	$(CC) $(CFLAGS) -o benchmark ./benchmarks/benchmark.c -Iinclude  $(OBJECTS)


clhash.o: ./src/clhash.c $(HEADERS)
	$(CC) $(CFLAGS) -c ./src/clhash.c -Iinclude

clean:
	rm -f $(OBJECTS) unit cppunit benchmark example cppexample
