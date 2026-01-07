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
	$(SRC_DIR)/lexbor_adapter.c \
	$(SRC_DIR)/style_resolver.c \
	$(SRC_DIR)/layout_engine.c \
	$(SRC_DIR)/box_renderer.c \
	$(SRC_DIR)/font_cache.c \
	$(SRC_DIR)/text_renderer.c \
	$(SRC_DIR)/transform.c \
	$(SRC_DIR)/compositor.c \
	$(SRC_DIR)/renderer.c \
	$(SRC_DIR)/webgl_bindings.c \
	$(SRC_DIR)/canvas_bindings.c \
	$(SRC_DIR)/fetch_bindings.c \
	$(SRC_DIR)/storage_bindings.c \
	$(SRC_DIR)/audio_engine.c \
	$(SRC_DIR)/audio_buffer.c \
	$(SRC_DIR)/audio_bindings.c

OBJS       = $(SRCS:.c=.o)

# Third‑party directories
QJS_DIR     = third_party/quickjs
SOKOL_DIR   = third_party/sokol
COSMO_SOKOL_DIR = third_party/cosmo-sokol
LEXBOR_DIR  = third_party/lexbor

QJS_OBJS    = $(QJS_DIR)/quickjs.o \
              $(QJS_DIR)/cutils.o \
              $(QJS_DIR)/libregexp.o \
              $(QJS_DIR)/libunicode.o \
              $(QJS_DIR)/dtoa.o
QJS_LIB     = $(QJS_DIR)/libquickjs.a


# ============================================================================
# LEXBOR HTML/CSS PARSER LIBRARY
# Handles HTML parsing, CSS parsing, DOM, and style computation.
# We build it as a static library from source using POSIX port files
# (Cosmopolitan is POSIX-compatible).
# ============================================================================
LEXBOR_SRC = $(LEXBOR_DIR)/source/lexbor

# Core module (memory, arrays, strings, etc.)
LEXBOR_CORE_SRCS = \
	$(LEXBOR_SRC)/core/array.c \
	$(LEXBOR_SRC)/core/array_obj.c \
	$(LEXBOR_SRC)/core/avl.c \
	$(LEXBOR_SRC)/core/bst.c \
	$(LEXBOR_SRC)/core/bst_map.c \
	$(LEXBOR_SRC)/core/conv.c \
	$(LEXBOR_SRC)/core/diyfp.c \
	$(LEXBOR_SRC)/core/dobject.c \
	$(LEXBOR_SRC)/core/dtoa.c \
	$(LEXBOR_SRC)/core/hash.c \
	$(LEXBOR_SRC)/core/in.c \
	$(LEXBOR_SRC)/core/mem.c \
	$(LEXBOR_SRC)/core/mraw.c \
	$(LEXBOR_SRC)/core/plog.c \
	$(LEXBOR_SRC)/core/print.c \
	$(LEXBOR_SRC)/core/serialize.c \
	$(LEXBOR_SRC)/core/shs.c \
	$(LEXBOR_SRC)/core/str.c \
	$(LEXBOR_SRC)/core/strtod.c \
	$(LEXBOR_SRC)/core/utils.c

# Platform-specific core (POSIX for Cosmopolitan)
LEXBOR_PORT_SRCS = \
	$(LEXBOR_SRC)/ports/posix/lexbor/core/fs.c \
	$(LEXBOR_SRC)/ports/posix/lexbor/core/memory.c \
	$(LEXBOR_SRC)/ports/posix/lexbor/core/perf.c

# DOM module
LEXBOR_DOM_SRCS = \
	$(LEXBOR_SRC)/dom/collection.c \
	$(LEXBOR_SRC)/dom/exception.c \
	$(LEXBOR_SRC)/dom/interface.c \
	$(LEXBOR_SRC)/dom/interfaces/attr.c \
	$(LEXBOR_SRC)/dom/interfaces/cdata_section.c \
	$(LEXBOR_SRC)/dom/interfaces/character_data.c \
	$(LEXBOR_SRC)/dom/interfaces/comment.c \
	$(LEXBOR_SRC)/dom/interfaces/document.c \
	$(LEXBOR_SRC)/dom/interfaces/document_fragment.c \
	$(LEXBOR_SRC)/dom/interfaces/document_type.c \
	$(LEXBOR_SRC)/dom/interfaces/element.c \
	$(LEXBOR_SRC)/dom/interfaces/event_target.c \
	$(LEXBOR_SRC)/dom/interfaces/node.c \
	$(LEXBOR_SRC)/dom/interfaces/processing_instruction.c \
	$(LEXBOR_SRC)/dom/interfaces/shadow_root.c \
	$(LEXBOR_SRC)/dom/interfaces/text.c

# Tag/namespace modules
LEXBOR_TAG_NS_SRCS = \
	$(LEXBOR_SRC)/tag/tag.c \
	$(LEXBOR_SRC)/ns/ns.c

# Encoding module
LEXBOR_ENCODING_SRCS = \
	$(LEXBOR_SRC)/encoding/decode.c \
	$(LEXBOR_SRC)/encoding/encode.c \
	$(LEXBOR_SRC)/encoding/encoding.c \
	$(LEXBOR_SRC)/encoding/multi.c \
	$(LEXBOR_SRC)/encoding/range.c \
	$(LEXBOR_SRC)/encoding/res.c \
	$(LEXBOR_SRC)/encoding/single.c

# HTML parser module
LEXBOR_HTML_SRCS = \
	$(LEXBOR_SRC)/html/encoding.c \
	$(LEXBOR_SRC)/html/interface.c \
	$(LEXBOR_SRC)/html/node.c \
	$(LEXBOR_SRC)/html/parser.c \
	$(LEXBOR_SRC)/html/serialize.c \
	$(LEXBOR_SRC)/html/token.c \
	$(LEXBOR_SRC)/html/token_attr.c \
	$(LEXBOR_SRC)/html/tokenizer.c \
	$(LEXBOR_SRC)/html/tree.c \
	$(LEXBOR_SRC)/html/tokenizer/error.c \
	$(LEXBOR_SRC)/html/tokenizer/state.c \
	$(LEXBOR_SRC)/html/tokenizer/state_comment.c \
	$(LEXBOR_SRC)/html/tokenizer/state_doctype.c \
	$(LEXBOR_SRC)/html/tokenizer/state_rawtext.c \
	$(LEXBOR_SRC)/html/tokenizer/state_rcdata.c \
	$(LEXBOR_SRC)/html/tokenizer/state_script.c \
	$(LEXBOR_SRC)/html/tree/active_formatting.c \
	$(LEXBOR_SRC)/html/tree/error.c \
	$(LEXBOR_SRC)/html/tree/open_elements.c \
	$(LEXBOR_SRC)/html/tree/template_insertion.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/after_after_body.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/after_after_frameset.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/after_body.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/after_frameset.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/after_head.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/before_head.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/before_html.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/foreign_content.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/in_body.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/in_caption.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/in_cell.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/in_column_group.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/in_frameset.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/in_head.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/in_head_noscript.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/in_row.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/in_table.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/in_table_body.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/in_table_text.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/in_template.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/initial.c \
	$(LEXBOR_SRC)/html/tree/insertion_mode/text.c

# HTML element interfaces
LEXBOR_HTML_IFACE_SRCS = \
	$(LEXBOR_SRC)/html/interfaces/anchor_element.c \
	$(LEXBOR_SRC)/html/interfaces/area_element.c \
	$(LEXBOR_SRC)/html/interfaces/audio_element.c \
	$(LEXBOR_SRC)/html/interfaces/base_element.c \
	$(LEXBOR_SRC)/html/interfaces/body_element.c \
	$(LEXBOR_SRC)/html/interfaces/br_element.c \
	$(LEXBOR_SRC)/html/interfaces/button_element.c \
	$(LEXBOR_SRC)/html/interfaces/canvas_element.c \
	$(LEXBOR_SRC)/html/interfaces/d_list_element.c \
	$(LEXBOR_SRC)/html/interfaces/data_element.c \
	$(LEXBOR_SRC)/html/interfaces/data_list_element.c \
	$(LEXBOR_SRC)/html/interfaces/details_element.c \
	$(LEXBOR_SRC)/html/interfaces/dialog_element.c \
	$(LEXBOR_SRC)/html/interfaces/directory_element.c \
	$(LEXBOR_SRC)/html/interfaces/div_element.c \
	$(LEXBOR_SRC)/html/interfaces/document.c \
	$(LEXBOR_SRC)/html/interfaces/element.c \
	$(LEXBOR_SRC)/html/interfaces/embed_element.c \
	$(LEXBOR_SRC)/html/interfaces/field_set_element.c \
	$(LEXBOR_SRC)/html/interfaces/font_element.c \
	$(LEXBOR_SRC)/html/interfaces/form_element.c \
	$(LEXBOR_SRC)/html/interfaces/frame_element.c \
	$(LEXBOR_SRC)/html/interfaces/frame_set_element.c \
	$(LEXBOR_SRC)/html/interfaces/head_element.c \
	$(LEXBOR_SRC)/html/interfaces/heading_element.c \
	$(LEXBOR_SRC)/html/interfaces/hr_element.c \
	$(LEXBOR_SRC)/html/interfaces/html_element.c \
	$(LEXBOR_SRC)/html/interfaces/iframe_element.c \
	$(LEXBOR_SRC)/html/interfaces/image_element.c \
	$(LEXBOR_SRC)/html/interfaces/input_element.c \
	$(LEXBOR_SRC)/html/interfaces/label_element.c \
	$(LEXBOR_SRC)/html/interfaces/legend_element.c \
	$(LEXBOR_SRC)/html/interfaces/li_element.c \
	$(LEXBOR_SRC)/html/interfaces/link_element.c \
	$(LEXBOR_SRC)/html/interfaces/map_element.c \
	$(LEXBOR_SRC)/html/interfaces/marquee_element.c \
	$(LEXBOR_SRC)/html/interfaces/media_element.c \
	$(LEXBOR_SRC)/html/interfaces/menu_element.c \
	$(LEXBOR_SRC)/html/interfaces/meta_element.c \
	$(LEXBOR_SRC)/html/interfaces/meter_element.c \
	$(LEXBOR_SRC)/html/interfaces/mod_element.c \
	$(LEXBOR_SRC)/html/interfaces/o_list_element.c \
	$(LEXBOR_SRC)/html/interfaces/object_element.c \
	$(LEXBOR_SRC)/html/interfaces/opt_group_element.c \
	$(LEXBOR_SRC)/html/interfaces/option_element.c \
	$(LEXBOR_SRC)/html/interfaces/output_element.c \
	$(LEXBOR_SRC)/html/interfaces/paragraph_element.c \
	$(LEXBOR_SRC)/html/interfaces/param_element.c \
	$(LEXBOR_SRC)/html/interfaces/picture_element.c \
	$(LEXBOR_SRC)/html/interfaces/pre_element.c \
	$(LEXBOR_SRC)/html/interfaces/progress_element.c \
	$(LEXBOR_SRC)/html/interfaces/quote_element.c \
	$(LEXBOR_SRC)/html/interfaces/script_element.c \
	$(LEXBOR_SRC)/html/interfaces/search_element.c \
	$(LEXBOR_SRC)/html/interfaces/select_element.c \
	$(LEXBOR_SRC)/html/interfaces/selectedcontent_element.c \
	$(LEXBOR_SRC)/html/interfaces/slot_element.c \
	$(LEXBOR_SRC)/html/interfaces/source_element.c \
	$(LEXBOR_SRC)/html/interfaces/span_element.c \
	$(LEXBOR_SRC)/html/interfaces/style_element.c \
	$(LEXBOR_SRC)/html/interfaces/table_caption_element.c \
	$(LEXBOR_SRC)/html/interfaces/table_cell_element.c \
	$(LEXBOR_SRC)/html/interfaces/table_col_element.c \
	$(LEXBOR_SRC)/html/interfaces/table_element.c \
	$(LEXBOR_SRC)/html/interfaces/table_row_element.c \
	$(LEXBOR_SRC)/html/interfaces/table_section_element.c \
	$(LEXBOR_SRC)/html/interfaces/template_element.c \
	$(LEXBOR_SRC)/html/interfaces/text_area_element.c \
	$(LEXBOR_SRC)/html/interfaces/time_element.c \
	$(LEXBOR_SRC)/html/interfaces/title_element.c \
	$(LEXBOR_SRC)/html/interfaces/track_element.c \
	$(LEXBOR_SRC)/html/interfaces/u_list_element.c \
	$(LEXBOR_SRC)/html/interfaces/unknown_element.c \
	$(LEXBOR_SRC)/html/interfaces/video_element.c \
	$(LEXBOR_SRC)/html/interfaces/window.c

# CSS parser module
LEXBOR_CSS_SRCS = \
	$(LEXBOR_SRC)/css/at_rule.c \
	$(LEXBOR_SRC)/css/at_rule/state.c \
	$(LEXBOR_SRC)/css/css.c \
	$(LEXBOR_SRC)/css/declaration.c \
	$(LEXBOR_SRC)/css/log.c \
	$(LEXBOR_SRC)/css/parser.c \
	$(LEXBOR_SRC)/css/property.c \
	$(LEXBOR_SRC)/css/property/state.c \
	$(LEXBOR_SRC)/css/rule.c \
	$(LEXBOR_SRC)/css/state.c \
	$(LEXBOR_SRC)/css/stylesheet.c \
	$(LEXBOR_SRC)/css/unit.c \
	$(LEXBOR_SRC)/css/value.c \
	$(LEXBOR_SRC)/css/syntax/anb.c \
	$(LEXBOR_SRC)/css/syntax/parser.c \
	$(LEXBOR_SRC)/css/syntax/state.c \
	$(LEXBOR_SRC)/css/syntax/syntax.c \
	$(LEXBOR_SRC)/css/syntax/token.c \
	$(LEXBOR_SRC)/css/syntax/tokenizer.c \
	$(LEXBOR_SRC)/css/syntax/tokenizer/error.c \
	$(LEXBOR_SRC)/css/selectors/pseudo.c \
	$(LEXBOR_SRC)/css/selectors/pseudo_state.c \
	$(LEXBOR_SRC)/css/selectors/selector.c \
	$(LEXBOR_SRC)/css/selectors/selectors.c \
	$(LEXBOR_SRC)/css/selectors/state.c

# Selectors module (for querySelector etc.)
LEXBOR_SELECTORS_SRCS = \
	$(LEXBOR_SRC)/selectors/selectors.c

# Style module (CSS cascade, computed styles)
LEXBOR_STYLE_SRCS = \
	$(LEXBOR_SRC)/style/style.c \
	$(LEXBOR_SRC)/style/event.c \
	$(LEXBOR_SRC)/style/dom/interfaces/document.c \
	$(LEXBOR_SRC)/style/dom/interfaces/element.c \
	$(LEXBOR_SRC)/style/html/interfaces/document.c \
	$(LEXBOR_SRC)/style/html/interfaces/style_element.c

# Other modules we may need
LEXBOR_MISC_SRCS = \
	$(LEXBOR_SRC)/punycode/punycode.c \
	$(LEXBOR_SRC)/unicode/idna.c \
	$(LEXBOR_SRC)/unicode/unicode.c \
	$(LEXBOR_SRC)/url/url.c \
	$(LEXBOR_SRC)/utils/http.c \
	$(LEXBOR_SRC)/utils/warc.c \
	$(LEXBOR_SRC)/engine/engine.c

# All Lexbor sources
LEXBOR_SRCS = \
	$(LEXBOR_CORE_SRCS) \
	$(LEXBOR_PORT_SRCS) \
	$(LEXBOR_DOM_SRCS) \
	$(LEXBOR_TAG_NS_SRCS) \
	$(LEXBOR_ENCODING_SRCS) \
	$(LEXBOR_HTML_SRCS) \
	$(LEXBOR_HTML_IFACE_SRCS) \
	$(LEXBOR_CSS_SRCS) \
	$(LEXBOR_SELECTORS_SRCS) \
	$(LEXBOR_STYLE_SRCS) \
	$(LEXBOR_MISC_SRCS)

LEXBOR_OBJS = $(LEXBOR_SRCS:.c=.o)
LEXBOR_LIB  = $(LEXBOR_DIR)/liblexbor.a

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
CFLAGS    += -I$(QJS_DIR)
CFLAGS    += -I$(SOKOL_DIR) -I$(PLATFORM_DIR)
CFLAGS    += -I$(LEXBOR_DIR)/source
CFLAGS    += -Ithird_party/clay -Ithird_party/stb

# Link in third‑party static libraries.
LDLIBS    += $(QJS_LIB) $(LEXBOR_LIB)

# ============================================================================
# BUILD MODE CONFIGURATION
# Sokol graphics is always enabled (SDL2 removed)
# ============================================================================

LDLIBS += $(SOKOL_LIB)
CFLAGS += -DMINIREND_SOKOL_ENABLED

all: $(PROJECT)

# COSMO_EXTRA_LDLIBS is set by the build script for Cosmopolitan builds
# and contains -lc -lcosmo which must come AFTER QuickJS to resolve symbols
$(PROJECT): $(OBJS) $(QJS_LIB) $(SOKOL_LIB) $(LEXBOR_LIB)
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

# --- Lexbor static library -----------------------------------------------
# Lexbor flags: build as static, suppress warnings for third-party code
LEXBOR_WARN_FLAGS = \
	-Wno-unused-parameter \
	-Wno-unused-function \
	-Wno-unused-variable \
	-Wno-sign-compare \
	-Wno-missing-field-initializers \
	-Wno-implicit-function-declaration \
	-Wno-pointer-sign

# Define LEXBOR_STATIC so LXB_API is empty (no dllexport/dllimport)
LEXBOR_CFLAGS = -DLEXBOR_STATIC -I$(LEXBOR_DIR)/source $(LEXBOR_WARN_FLAGS)

# Generic rule for Lexbor .o files
$(LEXBOR_SRC)/%.o: $(LEXBOR_SRC)/%.c
	$(CC) $(CFLAGS) $(LEXBOR_CFLAGS) -c -o $@ $<

$(LEXBOR_LIB): $(LEXBOR_OBJS)
	$(AR) rcs $@ $^

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
	$(RM) $(LEXBOR_OBJS) $(LEXBOR_LIB)

# Print build info
info:
	@echo "minirend Build Configuration"
	@echo "==========================="
	@echo ""
	@echo "Sokol platform files:"
	@echo "  $(SOKOL_SHIM_SRCS)"
	@echo ""
	@echo "Lexbor modules: core, dom, html, css, selectors, style"
	@echo "Lexbor source count: $(words $(LEXBOR_SRCS)) files"
	@echo ""
	@echo "Build targets:"
	@echo "  all   - Build minirend"
	@echo "  clean - Remove build artifacts"
	@echo "  info  - Show this help"

.PHONY: all clean info
