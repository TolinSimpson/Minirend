#
# minirend build system
#
# This Makefile is written to be friendly to Cosmopolitan's `cosmocc`
# toolchain but can also work with a normal POSIX toolchain for local
# development.
#

PROJECT    := minirend

# Compiler / archiver
# By default we use the wrapper scripts in build_scripts/ which handle both
# Windows (.bat) and Unix environments properly.
# Override CC/AR if you explicitly want another toolchain.
# Use ?= to allow environment variables to override
CC        ?= $(CURDIR)/build_scripts/x86_64-unknown-cosmo-cc
AR        ?= $(CURDIR)/build_scripts/x86_64-unknown-cosmo-ar
MAKE      ?= make

SRC_DIR      = src
PLATFORM_DIR = src/platform

SRCS       = \
	$(SRC_DIR)/sokol_main.c \
	$(SRC_DIR)/js_engine.c \
	$(SRC_DIR)/dom_bindings.c \
	$(SRC_DIR)/dom_runtime.c \
	$(SRC_DIR)/input.c \
	$(SRC_DIR)/ui_tree.c \
	$(SRC_DIR)/modest_adapter.c \
	$(SRC_DIR)/renderer.c \
	$(SRC_DIR)/webgl_bindings.c \
	$(SRC_DIR)/canvas_bindings.c \
	$(SRC_DIR)/fetch_bindings.c \
	$(SRC_DIR)/storage_bindings.c

OBJS       = $(SRCS:.c=.o)

# Third‑party directories
QJS_DIR     = third_party/quickjs
MODEST_DIR  = third_party/modest
SOKOL_DIR   = third_party/sokol
COSMO_SOKOL_DIR = third_party/cosmo-sokol

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

# ============================================================================
# SOKOL MULTI-PLATFORM SHIMS (from cosmo-sokol)
# 
# For Cosmopolitan cross-platform support, we compile sokol twice:
# - sokol_windows.c with D3D11 backend, functions prefixed with windows_
# - sokol_linux.c with OpenGL backend, functions prefixed with linux_
# - sokol_cosmo.c dispatches to the right platform at runtime
# - gl_stub.c / x11_stub.c provide dlopen wrappers for Linux
# ============================================================================

SOKOL_SHIM_SRCS = \
	$(PLATFORM_DIR)/sokol_windows.c \
	$(PLATFORM_DIR)/sokol_linux.c \
	$(PLATFORM_DIR)/sokol_shared.c \
	$(PLATFORM_DIR)/sokol_cosmo.c \
	$(PLATFORM_DIR)/gl_stub.c \
	$(PLATFORM_DIR)/x11_stub.c \
	$(PLATFORM_DIR)/win32_tweaks.c

SOKOL_SHIM_OBJS = $(SOKOL_SHIM_SRCS:.c=.o)

SOKOL_LIB = $(PLATFORM_DIR)/libsokol_cosmo.a

# Base flags for sokol compilation - suppress warnings for third-party code
SOKOL_WARN_FLAGS = \
	-Wno-unused-parameter \
	-Wno-unused-function \
	-Wno-unused-variable \
	-Wno-sign-compare \
	-Wno-missing-field-initializers \
	-Wno-implicit-function-declaration \
	-Wno-incompatible-pointer-types \
	-Wno-unknown-pragmas \
	-Wno-pointer-sign \
	-Wno-int-to-pointer-cast \
	-Wno-int-conversion \
	-Wno-array-parameter

# -Wno-unused-parameter is needed because QuickJS headers have inline functions
# with unused ctx parameters
CFLAGS    += -I$(SRC_DIR) -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter

# Third‑party include paths
CFLAGS    += -I$(QJS_DIR) -I$(MODEST_DIR)/include
CFLAGS    += -I$(SOKOL_DIR) -I$(PLATFORM_DIR)

# Link in third‑party static libraries.
LDLIBS    += $(QJS_LIB) $(MODEST_LIB)

# ============================================================================
# BUILD MODE CONFIGURATION
# Sokol graphics is always enabled (SDL2 removed)
# ============================================================================

LDLIBS += $(SOKOL_LIB)
CFLAGS += -DMINIREND_SOKOL_ENABLED

all: $(PROJECT)

# COSMO_EXTRA_LDLIBS is set by the build script for Cosmopolitan builds
# and contains -lc -lcosmo which must come AFTER QuickJS to resolve symbols
$(PROJECT): $(OBJS) $(QJS_LIB) $(SOKOL_LIB)
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

# ============================================================================
# SOKOL MULTI-PLATFORM SHIMS BUILD (cosmo-sokol approach)
# ============================================================================

# Get cosmopolitan include path from CC path
COSMO_HOME = $(dir $(shell dirname $(CC)))

# Linux-specific flags: include the shims for X11/GL headers
LINUX_FLAGS = $(SOKOL_WARN_FLAGS) -I$(PLATFORM_DIR)/shims -I$(SOKOL_DIR)

# Windows-specific flags: use cosmopolitan's NT headers
WIN32_FLAGS = $(SOKOL_WARN_FLAGS) -I$(PLATFORM_DIR)/shims -I$(SOKOL_DIR)

# Windows sokol backend (D3D11)
$(PLATFORM_DIR)/sokol_windows.o: $(PLATFORM_DIR)/sokol_windows.c $(PLATFORM_DIR)/sokol_windows.h
	$(CC) $(CFLAGS) $(WIN32_FLAGS) -c -o $@ $<

# Linux sokol backend (OpenGL Core)
$(PLATFORM_DIR)/sokol_linux.o: $(PLATFORM_DIR)/sokol_linux.c $(PLATFORM_DIR)/sokol_linux.h
	$(CC) $(CFLAGS) $(LINUX_FLAGS) -c -o $@ $<

# Shared sokol code
$(PLATFORM_DIR)/sokol_shared.o: $(PLATFORM_DIR)/sokol_shared.c
	$(CC) $(CFLAGS) $(SOKOL_WARN_FLAGS) -I$(SOKOL_DIR) -c -o $@ $<

# Cosmo runtime dispatcher
$(PLATFORM_DIR)/sokol_cosmo.o: $(PLATFORM_DIR)/sokol_cosmo.c
	$(CC) $(CFLAGS) $(SOKOL_WARN_FLAGS) -I$(SOKOL_DIR) -c -o $@ $<

# Linux GL stub (dlopen wrapper)
$(PLATFORM_DIR)/gl_stub.o: $(PLATFORM_DIR)/gl_stub.c
	$(CC) $(CFLAGS) $(LINUX_FLAGS) -c -o $@ $<

# Linux X11 stub (dlopen wrapper)
$(PLATFORM_DIR)/x11_stub.o: $(PLATFORM_DIR)/x11_stub.c
	$(CC) $(CFLAGS) $(LINUX_FLAGS) -c -o $@ $<

# Win32 tweaks
$(PLATFORM_DIR)/win32_tweaks.o: $(PLATFORM_DIR)/win32_tweaks.c $(PLATFORM_DIR)/win32_tweaks.h
	$(CC) $(CFLAGS) $(WIN32_FLAGS) -c -o $@ $<

# Bundle all sokol shims into a static library
$(SOKOL_LIB): $(SOKOL_SHIM_OBJS)
	$(AR) rcs $@ $^

clean:
	rm -f $(OBJS) $(PROJECT)
	rm -f $(SOKOL_SHIM_OBJS) $(SOKOL_LIB)
	$(RM) $(QJS_OBJS) $(QJS_LIB)
	$(MAKE) -C $(MODEST_DIR) clean || true

# Print build info
info:
	@echo "minirend Build Configuration"
	@echo "==========================="
	@echo ""
	@echo "Sokol platform files:"
	@echo "  $(SOKOL_SHIM_SRCS)"
	@echo ""
	@echo "Build targets:"
	@echo "  all   - Build minirend"
	@echo "  clean - Remove build artifacts"
	@echo "  info  - Show this help"

.PHONY: all clean info
