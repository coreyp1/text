SUITE := ghoti.io
PROJECT := text

BUILD ?= release
# The version of this library. MINOR_VERSION carries the minor and the patch as
# one dotted string; the two are split out below for the places that need three
# separate integers. See CONVENTIONS.md section 4.
MAJOR_VERSION := 0
MINOR_VERSION := 0.0
VERSION_MINOR_ONLY := $(word 1,$(subst ., ,$(MINOR_VERSION)))
VERSION_PATCH_ONLY := $(or $(word 2,$(subst ., ,$(MINOR_VERSION))),0)
# Substituted into the .pc file; an empty Version: field makes every
# pkg-config version constraint fail.
VERSION := $(MAJOR_VERSION).$(MINOR_VERSION)

# Names this build everywhere: the .pc file, the install directory, the soname
# and the symbol token. It defaults to the major version, so an ordinary build
# of 1.x is "-1" and two majors cannot be loaded into one process by mistake.
# Override it for a build that wants its own identity:  make BRANCH=-dev
BRANCH ?= -$(MAJOR_VERSION)

# What the library reports as its version. The branch is appended only when it
# is not the default, so an ordinary build says "1.2.3" and an overridden one
# says "1.2.3-dev". Computed before BUILD=debug rewrites BRANCH below.
ifeq ($(BRANCH),-$(MAJOR_VERSION))
VERSION_STRING := $(VERSION)
else
VERSION_STRING := $(VERSION)$(BRANCH)
endif

# If BUILD is debug, append -debug.
#
# "override" because BRANCH may have come from the command line, and a
# command-line variable otherwise wins over a plain assignment here: without it
# `make BRANCH=-dev BUILD=debug` produced a debug build carrying the release
# token, whose symbols collide with the release build's.
ifeq ($(BUILD),debug)
    override BRANCH := $(BRANCH)-debug
    override VERSION_STRING := $(VERSION_STRING)-debug
endif

BASE_NAME := lib$(SUITE)-$(PROJECT)$(BRANCH).so
# The symbol namespace token, from BRANCH, so that the token inside every
# exported symbol is the same one that names the .pc file, the install directory
# and the shared library. See CONVENTIONS.md section 4.
LIBVER_SYMBOL := $(shell echo "ghotiio_$(PROJECT)$(BRANCH)" | sed 's/[.-]/_/g')

BASE_NAME_PREFIX := lib$(SUITE)-$(PROJECT)$(BRANCH)
SO_NAME := $(BASE_NAME).$(MAJOR_VERSION)
ENV_VARS :=

# Detect OS
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Linux)
	OS_NAME := Linux
	LIB_EXTENSION := so
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,-soname,$(SO_NAME)
	TARGET := $(SO_NAME).$(MINOR_VERSION)
	EXE_EXTENSION :=
	# Additional Linux-specific variables
	PKG_CONFIG_PATH := /usr/local/share/pkgconfig
	INCLUDE_INSTALL_PATH := /usr/local/include
	LIB_INSTALL_PATH := /usr/local/lib
	PC_INCLUDE_DIR := $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	PC_LIB_DIR := $(LIB_INSTALL_PATH)/$(SUITE)
	BUILD := linux/$(BUILD)

else ifeq ($(UNAME_S), Darwin)
	OS_NAME := Mac
	LIB_EXTENSION := dylib
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,-install_name,$(BASE_NAME_PREFIX).dylib
	TARGET := $(BASE_NAME_PREFIX).dylib
	EXE_EXTENSION :=
	# Additional macOS-specific variables
	PKG_CONFIG_PATH := /usr/local/share/pkgconfig
	INCLUDE_INSTALL_PATH := /usr/local/include
	LIB_INSTALL_PATH := /usr/local/lib
	PC_INCLUDE_DIR := $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	PC_LIB_DIR := $(LIB_INSTALL_PATH)/$(SUITE)
	BUILD := mac/$(BUILD)

else ifeq ($(findstring MINGW32_NT,$(UNAME_S)),MINGW32_NT)  # 32-bit Windows
	OS_NAME := Windows
	LIB_EXTENSION := dll
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,--out-implib,$(APP_DIR)/$(BASE_NAME_PREFIX).dll.a
	TARGET := $(BASE_NAME_PREFIX).dll
	EXE_EXTENSION := .exe
	# Additional Windows-specific variables
	# This is the path to the pkg-config files on MSYS2
	PKG_CONFIG_PATH := /mingw32/lib/pkgconfig
	INCLUDE_INSTALL_PATH := /mingw32/include
	LIB_INSTALL_PATH := /mingw32/lib
	BIN_INSTALL_PATH := /mingw32/bin
	# Windows paths for .pc so gcc invoked by mingw can resolve -I/-L (cygpath for MSYS2)
	PC_INCLUDE_DIR = $(shell cygpath -m $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH))
	PC_LIB_DIR = $(shell cygpath -m $(LIB_INSTALL_PATH)/$(SUITE))
	BUILD := win32/$(BUILD)

else ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)  # 64-bit Windows
	OS_NAME := Windows
	LIB_EXTENSION := dll
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,--out-implib,$(APP_DIR)/$(BASE_NAME_PREFIX).dll.a
	TARGET := $(BASE_NAME_PREFIX).dll
	EXE_EXTENSION := .exe
	# Additional Windows-specific variables
	# This is the path to the pkg-config files on MSYS2
	PKG_CONFIG_PATH := /mingw64/lib/pkgconfig
	INCLUDE_INSTALL_PATH := /mingw64/include
	LIB_INSTALL_PATH := /mingw64/lib
	BIN_INSTALL_PATH := /mingw64/bin
	# Windows paths for .pc so gcc invoked by mingw can resolve -I/-L (cygpath for MSYS2)
	PC_INCLUDE_DIR = $(shell cygpath -m $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH))
	PC_LIB_DIR = $(shell cygpath -m $(LIB_INSTALL_PATH)/$(SUITE))
	BUILD := win64/$(BUILD)

else
    $(error Unsupported OS: $(UNAME_S))

endif

# ---------------------------------------------------------------------------
# Installation prefix
#
# Defaults to the system location chosen above. Override it to install
# somewhere else - the suite's bootstrap installs every library into a local
# prefix so that each build resolves its dependencies through pkg-config,
# exactly as a consumer would, rather than through a second code path that
# only in-tree builds exercise. See CONVENTIONS.md section 1.
#
#     make install PREFIX=/path/to/prefix
# ---------------------------------------------------------------------------
ifdef PREFIX
INCLUDE_INSTALL_PATH := $(PREFIX)/include
LIB_INSTALL_PATH := $(PREFIX)/lib
BIN_INSTALL_PATH := $(PREFIX)/bin
PKG_CONFIG_PATH := $(PREFIX)/share/pkgconfig
ifeq ($(OS_NAME), Windows)
PC_INCLUDE_DIR = $(shell cygpath -m $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH))
PC_LIB_DIR = $(shell cygpath -m $(LIB_INSTALL_PATH)/$(SUITE))
else
PC_INCLUDE_DIR := $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
PC_LIB_DIR := $(LIB_INSTALL_PATH)/$(SUITE)
endif
# A non-system prefix has no /etc/ld.so.conf.d, and writing to it would need
# root anyway. Everything built here carries an rpath to the prefix instead.
LDCONF_INSTALL_PATH :=
endif


CXX := g++
CXXFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wno-error=unused-function -Wfatal-errors -std=c++20 -O1 -g $(EXTRA_CXXFLAGS)
CC := cc
CFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wno-error=unused-function -Wfatal-errors -std=c17 -O0 -g $(EXTRA_CFLAGS)
# Library-specific compile flags (export symbols on Windows, PIC on Linux)
# GTEXT_BUILD enables DLL export on Windows (checked by GTEXT_API macro)
# GTEXT_TEST_BUILD enables export of internal functions for testing (checked by GTEXT_INTERNAL_API macro)
# No -DGTEXT_TEST_BUILD: the shipped library exports its public API and nothing
# else. Tests reach the internals by linking the static archive, which a static
# link can do even for hidden symbols.
LIB_CFLAGS := $(CFLAGS) -fvisibility=hidden -DGTEXT_BUILD $(EXTRA_CFLAGS)
# -DGHOTIIO_CUTIL_ENABLE_MEMORY_DEBUG
LDFLAGS := -L /usr/lib -lstdc++ -lm $(EXTRA_LDFLAGS)
ifdef PREFIX
# So that a library, a test or an example finds its Ghoti.io dependencies in the
# prefix at run time without LD_LIBRARY_PATH.
LDFLAGS += -Wl,-rpath,$(LIB_INSTALL_PATH)/$(SUITE)
endif

BUILD_DIR := ./build/$(BUILD)
OBJ_DIR := $(BUILD_DIR)/objects
GEN_DIR := $(BUILD_DIR)/generated
APP_DIR := $(BUILD_DIR)/apps


# Add OS-specific flags
ifeq ($(UNAME_S), Linux)
	LIB_CFLAGS += -fPIC

else ifeq ($(UNAME_S), Darwin)

else ifeq ($(findstring MINGW32_NT,$(UNAME_S)),MINGW32_NT)  # 32-bit Windows

else ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)  # 64-bit Windows

else
	$(error Unsupported OS: $(UNAME_S))

endif

# The standard include directories for the project.
INCLUDE := -I include/ -I $(GEN_DIR)/

# ghoti.io-cutil, for GCU_Allocator.  text used to declare its own copy of that
# vtable because CONVENTIONS.md described the library as standalone; that was a
# misreading - a dependency inside the suite is fine as long as the graph stays
# a DAG, and cutil is its root. compress, image and model all take the same
# dependency for the same type, and one definition is what lets an allocator
# written for any of them work with all of them.
CUTIL_PC ?= ghoti.io-cutil$(BRANCH)
CUTIL_CFLAGS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags $(CUTIL_PC) 2>/dev/null)
CUTIL_LIBS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(CUTIL_PC) 2>/dev/null)
ifeq ($(strip $(CUTIL_CFLAGS)),)
ifndef SKIP_DEP_CHECK
$(error ghoti.io-cutil was not found by pkg-config. Run ./bootstrap.sh in the parent folder to build and install the suite into a local prefix, then pass the same PREFIX here - or point PKG_CONFIG_PATH at the directory holding its .pc file. There is deliberately no sibling-checkout fallback: a second resolution path that only in-tree builds exercise is one that silently rots.)
endif
endif
INCLUDE += $(CUTIL_CFLAGS)
# A link dependency, not just a header one: gtext_allocator_default() returns
# cutil's default allocator rather than reimplementing it.
LDFLAGS += $(CUTIL_LIBS)

# Automatically collect all .c source files under the src directory.
SOURCES := $(shell find src -type f -name '*.c')

# Convert each source file path to an object file path.
LIBOBJECTS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SOURCES))


TESTFLAGS := `PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs --cflags gtest gtest_main`


# The static archive, not -l: a static link resolves hidden symbols, so the
# tests can exercise internals the shared library does not export.
# --whole-archive because anything that registers itself from a constructor is
# otherwise dropped - a plain archive link only pulls in object files that
# something references by name.
TEXTLIBRARY := -Wl,--whole-archive $(APP_DIR)/$(STATIC_TARGET) -Wl,--no-whole-archive

# Valgrind configuration for memory checking
VALGRIND_FLAGS := --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 --suppressions=tools/valgrind.supp

# Sanitizer flags (ASan + UBSan)
ASAN_UBSAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g

# Sanitizer build directory
ASAN_BUILD_DIR := $(BUILD_DIR)-asan
ASAN_OBJ_DIR := $(ASAN_BUILD_DIR)/objects
ASAN_APP_DIR := $(ASAN_BUILD_DIR)/apps

# Sanitizer target names
ASAN_TARGET := $(BASE_NAME_PREFIX)-asan.$(LIB_EXTENSION)
# Soname for the instrumented library: its own filename, since it is only ever
# loaded out of the build tree and never installed.
ifeq ($(UNAME_S),Linux)
	ASAN_LIBRARY_NAME_FLAG := -Wl,-soname,$(ASAN_TARGET)
else
	ASAN_LIBRARY_NAME_FLAG :=
endif
STATIC_TARGET := $(BASE_NAME_PREFIX).a
ASAN_STATIC_TARGET := $(BASE_NAME_PREFIX)-asan.a

# Sanitizer object files and library
ASAN_LIBOBJECTS := $(patsubst src/%.c,$(ASAN_OBJ_DIR)/%.o,$(SOURCES))

####################################################################
# Test discovery
####################################################################

# The static archive, not -l: a static link resolves hidden symbols, so the
# tests can exercise internals the shared library does not export.
# --whole-archive because anything that registers itself from a constructor is
# otherwise dropped - a plain archive link only pulls in object files that
# something references by name.
TEXTLIBRARY := -Wl,--whole-archive $(APP_DIR)/$(STATIC_TARGET) -Wl,--no-whole-archive

# Single shell: discover test sources and compute executable name for each (path|name per line).
# test.cpp -> testText; test-yaml-*.cpp -> testYaml*. Avoids hundreds of $(call test-name) / CreateProcess.
# The gates `make test` runs alongside the tests.  A coverage build clears
# this: --coverage links the gcov runtime, which exports mangle_path, and
# check-symbols is right to reject that in a shipping build but it is not a
# defect in an instrumented one.
TEST_GATES ?= check-symbols check-allocators check-headers

TEST_PAIRS := $(shell find tests -type f -name 'test*.cpp' -o -name 'test-*.cpp' 2>/dev/null | sort | while read f; do \
	if [ "$$f" = "tests/test.cpp" ]; then echo "$$f|testText"; \
	else echo "$$f|$$(basename "$$f" .cpp | sed 's/test-/test/g; s/test_/test/g; s/-\([a-z]\)/\U\1/g; s/_\([a-z]\)/\U\1/g; s/^test\([a-z]\)/test\U\1/')"; fi; done)
TEST_SOURCES := $(foreach pair,$(TEST_PAIRS),$(word 1,$(subst |, ,$(pair))))
TEST_NAMES := $(foreach pair,$(TEST_PAIRS),$(word 2,$(subst |, ,$(pair))))

# Generate list of test executables (no $(call test-name) - use precomputed TEST_NAMES)
TEST_EXECUTABLES := $(addprefix $(APP_DIR)/,$(addsuffix $(EXE_EXTENSION),$(TEST_NAMES)))

# ASan test executables
ASAN_TEST_EXECUTABLES := $(patsubst $(APP_DIR)/%,$(ASAN_APP_DIR)/%,$(TEST_EXECUTABLES))

# Automatically collect all example .c files, one directory per module.  A new
# module's examples are picked up by adding it to EXAMPLE_MODULES; yaml was
# omitted here for a long time, so its examples were never compiled and nothing
# noticed when they stopped building.
EXAMPLE_MODULES := json csv yaml

JSON_EXAMPLE_SOURCES := $(shell find examples/json -type f -name '*.c' 2>/dev/null)
CSV_EXAMPLE_SOURCES := $(shell find examples/csv -type f -name '*.c' 2>/dev/null)
YAML_EXAMPLE_SOURCES := $(shell find examples/yaml -type f -name '*.c' 2>/dev/null)
EXAMPLE_SOURCES := $(JSON_EXAMPLE_SOURCES) $(CSV_EXAMPLE_SOURCES) $(YAML_EXAMPLE_SOURCES)

# Convert each example source file path to an executable path.
JSON_EXAMPLES := $(patsubst examples/json/%.c,$(APP_DIR)/examples/json/%$(EXE_EXTENSION),$(JSON_EXAMPLE_SOURCES))
CSV_EXAMPLES := $(patsubst examples/csv/%.c,$(APP_DIR)/examples/csv/%$(EXE_EXTENSION),$(CSV_EXAMPLE_SOURCES))
YAML_EXAMPLES := $(patsubst examples/yaml/%.c,$(APP_DIR)/examples/yaml/%$(EXE_EXTENSION),$(YAML_EXAMPLE_SOURCES))
EXAMPLES := $(JSON_EXAMPLES) $(CSV_EXAMPLES) $(YAML_EXAMPLES)


all: $(APP_DIR)/$(TARGET) $(APP_DIR)/$(STATIC_TARGET) ## Build the shared and static libraries

####################################################################
# Dependency Inclusion
####################################################################

# Explicit list of dependency files (no wildcard: same set on all platforms, faster make startup).
TEST_DEPFILES := $(addprefix $(APP_DIR)/,$(addsuffix .d,$(TEST_NAMES)))
# The ASan build needs these as much as the release build does.  Without them
# a header change rebuilds nothing under release-asan, and the stale objects
# disagree with the freshly built ones about struct layout - which shows up as
# an AddressSanitizer report in code that is correct, and can equally hide a
# report in code that is not.
ASAN_TEST_DEPFILES := $(addprefix $(ASAN_APP_DIR)/,$(addsuffix .d,$(TEST_NAMES)))
DEPFILES := $(LIBOBJECTS:.o=.d) $(TEST_DEPFILES) \
	$(ASAN_LIBOBJECTS:.o=.d) $(ASAN_TEST_DEPFILES)
-include $(DEPFILES)


####################################################################
# Object Files
####################################################################

# Pattern rule for C source files: compile .c files to .o files, generating dependency files.
####################################################################
# Generated version header
####################################################################

LIBVER_GEN := $(GEN_DIR)/ghoti.io/$(PROJECT)/libver_gen.h

# libver_gen.h is regenerated on every build and rewritten only when its content
# changes, so a variable given on the command line - make MAJOR_VERSION=2, or
# make BRANCH=-dev - takes effect. Keying the rule on the Makefile's timestamp
# alone left the previous token and version baked into the build, and nothing
# said so.
.PHONY: force-libver
force-libver:

$(LIBVER_GEN): force-libver
	@if [ -z "$(LIBVER_SYMBOL)" ]; then \
		printf "### LIBVER_SYMBOL is empty ###\n" >&2; \
		printf "Every exported symbol would lose its version namespace.\n" >&2; \
		exit 1; \
	fi
	@mkdir -p $(@D)
	@printf '%s\n' \
		'// Generated by the Makefile. Do not edit; see CONVENTIONS.md section 4.' \
		'#ifndef GHOTI_IO_GTEXT_LIBVER_GEN_H' \
		'#define GHOTI_IO_GTEXT_LIBVER_GEN_H' \
		'' \
		'/** The symbol namespace for this build, from the Makefile'"'"'s BRANCH. */' \
		'#define GHOTIIO_TEXT_NAME $(LIBVER_SYMBOL)' \
		'' \
		'/** Human-readable version of this build. */' \
		'#define GHOTIIO_TEXT_VERSION "$(VERSION_STRING)"' \
		'' \
		'/** The same version as three integers. */' \
		'#define GHOTIIO_TEXT_VERSION_MAJOR $(MAJOR_VERSION)' \
		'#define GHOTIIO_TEXT_VERSION_MINOR $(VERSION_MINOR_ONLY)' \
		'#define GHOTIIO_TEXT_VERSION_PATCH $(VERSION_PATCH_ONLY)' \
		'' \
		'#endif // GHOTI_IO_GTEXT_LIBVER_GEN_H' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

$(OBJ_DIR)/%.o: src/%.c | $(LIBVER_GEN)
	@printf "\n### Compiling $@ ###\n"
	@mkdir -p $(@D)
	$(CC) $(LIB_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

# Pattern rule for C++ source files (if any):
$(OBJ_DIR)/%.o: src/%.cpp
	@printf "\n### Compiling $@ ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@


####################################################################
# Shared Library
####################################################################

$(APP_DIR)/$(STATIC_TARGET): $(LIBOBJECTS)
	@printf "\n### Archiving Text Library ###\n"
	@mkdir -p $(@D)
	@rm -f $@
	ar rcs $@ $^

$(APP_DIR)/$(TARGET): \
		$(LIBOBJECTS)
	@printf "\n### Compiling Text Library ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -shared -o $@ $^ $(LDFLAGS) $(OS_SPECIFIC_LIBRARY_NAME_FLAG)

ifeq ($(OS_NAME), Linux)
	@ln -f -s $(TARGET) $(APP_DIR)/$(SO_NAME)
	@ln -f -s $(SO_NAME) $(APP_DIR)/$(BASE_NAME)
endif

####################################################################
# Unit Tests
####################################################################

# Compile each test .cpp directly to executable. Fewer targets = faster make graph.
# Args: $1 = source path, $2 = executable name (from TEST_PAIRS).
define test-executable-rule
# The library is a normal prerequisite, not an order-only one.  The tests link
# $(STATIC_TARGET) with --whole-archive, so a change to the library has to
# relink them; behind `|` it did not, and `make test` would happily run last
# build's binaries against this build's sources.  That produces both phantom
# failures and, worse, phantom passes.
$(APP_DIR)/$2$(EXE_EXTENSION): \
		$1 \
		$(APP_DIR)/$(TARGET) $(APP_DIR)/$(STATIC_TARGET)
	@printf "\n### Compiling %s Test ###\n" "$2"
	@mkdir -p $$(@D)
	$$(CXX) $$(CXXFLAGS) $$(INCLUDE) -MMD -MP -MF $$(APP_DIR)/$2.d -o $$@ $$< $$(TEXTLIBRARY) $$(LDFLAGS) $$(TESTFLAGS)
endef

# Generate build rules from TEST_PAIRS (one pair = source|name)
$(foreach pair,$(TEST_PAIRS),$(eval $(call test-executable-rule,$(word 1,$(subst |, ,$(pair))),$(word 2,$(subst |, ,$(pair))))))

# tests/test-headers.c is C, and TEST_PAIRS above globs only test*.cpp and
# test-*.cpp - so this file had never been compiled, not once, since it was
# written.  That is why its YAML smoke functions were defined and never called
# from main(), and why it covered no CSV header at all: nothing was ever in a
# position to notice.
#
# It is kept as C rather than renamed to .cpp because every other test in this
# project is C++, and this is the only place a C consumer compiles against
# these headers.  It links the library but not gtest; success is exit 0.
#
# Appended to TEST_EXECUTABLES here, below the point where
# ASAN_TEST_EXECUTABLES is derived from it, so the ASan build does not try to
# apply the C++ test rule to a C source.
$(APP_DIR)/testHeaders$(EXE_EXTENSION): \
		tests/test-headers.c \
		$(APP_DIR)/$(TARGET) $(APP_DIR)/$(STATIC_TARGET)
	@printf "\n### Compiling testHeaders Test ###\n"
	@mkdir -p $(@D)
# -Werror=unused-function overrides the -Wno-error=unused-function in CFLAGS,
# which is set for reasons elsewhere in the tree.  Every smoke function here
# must be reachable from main(); a new one that nobody calls is the defect
# this file already had, so it is a build error here rather than a warning.
	$(CC) $(CFLAGS) -Werror=unused-function $(INCLUDE) -MMD -MP -MF $(APP_DIR)/testHeaders.d -o $@ $< $(TEXTLIBRARY) $(LDFLAGS)

TEST_EXECUTABLES += $(APP_DIR)/testHeaders$(EXE_EXTENSION)

####################################################################
# Examples
####################################################################

# $(TEXTLIBRARY) comes before $(LDFLAGS), as it does in the test rule.  These
# three rules had them the other way round, so -lghoti.io-cutil-0 was offered
# to the linker before the archive that references it and every example failed
# to link with an undefined reference to gcu_allocator_default.  Nothing
# noticed because `make examples` is not part of `make test`; it is a CI step
# now.

# Pattern rule for JSON example executables
# Links the archive, so it depends on the archive: naming only the shared
# library left nothing in the chain that builds the archive, so a clean tree
# could not build the examples at all.
$(APP_DIR)/examples/json/%$(EXE_EXTENSION): examples/json/%.c \
		$(APP_DIR)/$(STATIC_TARGET) | $(APP_DIR)/$(TARGET)
	@printf "\n### Compiling Example: $* ###\n"
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $< $(TEXTLIBRARY) $(LDFLAGS)

# Pattern rule for CSV example executables
# Links the archive, so it depends on the archive: naming only the shared
# library left nothing in the chain that builds the archive, so a clean tree
# could not build the examples at all.
$(APP_DIR)/examples/csv/%$(EXE_EXTENSION): examples/csv/%.c \
		$(APP_DIR)/$(STATIC_TARGET) | $(APP_DIR)/$(TARGET)
	@printf "\n### Compiling Example: $* ###\n"
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $< $(TEXTLIBRARY) $(LDFLAGS)

# Pattern rule for YAML example executables
# Links the archive, so it depends on the archive: naming only the shared
# library left nothing in the chain that builds the archive, so a clean tree
# could not build the examples at all.
$(APP_DIR)/examples/yaml/%$(EXE_EXTENSION): examples/yaml/%.c \
		$(APP_DIR)/$(STATIC_TARGET) | $(APP_DIR)/$(TARGET)
	@printf "\n### Compiling Example: $* ###\n"
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $< $(TEXTLIBRARY) $(LDFLAGS)

####################################################################
# Sanitizer Builds (ASan + UBSan)
####################################################################

# Compile flags for ASan builds (include UBSan for comprehensive checking)
ASAN_CFLAGS := $(CFLAGS) $(ASAN_UBSAN_FLAGS) -DGTEXT_BUILD -DGTEXT_TEST_BUILD
ASAN_CXXFLAGS := $(CXXFLAGS) $(ASAN_UBSAN_FLAGS)
ASAN_LDFLAGS := $(LDFLAGS) $(ASAN_UBSAN_FLAGS)
ASAN_TEXTLIBRARY := -L $(ASAN_APP_DIR) -l$(SUITE)-$(PROJECT)$(BRANCH)-asan

# Add PIC on Linux
ifeq ($(UNAME_S), Linux)
	ASAN_CFLAGS += -fPIC
endif

# Pattern rule for ASan-instrumented C object files
$(ASAN_OBJ_DIR)/%.o: src/%.c
	@printf "\n### Compiling (ASan+UBSan instrumented): $< ###\n"
	@mkdir -p $(@D)
	$(CC) $(ASAN_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

# ASan-instrumented shared library
# The soname has to name this file, not the release library's.  Using
# OS_SPECIFIC_LIBRARY_NAME_FLAG here stamped the instrumented library with the
# release soname, so every ASan test binary recorded a dependency on
# libghoti.io-text-0.so.0 - which is not what this file is called and is not in
# ASAN_APP_DIR.  `make test-asan` died at the first test with "error while
# loading shared libraries" before running anything.
$(ASAN_APP_DIR)/$(ASAN_TARGET): $(ASAN_LIBOBJECTS)
	@printf "\n### Compiling ASan+UBSan-instrumented Shared Library ###\n"
	@mkdir -p $(@D)
	$(CXX) $(ASAN_CXXFLAGS) -shared -o $@ $^ $(ASAN_LDFLAGS) $(ASAN_LIBRARY_NAME_FLAG)

# Pattern rule for ASan test executables - uses same TEST_PAIRS
define asan-test-executable-rule
# Same reasoning as the release rule: a normal prerequisite, so a library
# change relinks the ASan binaries.
$(ASAN_APP_DIR)/$2$(EXE_EXTENSION): \
		$1 \
		$(ASAN_APP_DIR)/$(ASAN_TARGET)
	@printf "\n### Compiling ASan+UBSan %s Test ###\n" "$2"
	@mkdir -p $$(@D)
	$$(CXX) $$(ASAN_CXXFLAGS) $$(INCLUDE) -MMD -MP -MF $$(ASAN_APP_DIR)/$2.d -o $$@ $$< $$(ASAN_LDFLAGS) $$(TESTFLAGS) $$(ASAN_APP_DIR)/$$(ASAN_TARGET)
endef

# Generate ASAN build rules from TEST_PAIRS
$(foreach pair,$(TEST_PAIRS),$(eval $(call asan-test-executable-rule,$(word 1,$(subst |, ,$(pair))),$(word 2,$(subst |, ,$(pair))))))

####################################################################
# Commands
####################################################################

# General commands
.PHONY: clean cloc docs docs-pdf examples help coverage conformance conformance-json conformance-csv conformance-all fuzz fuzz-clean check-symbols check-allocators check-headers
# Release build commands
.PHONY: all install test test-quiet test-valgrind test-valgrind-quiet test-watch uninstall watch
# Debug build commands
.PHONY: all-debug install-debug test-debug test-watch-debug uninstall-debug watch-debug
# Sanitizer commands
.PHONY: test-asan test-asan-quiet test-ubsan sanitizer-help


watch: ## Watch the file directory for changes and compile the target
	@while true; do \
		make --no-print-directory all; \
		printf "\033[0;32m\n"; \
		printf "#########################\n"; \
		printf "# Waiting for changes.. #\n"; \
		printf "#########################\n"; \
		printf "\033[0m\n"; \
		inotifywait -qr -e modify -e create -e delete -e move src include tests Makefile --exclude '/\.'; \
		done

test-watch: ## Watch the file directory for changes and run the unit tests
	@while true; do \
		make --no-print-directory all; \
		make --no-print-directory test; \
		printf "\033[0;32m\n"; \
		printf "#########################\n"; \
		printf "# Waiting for changes.. #\n"; \
		printf "#########################\n"; \
		printf "\033[0m\n"; \
		inotifywait -qr -e modify -e create -e delete -e move src include tests Makefile --exclude '/\.'; \
		done

examples: ## Build all JSON, CSV and YAML examples
examples: $(APP_DIR)/$(TARGET) $(EXAMPLES)
	@printf "\033[0;32m\n"
	@printf "############################\n"
	@printf "### Examples built       ###\n"
	@printf "############################\n"
	@printf "\033[0m\n"
	@printf "JSON examples are available in: $(APP_DIR)/examples/json/\n"
	@printf "CSV examples are available in: $(APP_DIR)/examples/csv/\n"
	@printf "YAML examples are available in: $(APP_DIR)/examples/yaml/\n"
	@printf "\n"
	@printf "\033[0;33mTo run examples:\033[0m\n"
ifeq ($(OS_NAME), Linux)
	@printf "  Linux: Set LD_LIBRARY_PATH to include the library directory:\n"
	@printf "    export LD_LIBRARY_PATH=\"$(APP_DIR):$$LD_LIBRARY_PATH\"\n"
	@printf "    $(APP_DIR)/examples/json/json_basic\n"
	@printf "    $(APP_DIR)/examples/csv/csv_basic\n"
else ifeq ($(OS_NAME), Mac)
	@printf "  macOS: Set DYLD_LIBRARY_PATH to include the library directory:\n"
	@printf "    export DYLD_LIBRARY_PATH=\"$(APP_DIR):$$DYLD_LIBRARY_PATH\"\n"
	@printf "    $(APP_DIR)/examples/json/json_basic\n"
	@printf "    $(APP_DIR)/examples/csv/csv_basic\n"
else ifeq ($(OS_NAME), Windows)
	@printf "  Windows (MSYS2): The DLL must be in the same directory or in PATH.\n"
	@printf "  Option 1 - Run from the library directory:\n"
	@printf "    cd $(APP_DIR)\n"
	@printf "    ./examples/json/json_basic$(EXE_EXTENSION)\n"
	@printf "    ./examples/csv/csv_basic$(EXE_EXTENSION)\n"
	@printf "  Option 2 - Add library directory to PATH:\n"
	@printf "    export PATH=\"$(APP_DIR):$$PATH\"\n"
	@printf "    $(APP_DIR)/examples/json/json_basic$(EXE_EXTENSION)\n"
	@printf "    $(APP_DIR)/examples/csv/csv_basic$(EXE_EXTENSION)\n"
	@printf "  Option 3 - Copy DLL to example directories:\n"
	@printf "    cp $(APP_DIR)/$(TARGET) $(APP_DIR)/examples/json/\n"
	@printf "    cp $(APP_DIR)/$(TARGET) $(APP_DIR)/examples/csv/\n"
	@printf "    Then run: $(APP_DIR)/examples/json/json_basic$(EXE_EXTENSION)\n"
endif
	@printf "\n"


####################################################################
# Symbol namespace check
####################################################################

# Files whose allocations must all go through GTEXT_Allocator.  A file is
# added here when it has been converted; the check then keeps it converted.
# The list is the contract behind the "allocator" field in the parse options:
# without it, one raw malloc() added later would silently reintroduce the
# bypass the option exists to remove.
# src/allocator.c is deliberately absent: it is the default allocator, so the
# C library calls in it are the implementation rather than a bypass.
ALLOCATOR_CLEAN_SOURCES := \
	src/json/json_dom.c \
	src/json/json_lexer.c \
	src/json/json_number.c \
	src/json/json_parser.c

check-allocators: ## Fail if a converted file allocates without the allocator
	@raw=$$(grep -nE '(^|[^_[:alnum:]])(malloc|calloc|realloc|free)[[:space:]]*\(' \
		$(ALLOCATOR_CLEAN_SOURCES) /dev/null \
		| grep -v 'gtext_allocator_' \
		| grep -v 'allocator-exempt' \
		| grep -vE ':[0-9]+:[[:space:]]*(\*|//|/\*)' || true); \
	if [ -n "$$raw" ]; then \
		printf "\033[0;31m\n### Raw allocation in a file that must use GTEXT_Allocator ###\033[0m\n" >&2; \
		printf "%s\n" "$$raw" >&2; \
		printf "\nGTEXT_JSON_Parse_Options::allocator promises that a caller-supplied\n" >&2; \
		printf "allocator sees every allocation the parse makes. A direct malloc() or\n" >&2; \
		printf "free() here breaks that promise silently - the caller cannot detect it,\n" >&2; \
		printf "and a free() through the wrong allocator corrupts the heap.\n" >&2; \
		printf "Use gtext_allocator_malloc()/_calloc()/_realloc()/_free().\n" >&2; \
		exit 1; \
	fi
	@printf "\033[0;32mEvery allocation in the converted files goes through GTEXT_Allocator.\033[0m\n"

check-headers: ## Fail if any installed header is not self-contained
# `make install` copies include/ghoti.io wholesale, so every header under
# include/ ships whether or not anything includes it.  Globbing that directory
# is therefore the authoritative list, and the reason this is a gate rather
# than a test file: tests/test-headers.c was a hand-maintained list of fifteen
# includes, which covered no CSV header at all and did not notice that
# json/json.h had been dead since the great rename.  A list someone has to
# remember to extend is a list that silently stops matching the tree.
#
# Each header is compiled alone, so it must include what it uses; twice, so a
# broken or duplicated include guard shows up as a redefinition; and once as
# C++, because every test in this project is C++ and a header that forgets
# `extern "C"` links against nothing.
	@mkdir -p $(BUILD_DIR)
	@fail=0; checked=0; \
	for h in $$(find include -name '*.h' | sed 's|^include/||' | sort); do \
		checked=$$((checked + 1)); \
		printf '#include <%s>\n#include <%s>\nint main(void) { return 0; }\n' "$$h" "$$h" \
			> $(BUILD_DIR)/hdrcheck.c; \
		if ! $(CC) $(CFLAGS) -I include -I $(GEN_DIR) $(CUTIL_CFLAGS) \
				-c -o /dev/null $(BUILD_DIR)/hdrcheck.c 2> $(BUILD_DIR)/hdrcheck.log; then \
			printf "\033[0;31m\n### %s is not self-contained (C) ###\033[0m\n" "$$h" >&2; \
			sed 's/^/    /' $(BUILD_DIR)/hdrcheck.log >&2; \
			fail=1; \
		fi; \
		cp $(BUILD_DIR)/hdrcheck.c $(BUILD_DIR)/hdrcheck.cpp; \
		if ! $(CXX) $(CXXFLAGS) -I include -I $(GEN_DIR) $(CUTIL_CFLAGS) \
				-c -o /dev/null $(BUILD_DIR)/hdrcheck.cpp 2> $(BUILD_DIR)/hdrcheck.log; then \
			printf "\033[0;31m\n### %s is not self-contained (C++) ###\033[0m\n" "$$h" >&2; \
			sed 's/^/    /' $(BUILD_DIR)/hdrcheck.log >&2; \
			fail=1; \
		fi; \
	done; \
	rm -f $(BUILD_DIR)/hdrcheck.c $(BUILD_DIR)/hdrcheck.cpp $(BUILD_DIR)/hdrcheck.log; \
	if [ "$$fail" -ne 0 ]; then \
		printf "\nA header that does not compile alone works only for callers who\n" >&2; \
		printf "happen to have included its dependencies first.\n" >&2; \
		exit 1; \
	fi; \
	printf "\033[0;32mAll %s installed headers compile standalone, twice, as C and C++.\033[0m\n" "$$checked"

check-symbols: ## Fail if any exported symbol lacks the version namespace
check-symbols: $(APP_DIR)/$(TARGET)
ifeq ($(OS_NAME), Linux)
	@leaked=$$(nm -D --defined-only $(APP_DIR)/$(TARGET) \
		| awk '$$2 ~ /^[TDBR]$$/ {print $$3}' \
		| grep -v '^$(LIBVER_SYMBOL)_' | grep -v '^_' || true); \
	if [ -n "$$leaked" ]; then \
		printf "\033[0;31m\n### Exported symbols missing the $(LIBVER_SYMBOL)_ namespace ###\033[0m\n" >&2; \
		printf "%s\n" "$$leaked" >&2; \
		printf "\nEach needs a '#define <name> GHOTIIO_TEXT(<name>)' line in the header\n" >&2; \
		printf "that declares it. See CONVENTIONS.md section 4.\n" >&2; \
		exit 1; \
	fi
	@unexported=$$(find include -name '*.h' -exec awk '/^#if DOXYGEN/{d=1} d==0 && /^[a-z_][A-Za-z0-9_ ]*\**[[:space:]]*gtext_[a-z0-9_]+[[:space:]]*\(/{print FILENAME": "$$0} /^#endif/{d=0}' {} + \
		| grep -vE 'typedef|static inline' || true); \
	if [ -n "$$unexported" ]; then \
		printf "\033[0;31m\n### Public declarations without GTEXT_API ###\033[0m\n" >&2; \
		printf "%s\n" "$$unexported" >&2; \
		printf "\nThese are hidden in the shared library. The tests link the archive and\n" >&2; \
		printf "would not notice; a consumer gets an undefined reference.\n" >&2; \
		exit 1; \
	fi
	@split=$$(nm -D --undefined-only $(APP_DIR)/$(TARGET) \
		| awk '{print $$2}' | grep '^$(LIBVER_SYMBOL)_' || true); \
	if [ -n "$$split" ]; then \
		printf "\033[0;31m\n### Renamed but undefined - a split symbol ###\033[0m\n" >&2; \
		printf "%s\n" "$$split" >&2; \
		printf "\nA translation unit referenced the namespaced name while the one that\n" >&2; \
		printf "defines it did not see the rename - usually an internal header that\n" >&2; \
		printf "declares or defines something without including macros.h first.\n" >&2; \
		exit 1; \
	fi
	@nomacros=$$(find include src -name '*.h' \
		! -name 'libver.h' ! -name 'libver_gen.h' ! -name 'namespace.h' ! -name 'macros.h' \
		-exec grep -L '#include <ghoti.io/text/macros.h>' {} + || true); \
	if [ -n "$$nomacros" ]; then \
		printf "\033[0;31m\n### Headers that do not include macros.h ###\033[0m\n" >&2; \
		printf "%s\n" "$$nomacros" >&2; \
		printf "\nEvery header must include <ghoti.io/text/macros.h> before it declares\n" >&2; \
		printf "anything, so that the renames in namespace.h are already in effect. A\n" >&2; \
		printf "header that skips it can name a type before that type has been renamed,\n" >&2; \
		printf "producing two different types under one spelling.\n" >&2; \
		printf "See CONVENTIONS.md section 4.\n" >&2; \
		exit 1; \
	fi
	@badguards=$$(find include src -name '*.h' -exec awk 'FNR==1{d=0} !d && /^#ifndef/{print $$2; d=1}' {} + \
		| awk '$$1 !~ /^GHOTI_IO_GTEXT_/ {print $$1}' || true); \
	if [ -n "$$badguards" ]; then \
		printf "\033[0;31m\n### Include guards with the wrong prefix ###\033[0m\n" >&2; \
		printf "%s\n" "$$badguards" >&2; \
		printf "\nGuards mirror the path: GHOTI_IO_GTEXT_<PATH>_H. A guard without the\n" >&2; \
		printf "library token is one rename away from colliding with another library's.\n" >&2; \
		exit 1; \
	fi
	@dupguards=$$(find include src -name '*.h' -exec awk 'FNR==1{d=0} !d && /^#ifndef/{print $$2; d=1}' {} + \
		| sort | uniq -d || true); \
	if [ -n "$$dupguards" ]; then \
		printf "\033[0;31m\n### Headers sharing an include guard ###\033[0m\n" >&2; \
		printf "%s\n" "$$dupguards" >&2; \
		printf "\nTwo headers with one guard means whichever is included second is\n" >&2; \
		printf "silently empty. Guards mirror the path: GHOTI_IO_GTEXT_<PATH>_H.\n" >&2; \
		exit 1; \
	fi
	@printf "\033[0;32mEvery exported symbol carries the $(LIBVER_SYMBOL)_ namespace.\033[0m\n"
	@printf "\033[0;32mEvery public declaration carries GTEXT_API.\033[0m\n"
	@printf "\033[0;32mEvery header includes macros.h.\033[0m\n"
	@printf "\033[0;32mEvery include guard is unique and correctly prefixed.\033[0m\n"
else
	@printf "check-symbols: skipped (Linux only)\n"
endif

test: ## Make and run the Unit tests
test: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES) $(TEST_GATES)
	@printf "\033[0;30;43m\n"
	@printf "############################\n"
	@printf "### Running Text tests   ###\n"
	@printf "############################\n"
	@printf "\033[0m\n\n"
	LD_LIBRARY_PATH="$(APP_DIR)" $(APP_DIR)/testText$(EXE_EXTENSION) --gtest_brief=1

	@printf "\033[0;30;43m\n"
	@printf "############################\n"
	@printf "### Running JSON tests   ###\n"
	@printf "############################\n"
	@printf "\033[0m\n\n"
	LD_LIBRARY_PATH="$(APP_DIR)" $(APP_DIR)/testJson$(EXE_EXTENSION) --gtest_brief=1

	@printf "\033[0;30;43m\n"
	@printf "############################\n"
	@printf "### Running CSV tests    ###\n"
	@printf "############################\n"
	@printf "\033[0m\n\n"
	LD_LIBRARY_PATH="$(APP_DIR)" $(APP_DIR)/testCsv$(EXE_EXTENSION) --gtest_brief=1

	@printf "\n############################\n"
	@printf "### Running All Tests    ###\n"
	@printf "############################\n\n"
# The `|| true` that used to end this line meant `make test` exited 0 with a
# failing suite.  Only the three named runs above could fail the build; the
# other sixty-four were decorative - their failures printed and were discarded.
# Every claim resting on "the suite is green" rested on this loop.
	@failed=""; \
	for test_exe in $(TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		printf "\n### Running $$test_name ###\n"; \
		if ! LD_LIBRARY_PATH="$(APP_DIR)" $$test_exe --gtest_brief=1; then \
			failed="$$failed $$test_name"; \
		fi; \
	done; \
	if [ -n "$$failed" ]; then \
		printf "\033[0;31m\n### Failing suites:%s ###\033[0m\n" "$$failed" >&2; \
		exit 1; \
	fi

test-quiet: ## Run tests with minimal output (one line per test suite)
test-quiet: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES)
	@total_tests=0; total_passed=0; total_failed=0; total_time=0; failed_suites=""; \
	printf "\n\033[1;36m%-30s %8s %10s %s\033[0m\n" "Test Suite" "Tests" "Time" "Status"; \
	printf "\033[1;36m%-30s %8s %10s %s\033[0m\n" "------------------------------" "--------" "----------" "------"; \
	for test_exe in $(TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		output=$$(LD_LIBRARY_PATH="$(APP_DIR)" $$test_exe --gtest_brief=1 2>&1); \
		exit_code=$$?; \
		num_tests=$$(echo "$$output" | grep -oP '\[\s*=+\s*\]\s*\K\d+(?=\s+tests?)' | head -1); \
		time_ms=$$(echo "$$output" | grep -oP '\(\K\d+(?=\s*ms\s*total\))' | head -1); \
		[ -z "$$num_tests" ] && num_tests=0; \
		[ -z "$$time_ms" ] && time_ms=0; \
		total_tests=$$((total_tests + num_tests)); \
		total_time=$$((total_time + time_ms)); \
		if [ $$exit_code -eq 0 ]; then \
			total_passed=$$((total_passed + num_tests)); \
			printf "%-30s %8d %8dms \033[0;32mPASS\033[0m\n" "$$test_name" "$$num_tests" "$$time_ms"; \
		else \
			failures=$$(echo "$$output" | grep -oP '\[\s*FAILED\s*\]\s*\K\d+' | head -1); \
			[ -z "$$failures" ] && failures=$$num_tests; \
			total_failed=$$((total_failed + failures)); \
			total_passed=$$((total_passed + num_tests - failures)); \
			printf "%-30s %8d %8dms \033[0;31mFAIL\033[0m\n" "$$test_name" "$$num_tests" "$$time_ms"; \
			failed_suites="$$failed_suites\n\033[0;31m=== $$test_name FAILURES ===\033[0m\n$$output\n"; \
		fi; \
	done; \
	printf "\033[1;36m%-30s %8s %10s %s\033[0m\n" "------------------------------" "--------" "----------" "------"; \
	if [ $$total_failed -eq 0 ]; then \
		printf "\033[0;32m%-30s %8d %6dms PASS\033[0m\n\n" "TOTAL" "$$total_tests" "$$total_time"; \
	else \
		printf "\033[0;31m%-30s %8d %6dms FAIL (%d failed)\033[0m\n" "TOTAL" "$$total_tests" "$$total_time" "$$total_failed"; \
		printf "$$failed_suites\n"; \
		exit 1; \
	fi

test-valgrind: ## Run all tests under valgrind (Linux only)
test-valgrind: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES)
ifeq ($(OS_NAME), Linux)
# VALGRIND_FLAGS carries --error-exitcode=1, so valgrind already reports a
# memory error as a non-zero status.  This loop simply did not look at it.
	@failed=""; \
	for test_exe in $(TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		printf "\033[0;30;43m\n"; \
		printf "############################\n"; \
		printf "### Running %s tests under Valgrind ###\n" "$$test_name"; \
		printf "############################"; \
		printf "\033[0m\n\n"; \
		if ! LD_LIBRARY_PATH="$(APP_DIR)" valgrind $(VALGRIND_FLAGS) $$test_exe --gtest_brief=1; then \
			failed="$$failed $$test_name"; \
		fi; \
	done; \
	if [ -n "$$failed" ]; then \
		printf "\033[0;31m\n### Suites failing under valgrind:%s ###\033[0m\n" "$$failed" >&2; \
		exit 1; \
	fi
else
	@printf "\033[0;31m\n"
	@printf "Valgrind is only available on Linux\n"
	@printf "\033[0m\n"
	@exit 1
endif

test-valgrind-quiet: ## Run tests under valgrind with minimal output (Linux only)
test-valgrind-quiet: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES)
ifeq ($(OS_NAME), Linux)
	@total_tests=0; total_passed=0; total_failed=0; total_time=0; failed_suites=""; \
	printf "\n\033[1;35m%-30s %8s %10s %s\033[0m\n" "Test Suite (Valgrind)" "Tests" "Time" "Status"; \
	printf "\033[1;35m%-30s %8s %10s %s\033[0m\n" "------------------------------" "--------" "----------" "------"; \
	for test_exe in $(TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		output=$$(LD_LIBRARY_PATH="$(APP_DIR)" valgrind $(VALGRIND_FLAGS) $$test_exe --gtest_brief=1 2>&1); \
		exit_code=$$?; \
		num_tests=$$(echo "$$output" | grep -oP '\[\s*=+\s*\]\s*\K\d+(?=\s+tests?)' | head -1); \
		time_ms=$$(echo "$$output" | grep -oP '\(\K\d+(?=\s*ms\s*total\))' | head -1); \
		[ -z "$$num_tests" ] && num_tests=0; \
		[ -z "$$time_ms" ] && time_ms=0; \
		total_tests=$$((total_tests + num_tests)); \
		total_time=$$((total_time + time_ms)); \
		has_leak=$$(echo "$$output" | grep -c "are definitely lost\|are indirectly lost\|are possibly lost" || true); \
		if [ $$exit_code -eq 0 ] && [ $$has_leak -eq 0 ]; then \
			total_passed=$$((total_passed + num_tests)); \
			printf "%-30s %8d %8dms \033[0;32mPASS\033[0m\n" "$$test_name" "$$num_tests" "$$time_ms"; \
		else \
			failures=$$(echo "$$output" | grep -oP '\[\s*FAILED\s*\]\s*\K\d+' | head -1); \
			[ -z "$$failures" ] && failures=0; \
			if [ $$has_leak -gt 0 ]; then \
				total_failed=$$((total_failed + num_tests)); \
				printf "%-30s %8d %8dms \033[0;31mLEAK\033[0m\n" "$$test_name" "$$num_tests" "$$time_ms"; \
			else \
				total_failed=$$((total_failed + failures)); \
				total_passed=$$((total_passed + num_tests - failures)); \
				printf "%-30s %8d %8dms \033[0;31mFAIL\033[0m\n" "$$test_name" "$$num_tests" "$$time_ms"; \
			fi; \
			failed_suites="$$failed_suites\n\033[0;31m=== $$test_name FAILURES/LEAKS ===\033[0m\n$$output\n"; \
		fi; \
	done; \
	printf "\033[1;35m%-30s %8s %10s %s\033[0m\n" "------------------------------" "--------" "----------" "------"; \
	if [ $$total_failed -eq 0 ]; then \
		printf "\033[0;32m%-30s %8d %6dms PASS\033[0m\n\n" "TOTAL" "$$total_tests" "$$total_time"; \
	else \
		printf "\033[0;31m%-30s %8d %6dms FAIL (%d failed)\033[0m\n" "TOTAL" "$$total_tests" "$$total_time" "$$total_failed"; \
		printf "$$failed_suites\n"; \
		exit 1; \
	fi
else
	@printf "\033[0;31m\n"
	@printf "Valgrind is only available on Linux\n"
	@printf "\033[0m\n"
	@exit 1
endif

test-asan: ## Run all tests with AddressSanitizer + UndefinedBehaviorSanitizer (Linux only)
# LD_PRELOAD is cleared for each run: the ASan runtime has to be first in the
# initial library list, and a desktop-wide preload (Debian sets
# libgtk3-nocsd.so.0 in many sessions) gets ahead of it, at which point ASan
# aborts before the test starts.
test-asan: $(ASAN_APP_DIR)/$(ASAN_TARGET) $(ASAN_TEST_EXECUTABLES)
ifeq ($(OS_NAME), Linux)
	@printf "\033[0;36m\n"
	@printf "###########################################\n"
	@printf "### Running tests with ASan + UBSan    ###\n"
	@printf "###########################################\n"
	@printf "\033[0m\n"
# allocator_may_return_null=1 makes an oversized request return NULL instead of
# aborting the process.  Without it ASan treats "too big to allocate" as a fatal
# error, which makes every out-of-memory path in this library untestable under
# the sanitizer - the buffer growth tests ask for SIZE_MAX-sized capacities on
# purpose, to reach the overflow branches, and expect the documented
# GTEXT_*_E_OOM back.  Leak detection and every other check are unaffected.
	@for test_exe in $(ASAN_TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		printf "\033[0;30;43m\n"; \
		printf "############################\n"; \
		printf "### Running %s tests (ASan+UBSan) ###\n" "$$test_name"; \
		printf "############################"; \
		printf "\033[0m\n\n"; \
		LD_PRELOAD= LD_LIBRARY_PATH="$(ASAN_APP_DIR)" ASAN_OPTIONS=detect_leaks=1:allocator_may_return_null=1 UBSAN_OPTIONS=print_stacktrace=1 $$test_exe --gtest_brief=1 || exit 1; \
	done
	@printf "\033[0;32m\n"
	@printf "###########################################\n"
	@printf "### All tests passed with ASan + UBSan ###\n"
	@printf "###########################################\n"
	@printf "\033[0m\n"
else
	@printf "\033[0;31m\n"
	@printf "Sanitizer builds are currently only supported on Linux\n"
	@printf "\033[0m\n"
	@exit 1
endif

test-ubsan: ## Alias for test-asan (ASan+UBSan are run together)
test-ubsan: test-asan

test-asan-quiet: ## Run ASan+UBSan tests with minimal output (Linux only)
test-asan-quiet: $(ASAN_APP_DIR)/$(ASAN_TARGET) $(ASAN_TEST_EXECUTABLES)
ifeq ($(OS_NAME), Linux)
	@total_tests=0; total_passed=0; total_failed=0; total_time=0; failed_suites=""; \
	printf "\n\033[1;33m%-30s %8s %10s %s\033[0m\n" "Test Suite (ASan+UBSan)" "Tests" "Time" "Status"; \
	printf "\033[1;33m%-30s %8s %10s %s\033[0m\n" "------------------------------" "--------" "----------" "------"; \
	for test_exe in $(ASAN_TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		output=$$(LD_PRELOAD= LD_LIBRARY_PATH="$(ASAN_APP_DIR)" ASAN_OPTIONS=detect_leaks=1:allocator_may_return_null=1 UBSAN_OPTIONS=print_stacktrace=1 $$test_exe --gtest_brief=1 2>&1); \
		exit_code=$$?; \
		num_tests=$$(echo "$$output" | grep -oP '\[\s*=+\s*\]\s*\K\d+(?=\s+tests?)' | head -1); \
		time_ms=$$(echo "$$output" | grep -oP '\(\K\d+(?=\s*ms\s*total\))' | head -1); \
		[ -z "$$num_tests" ] && num_tests=0; \
		[ -z "$$time_ms" ] && time_ms=0; \
		total_tests=$$((total_tests + num_tests)); \
		total_time=$$((total_time + time_ms)); \
		if [ $$exit_code -eq 0 ]; then \
			total_passed=$$((total_passed + num_tests)); \
			printf "%-30s %8d %8dms \033[0;32mPASS\033[0m\n" "$$test_name" "$$num_tests" "$$time_ms"; \
		else \
			failures=$$(echo "$$output" | grep -oP '\[\s*FAILED\s*\]\s*\K\d+' | head -1); \
			[ -z "$$failures" ] && failures=$$num_tests; \
			total_failed=$$((total_failed + failures)); \
			total_passed=$$((total_passed + num_tests - failures)); \
			printf "%-30s %8d %8dms \033[0;31mFAIL\033[0m\n" "$$test_name" "$$num_tests" "$$time_ms"; \
			failed_suites="$$failed_suites\n\033[0;31m=== $$test_name FAILURES ===\033[0m\n$$output\n"; \
		fi; \
	done; \
	printf "\033[1;33m%-30s %8s %10s %s\033[0m\n" "------------------------------" "--------" "----------" "------"; \
	if [ $$total_failed -eq 0 ]; then \
		printf "\033[0;32m%-30s %8d %6dms PASS\033[0m\n\n" "TOTAL" "$$total_tests" "$$total_time"; \
	else \
		printf "\033[0;31m%-30s %8d %6dms FAIL (%d failed)\033[0m\n" "TOTAL" "$$total_tests" "$$total_time" "$$total_failed"; \
		printf "$$failed_suites\n"; \
		exit 1; \
	fi
else
	@printf "\033[0;31m\n"
	@printf "Sanitizer builds are currently only supported on Linux\n"
	@printf "\033[0m\n"
	@exit 1
endif

sanitizer-help: ## Show help for sanitizer usage
	@printf "\033[1;36m\n"
	@printf "##############################################\n"
	@printf "### Sanitizer Testing (ASan + UBSan)      ###\n"
	@printf "##############################################\n"
	@printf "\033[0m\n"
	@printf "AddressSanitizer (ASan) detects:\n"
	@printf "  - Memory leaks\n"
	@printf "  - Use-after-free\n"
	@printf "  - Buffer overflows\n"
	@printf "  - Stack/heap corruption\n"
	@printf "\n"
	@printf "UndefinedBehaviorSanitizer (UBSan) detects:\n"
	@printf "  - Integer overflow/underflow\n"
	@printf "  - Null pointer dereference\n"
	@printf "  - Unaligned memory access\n"
	@printf "  - Division by zero\n"
	@printf "\n"
	@printf "Usage:\n"
	@printf "  make test-asan         - Run all tests with sanitizers (verbose)\n"
	@printf "  make test-asan-quiet   - Run all tests with sanitizers (summary)\n"
	@printf "  make test-ubsan        - Alias for test-asan\n"
	@printf "\n"
	@printf "Sanitizers build a separate instrumented library in:\n"
	@printf "  $(ASAN_APP_DIR)/\n"
	@printf "\n"
	@printf "Note: Sanitizer builds are slower but catch more bugs.\n"
	@printf "      Use them before commits or releases.\n"
	@printf "\n"

# Removed duplicated run block and empty target stubs that previously caused
# "overriding recipe" warnings. The YAML test targets are defined above.

clean: ## Remove all contents of the build directories.
	-@rm -rvf $(BUILD_DIR)

# Files will be as follows:
# /usr/local/lib/(SUITE)/
#   lib(SUITE)-(PROJECT)(BRANCH).so.(MAJOR).(MINOR)
#   lib(SUITE)-(PROJECT)(BRANCH).so.(MAJOR) link to previous
#   lib(SUITE)-(PROJECT)(BRANCH).so link to previous
# Where the dynamic loader configuration fragment goes. Overridable so a
# staged or user-prefix install has somewhere to write it; the default is the
# system location, which is what an ordinary `sudo make install` uses.
LDCONF_INSTALL_PATH ?= /etc/ld.so.conf.d

# Dependencies a consumer of this library needs on its own include path.
PC_REQUIRES := $(CUTIL_PC)

# Where this project's own .pc file is installed. Defaults to the directory
# pkg-config is already being told to search, but separate from it so a
# staged install can write somewhere else without also redirecting lookups.
PKGCONFIG_INSTALL_PATH ?= $(PKG_CONFIG_PATH)

# $(LDCONF_INSTALL_PATH)/(SUITE)-(PROJECT)(BRANCH).conf will point to $(LIB_INSTALL_PATH)/(SUITE)
# /usr/local/include/(SUITE)/(PROJECT)(BRANCH)
#   *.h copied from ./include/(PROJECT)
# /usr/local/share/pkgconfig
#   (SUITE)-(PROJECT)(BRANCH).pc created

install: ## Install the library globally, requires sudo
# Depends on all: install used to copy whatever happened to be in the build
# directory, so it could install a stale artifact or fail outright on a clean
# tree.
install: all
	# Installing the shared library.
	@mkdir -p $(LIB_INSTALL_PATH)/$(SUITE)
ifeq ($(OS_NAME), Linux)
# Install the .so file
	@cp $(APP_DIR)/$(TARGET) $(LIB_INSTALL_PATH)/$(SUITE)/
	@ln -f -s $(TARGET) $(LIB_INSTALL_PATH)/$(SUITE)/$(SO_NAME)
	@ln -f -s $(SO_NAME) $(LIB_INSTALL_PATH)/$(SUITE)/$(BASE_NAME)
	# Installing the ld configuration file.
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then mkdir -p $(LDCONF_INSTALL_PATH); fi
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then echo "$(LIB_INSTALL_PATH)/$(SUITE)" > $(LDCONF_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).conf; fi
endif
ifeq ($(OS_NAME), Windows)
# The .dll file and the .dll.a file
	@mkdir -p $(BIN_INSTALL_PATH)/$(SUITE)
	@cp $(APP_DIR)/$(TARGET).a $(LIB_INSTALL_PATH)
	@cp $(APP_DIR)/$(TARGET) $(BIN_INSTALL_PATH)
endif
	# Installing the headers.
	# Removed first: this directory is owned entirely by this project and
	# branch, and copying over the top of it would leave headers behind that
	# have since been renamed or deleted.
	@rm -rf $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	@mkdir -p $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	@if [ -d include/ghoti.io ]; then \
		cp -r include/ghoti.io $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/ ; \
	fi
	@if [ -n "$$(find $(GEN_DIR) -maxdepth 1 -name '*.h' 2>/dev/null)" ]; then \
		mkdir -p $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/ghoti.io/$(PROJECT); \
		cp $(GEN_DIR)/*.h $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/ghoti.io/$(PROJECT)/; \
	fi
	@if [ -d $(GEN_DIR)/ghoti.io ]; then \
		cp -r $(GEN_DIR)/ghoti.io $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/ ; \
	fi
	# Installing the pkg-config files.
	@mkdir -p $(PKGCONFIG_INSTALL_PATH)
	@cat pkgconfig/$(SUITE)-$(PROJECT).pc | sed 's/(SUITE)/$(SUITE)/g; s/(PROJECT)/$(PROJECT)/g; s/(BRANCH)/$(BRANCH)/g; s/(VERSION)/$(VERSION)/g; s|(PC_LIB_DIR)|$(PC_LIB_DIR)|g; s|(PC_INCLUDE_DIR)|$(PC_INCLUDE_DIR)|g; s|(REQUIRES)|$(PC_REQUIRES)|g' > $(PKGCONFIG_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).pc
ifeq ($(OS_NAME), Linux)
	# Running ldconfig.
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then ldconfig >> /dev/null 2>&1; fi
endif
	@echo "Ghoti.io $(PROJECT)$(BRANCH) installed"

uninstall: ## Delete the globally-installed files.  Requires sudo.
	# Deleting the shared library.
ifeq ($(OS_NAME), Linux)
	@rm -f $(LIB_INSTALL_PATH)/$(SUITE)/$(BASE_NAME)*
	# Deleting the ld configuration file.
	@rm -f $(LDCONF_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).conf
endif
ifeq ($(OS_NAME), Windows)
	@rm -f $(LIB_INSTALL_PATH)/$(TARGET).a
	@rm -f $(BIN_INSTALL_PATH)/$(TARGET)
endif
	# Deleting the headers.
	@rm -rf $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	# Deleting the pkg-config files.
	@rm -f $(PKGCONFIG_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).pc
	# Cleaning up (potentially) no longer needed directories.
	@rmdir --ignore-fail-on-non-empty $(INCLUDE_INSTALL_PATH)/$(SUITE)
	@rmdir --ignore-fail-on-non-empty $(LIB_INSTALL_PATH)/$(SUITE)
ifeq ($(OS_NAME), Linux)
	# Running ldconfig.
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then ldconfig >> /dev/null 2>&1; fi
endif
	@echo "Ghoti.io $(PROJECT)$(BRANCH) has been uninstalled"

debug: ## Build the project in DEBUG mode
	make all BUILD=debug

install-debug: ## Install the DEBUG library globally, requires sudo
	make install BUILD=debug

uninstall-debug: ## Delete the DEBUG globally-installed files.  Requires sudo.
	make uninstall BUILD=debug

test-debug: ## Make and run the Unit tests in DEBUG mode
	make test BUILD=debug

watch-debug: ## Watch the file directory for changes and compile the target in DEBUG mode
	make watch BUILD=debug

test-watch-debug: ## Watch the file directory for changes and run the unit tests in DEBUG mode
	make test-watch BUILD=debug

docs: ## Generate the documentation in the ./docs subdirectory
	doxygen

docs-pdf: docs ## Generate the documentation as a pdf, at ./docs/(SUITE)-(PROJECT)(BRANCH).pdf
	cd ./docs/latex/ && make
	mv -f ./docs/latex/refman.pdf ./docs/$(SUITE)-$(PROJECT)$(BRANCH)-docs.pdf

cloc: ## Count the lines of code used in the project
	cloc src include tests Makefile

####################################################################
# Fuzzing (libFuzzer)
####################################################################
# The library sources are recompiled with clang's coverage instrumentation
# and linked into the harness, rather than the harness linking the ordinary
# shared library. That matters: libFuzzer steers its mutations by the
# coverage it observes, and against an uninstrumented library it sees only
# the harness file and degrades into random input generation.
#
# AddressSanitizer and UndefinedBehaviorSanitizer are on, since a parser
# reading one byte past a buffer is exactly the bug being looked for and it
# will not usually crash on its own.
FUZZ_CC ?= clang
FUZZ_CXX ?= clang++
FUZZ_CC_OK := $(shell which $(FUZZ_CC) 2>/dev/null)
FUZZ_SAN := -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1
FUZZ_LIB_FLAGS := $(FUZZ_SAN) -fsanitize=fuzzer-no-link
FUZZ_BIN_FLAGS := $(FUZZ_SAN) -fsanitize=fuzzer
# The fuzzers link cutil like everything else, so they need the same rpath the
# test binaries get from LDFLAGS; FUZZ_BIN_FLAGS does not include LDFLAGS.
ifdef PREFIX
FUZZ_RPATH := -Wl,-rpath,$(LIB_INSTALL_PATH)/$(SUITE)
endif
FUZZ_DIR := $(BUILD_DIR)/fuzz
FUZZ_OBJ_DIR := $(FUZZ_DIR)/objects
FUZZ_APP_DIR := $(FUZZ_DIR)/apps
FUZZ_OBJECTS := $(patsubst src/%.c,$(FUZZ_OBJ_DIR)/%.o,$(SOURCES))
# Included here rather than added to DEPFILES, which is simply-expanded and
# defined long before FUZZ_OBJECTS exists.
-include $(FUZZ_OBJECTS:.o=.d)
FUZZ_CORPUS := tests/fuzz/corpus
# Long enough to be worth running, short enough for a coffee. Override for a
# real campaign: make fuzz FUZZ_TIME=3600
FUZZ_TIME ?= 60

# -MMD -MP -MF for the same reason as the release and ASan rules: without it a
# header change rebuilds nothing here, and the stale objects disagree with the
# fresh ones about struct layout.  That shows up as a fuzzer "finding" -
# a _Bool loaded as 255, a SEGV in free() - in code that is correct.
$(FUZZ_OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	@$(FUZZ_CC) $(FUZZ_LIB_FLAGS) -std=c17 -w $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

# $1 = harness basename (fuzz_json), $2 = target suffix (json)
define fuzz-rule
fuzz-$2: ## Build the $2 fuzz harness (requires clang)
fuzz-$2: $$(FUZZ_APP_DIR)/$1

$$(FUZZ_APP_DIR)/$1: tests/fuzz/$1.cpp $$(FUZZ_OBJECTS)
	@if [ -z "$$(FUZZ_CC_OK)" ]; then \
		echo "fuzzing requires $$(FUZZ_CXX); install clang or set FUZZ_CC/FUZZ_CXX"; \
		exit 1; \
	fi
	@mkdir -p $$(@D) $$(FUZZ_CORPUS)
	@printf "\n### Building $1 ###\n"
	$$(FUZZ_CXX) $$(FUZZ_BIN_FLAGS) -std=c++20 -w $$(INCLUDE) \
		-o $$@ $$< $$(FUZZ_OBJECTS) $$(CUTIL_LIBS) $$(FUZZ_RPATH)

fuzz-run-$2: ## Run the $2 fuzzer for $$(FUZZ_TIME) seconds
fuzz-run-$2: $$(FUZZ_APP_DIR)/$1
	@mkdir -p $$(FUZZ_CORPUS)/$2
	@printf "\n### Fuzzing $2 for $$(FUZZ_TIME)s ###\n"
	@$$(FUZZ_APP_DIR)/$1 $$(FUZZ_CORPUS)/$2 \
		-max_total_time=$$(FUZZ_TIME) -print_final_stats=1
endef

$(eval $(call fuzz-rule,fuzz_json,json))
$(eval $(call fuzz-rule,fuzz_yaml,yaml))
$(eval $(call fuzz-rule,fuzz_csv,csv))

fuzz: ## Build and run every fuzzer for $(FUZZ_TIME) seconds each
fuzz: fuzz-run-json fuzz-run-yaml fuzz-run-csv

fuzz-clean: ## Remove the fuzz build (keeps the corpus)
fuzz-clean:
	-@rm -rf $(FUZZ_DIR)

conformance: ## Score the YAML parser against yaml-test-suite (clones it on first use)
conformance:
	@PREFIX="$(PREFIX)" tools/conformance/run.sh

conformance-json: ## Score the JSON parser against JSONTestSuite (clones it on first use)
conformance-json:
	@PREFIX="$(PREFIX)" tools/conformance/run-json.sh

conformance-csv: ## Score the CSV parser against csv-spectrum (clones it on first use)
conformance-csv:
	@PREFIX="$(PREFIX)" tools/conformance/run-csv.sh

conformance-all: ## Score all three parsers against their external corpora
conformance-all: conformance conformance-json conformance-csv

coverage: ## Build instrumented, run the tests, and report line coverage
# Cleans first because the object files would otherwise be reused without the
# instrumentation, then cleans and rebuilds at the end: leaving the
# instrumented objects behind would have a later `make` silently link them,
# and leaving the tree cleaned would break any sibling project that links
# this one. The cost is one extra build; coverage is not run often.
	@$(MAKE) --no-print-directory clean > /dev/null
# The instrumented build, the report, and the restoration of the tree are one
# shell command so that the cleanup runs whatever fails.  Letting a failure
# stop the recipe leaves the --coverage objects in build/, and the next
# ordinary `make` links them into a library that needs the gcov runtime; every
# later build then fails with undefined references to __gcov_init until
# somebody works out why.  That is exactly what the cleanup exists to prevent,
# so it must not itself be skipped by the failure it is there to survive.
#
# TEST_GATES is cleared because --coverage links the gcov runtime, which
# exports mangle_path.  check-symbols is right to reject that in a shipping
# build, but it is not a defect in an instrumented one, and it made this
# target fail before it ever produced a report.
#
# COVERAGE_MIN, when set, makes the report fail below that percentage.  CI
# passes one so that coverage can only be argued upward; a local run without it
# just prints the numbers.
	@status=0; \
	$(MAKE) --no-print-directory test TEST_GATES= \
		EXTRA_CFLAGS="--coverage -O0" \
		EXTRA_LDFLAGS="--coverage" > /dev/null || status=$$?; \
	if [ $$status -eq 0 ]; then \
		COVERAGE_MIN=$(COVERAGE_MIN) tools/coverage.sh $(OBJ_DIR) || status=$$?; \
	else \
		printf "coverage: the instrumented test run failed; no report\n" >&2; \
	fi; \
	$(MAKE) --no-print-directory clean > /dev/null; \
	$(MAKE) --no-print-directory all > /dev/null; \
	exit $$status

help: ## Display this help
	@grep -E '^[ a-zA-Z_-]+:.*?## .*$$' Makefile | sort | sed 's/\([^:]*\):.*## \(.*\)/\1:\2/' | awk -F: '{printf "%-15s %s\n", $$1, $$2}' | sed "s/(SUITE)/$(SUITE)/g; s/(PROJECT)/$(PROJECT)/g; s/(BRANCH)/$(BRANCH)/g"
