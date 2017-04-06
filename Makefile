CXX=g++
CXXFLAGS=-O3 -march=native -Wall
LIBS=-pthread
INCLUDES=-Iinclude -Iprotocols

BINARIES=$(patsubst examples/%.cpp,%,$(wildcard examples/*))
BIN_PATH=$(patsubst %,bin/%, ${BINARIES})

DEPDIR=.deps
DEPFLAGS=-MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td

all: ${BIN_PATH}

bin/%: examples/%.cpp Makefile $(DEPDIR)/%.d
	${CXX} $< -o $@ ${CXXFLAGS} ${LIBS} ${INCLUDES} ${DEPFLAGS}
	mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

include $(wildcard $(patsubst %,$(DEPDIR)/%.d,$(basename $(BINARIES))))

