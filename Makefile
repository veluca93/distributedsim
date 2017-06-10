CXX=g++
CXXFLAGS=-O3 -march=native -Wall -std=c++17 -ltcmalloc -flto
LIBS=-pthread
INCLUDES=-Iinclude -Iprotocols

S_BINARIES=$(patsubst examples/%.cpp,%,$(wildcard examples/*.cpp))
M_BINARIES=$(patsubst examples/%/,%,$(sort $(dir $(wildcard examples/*/*))))
S_BIN_PATH=$(patsubst %,bin/%, ${S_BINARIES})
M_BIN_PATH=$(patsubst %,bin/%, ${M_BINARIES})

DEPDIR=.deps
DEPFLAGS=-MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td

all: ${S_BIN_PATH} ${M_BIN_PATH}

${S_BIN_PATH}:bin/%: examples/%.cpp Makefile $(DEPDIR)/%_cpp.d
	${CXX} $< -o $@ ${CXXFLAGS} ${LIBS} ${INCLUDES} ${DEPFLAGS}
	mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*_cpp.d

${M_BIN_PATH}:bin/%: examples/% Makefile $(DEPDIR)/%.d
	${CXX} $</*.cpp -o $@ ${CXXFLAGS} ${LIBS} ${INCLUDES} -I $< ${DEPFLAGS}
	mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

include $(wildcard $(patsubst %,$(DEPDIR)/%_cpp.d,$(basename $(S_BINARIES))))
include $(wildcard $(patsubst %,$(DEPDIR)/%.d,$(basename $(M_BINARIES))))
