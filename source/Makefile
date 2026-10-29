# Builds ../output/CGR26_renderer. Usage (from this directory): make -j
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -pthread
LDFLAGS ?= -pthread

OUTDIR := ../output
BUILDDIR := ../build
TARGET := $(OUTDIR)/CGR26_renderer
SRCS := main.cpp parser.cpp scene.cpp shapes.cpp lights.cpp integrators.cpp image.cpp logger.cpp
OBJS := $(SRCS:%.cpp=$(BUILDDIR)/%.o)

all: $(TARGET)

$(TARGET): $(OBJS) | $(OUTDIR)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(BUILDDIR)/%.o: %.cpp $(wildcard *.h) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUTDIR) $(BUILDDIR):
	mkdir -p $@

clean:
	rm -rf $(BUILDDIR) $(TARGET)

.PHONY: all clean
