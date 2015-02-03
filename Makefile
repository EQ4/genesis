.PHONY: all
all:

-include $(wildcard build/*.d)

OBJECTS = build/main.o build/util.o build/byte_buffer.o build/genesis.o build/path.o

CPP_FLAGS += -Ibuild -Isrc -g -Wall -Werror
COMPILE_CPP = g++ -nodefaultlibs -fno-exceptions -fno-rtti -c -std=c++11 -o $@ -MMD -MP -MF $@.d $(CPP_FLAGS) $<

build/genesis: $(OBJECTS)
	gcc -o $@ $(OBJECTS) -lgroove -lSDL2 -lm -lGLEW -lGLU -lGL
all: build/genesis

build/%.o: src/%.cpp
	$(COMPILE_CPP)

$(OBJECTS): | build
build:
	mkdir -p $@

.PHONY: clean
clean:
	rm -rf build

