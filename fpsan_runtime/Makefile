TARGET   = libfpsanitizer
mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
TD_ROOT := $(notdir $(patsubst %/,%,$(dir $(mkfile_path))))
CXX=g++
LINKER=g++
LDFLAGS+= -shared
OPT=-O3

PREC := $(if $(PRECISION),$(PRECISION), "PREC_512")
CXXFLAGS+=-D $(PREC) -fPIC -g -std=c++11 -rdynamic -Wall -lmpfr -pthread 

INCPATH=-I. 
ARFLAGS=-rcs

# change these to set the proper directories where each files shoould be
SRCDIR = .
INCDIR = .
OBJDIR = obj
BINDIR = bin

SOURCES  := $(wildcard $(SRCDIR)/*.cpp)
INCLUDES := $(wildcard $(INCDIR)/*.h)
OBJECTS  := $(SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
rm       = rm -rf

all:
	@test -d $(OBJDIR) || mkdir $(OBJDIR)
	@make tool

tool: $(OBJECTS)
	@$(LINKER) $(OBJECTS) $(LDFLAGS) -o $(TARGET).so
	@mv $(TARGET).so $(OBJDIR)
	@echo "Linking complete!"

$(OBJECTS): $(OBJDIR)/%.o : $(SRCDIR)/%.cpp
	$(CXX) $(OPT) $(CXXFLAGS) -c $< $(INCPATH) -o $@
	@echo "Compiled "$<" successfully!"

.PHONY: clean

clean:
	@$(rm) $(OBJDIR)
	@$(rm) *~
	@echo "Cleanup complete!"
