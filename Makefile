#
# Minrend build system
#
# This Makefile is written to be friendly to Cosmopolitan's `cosmocc`
# toolchain but can also work with a normal POSIX toolchain for local
# development.
#

PROJECT    := minrend

# Compiler / archiver
# By default we use the wrapper scripts in scripts/ which handle both
# Windows (.bat) and Unix environments properly.
# Override CC/AR if you explicitly want another toolchain.
# Use ?= to allow environment variables to override
CC        ?= $(CURDIR)/scripts/x86_64-unknown-cosmo-cc
AR        ?= $(CURDIR)/scripts/x86_64-unknown-cosmo-ar
MAKE      ?= make

SRC_DIR    = src
INC_DIR    = include

SRCS       = \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/js_engine.c \
	$(SRC_DIR)/dom_bindings.c \
	$(SRC_DIR)/renderer.c \
	$(SRC_DIR)/webgl_bindings.c \
	$(SRC_DIR)/canvas_bindings.c \
	$(SRC_DIR)/fetch_bindings.c \
	$(SRC_DIR)/storage_bindings.c \
	$(SRC_DIR)/sdl_gl_stubs.c

OBJS       = $(SRCS:.c=.o)

# Third‑party directories
QJS_DIR     = third_party/quickjs
MODEST_DIR  = third_party/modest
SDL2_DIR    = third_party/SDL2

QJS_OBJS    = $(QJS_DIR)/quickjs.o \
              $(QJS_DIR)/cutils.o \
              $(QJS_DIR)/libregexp.o \
              $(QJS_DIR)/libunicode.o \
              $(QJS_DIR)/dtoa.o
QJS_LIB     = $(QJS_DIR)/libquickjs.a

# Modest library is currently disabled - its Makefile doesn't work with cosmo make on Windows
# Enable when Modest is needed and its build is fixed
# MODEST_LIB  = $(MODEST_DIR)/lib/libmodest_static.a
MODEST_LIB  =

# -Wno-unused-parameter is needed because QuickJS headers have inline functions
# with unused ctx parameters
CFLAGS    += -I$(INC_DIR) -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter

# Third‑party include paths – adjust as needed for your environment.
CFLAGS    += -I$(QJS_DIR) -I$(MODEST_DIR)/include -I$(SDL2_DIR)/include

# SDL2 / OpenGL linking
# For Cosmopolitan builds, we don't link SDL2/GL as libraries since they're
# platform-specific. The headers are used for type definitions, but actual
# SDL2/GL functionality would need stubs or dynamic loading.
# LDLIBS    += -lSDL2 -lGL

# Link in third‑party static libraries.
LDLIBS    += $(QJS_LIB) $(MODEST_LIB)

all: $(PROJECT)

# COSMO_EXTRA_LDLIBS is set by the build script for Cosmopolitan builds
# and contains -lc -lcosmo which must come AFTER QuickJS to resolve symbols
$(PROJECT): $(OBJS) $(QJS_LIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS) $(COSMO_EXTRA_LDLIBS)

# Create a ZIP of the app directory for embedding
app.zip:
	@if [ -d "app" ]; then \
		cd app && zip -r ../app.zip .; \
	else \
		echo "Warning: app/ directory not found, creating empty ZIP"; \
		touch app.zip; \
	fi

# Append ZIP to executable for single-file distribution
$(PROJECT).zip: $(PROJECT) app.zip
	cat $(PROJECT) app.zip > $(PROJECT).zip
	chmod +x $(PROJECT).zip
	@echo "Created single-file distribution: $(PROJECT).zip"

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# --- QuickJS static library ---------------------------------------------
# QuickJS compatibility flags for Cosmopolitan
# - Suppress warnings since it's third-party code
# - Define CONFIG_VERSION for version string
# - Define asm as __asm__ for GNU C compatibility
QJS_WARN_FLAGS = -Wno-unused-parameter -Wno-implicit-fallthrough -Wno-sign-compare -Wno-missing-field-initializers
QJS_COMPAT_FLAGS = -DCONFIG_VERSION=\"2024-02-14\" -Dasm=__asm__

$(QJS_DIR)/%.o: $(QJS_DIR)/%.c
	$(CC) $(CFLAGS) $(QJS_WARN_FLAGS) $(QJS_COMPAT_FLAGS) -I$(QJS_DIR) -c -o $@ $<

$(QJS_LIB): $(QJS_OBJS)
	$(AR) rcs $@ $^

# --- Modest static library ----------------------------------------------
# Disabled for now - enable when Modest is needed and its build is fixed for Windows
# $(MODEST_LIB):
#	$(MAKE) -C $(MODEST_DIR) static CC=$(CC)

# --- Test programs -----------------------------------------------------------

test_window: $(SRC_DIR)/test_window.o $(SRC_DIR)/cosmo_window.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $^ $(COSMO_EXTRA_LDLIBS)

$(SRC_DIR)/cosmo_window.o: $(SRC_DIR)/cosmo_window.c $(SRC_DIR)/cosmo_window.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRC_DIR)/test_window.o: $(SRC_DIR)/test_window.c $(SRC_DIR)/cosmo_window.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(PROJECT) test_window
	rm -f $(SRC_DIR)/cosmo_window.o $(SRC_DIR)/test_window.o
	$(RM) $(QJS_OBJS) $(QJS_LIB)
	$(MAKE) -C $(MODEST_DIR) clean || true

.PHONY: all clean test_window


