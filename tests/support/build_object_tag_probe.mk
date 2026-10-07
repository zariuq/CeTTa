CC = gcc
CFLAGS = -O2
PROBE_VALUE ?= 1
CPPFLAGS = -Isrc -DPROBE_VALUE=$(PROBE_VALUE) -include $(BUILD_CONFIG_HEADER)
LDFLAGS = -lm
ENABLE_PYTHON = 0
ENABLE_MORK_STATIC = 0
ENABLE_PATHMAP_SPACE = 0
ENABLE_RUNTIME_STATS = 0
ENABLE_RUNTIME_TIMING = 0
PROBE_CONFIG ?= primary
PROBE_LABEL ?= root
BUILD_CANONICAL_OBJ_TAG = core.$(PROBE_CONFIG)
BUILD_OBJ_TAG = $(BUILD_CANONICAL_OBJ_TAG)
BUILD_CONFIG_HEADER = build_config.$(BUILD_OBJ_TAG).h
TAG_RULES ?= make/build_object_tag.mk
include $(TAG_RULES)

.PHONY: probe matrix return-leg FORCE small-binary
probe:
	@printf 'TagProbe\t%s\t%s\t%s\t%s\n' '$(PROBE_LABEL)' '$(BUILD_OBJ_TAG)' '$(BUILD_TAG_CONFIG_ID)' '$(BUILD_BINARY_TAG)'

matrix: probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) PROBE_LABEL=same probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) PROBE_CONFIG=nogmp PROBE_LABEL=nogmp probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) PROBE_CONFIG=nojson PROBE_LABEL=nojson probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) PROBE_CONFIG=causal PROBE_LABEL=causal probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) ENABLE_RUNTIME_STATS=1 PROBE_LABEL=stats probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) ENABLE_RUNTIME_TIMING=1 PROBE_LABEL=timing probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) CC=clang PROBE_LABEL=compiler probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) CFLAGS='-O0 -DVALUE="a b"' PROBE_LABEL=cflags probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) CPPFLAGS='-DVALUE=2' PROBE_LABEL=cppflags probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) LDFLAGS=-ldl PROBE_LABEL=ldflags probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) ENABLE_PYTHON=1 PROBE_LABEL=python probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) ENABLE_MORK_STATIC=1 PROBE_LABEL=mork probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) ENABLE_PATHMAP_SPACE=1 PROBE_LABEL=pathmap probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) PROBE_CONFIG=nogmp PROBE_LABEL=nested return-leg
ifeq ($(PROBE_LABEL),root)
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) BUILD_OBJ_TAG=another-lane PROBE_LABEL=explicit matrix
endif

return-leg: probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) PROBE_LABEL=nested-same probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) PROBE_CONFIG=nojson PROBE_LABEL=nested-other probe
	@$(MAKE) --no-print-directory -s -f $(firstword $(MAKEFILE_LIST)) PROBE_CONFIG=primary PROBE_LABEL=returned probe

# Actual A -> B -> A compilation under one namespace. The second alias is
# newer than A's retained object, so merely using distinct objects is not enough.
PROBE_OUT ?= runtime/build-object-tag-probe
small-binary: $(PROBE_OUT)/$(BUILD_BINARY_TAG)

$(PROBE_OUT)/$(BUILD_BINARY_TAG): $(PROBE_OUT)/$(BUILD_OBJ_TAG).o $(BUILD_BINARY_FORCE)
	@$(CC) $(CFLAGS) $(filter-out FORCE,$^) -o $@

$(PROBE_OUT)/$(BUILD_OBJ_TAG).o: tests/support/build_object_tag_smoke.c
	@mkdir -p $(PROBE_OUT)
	@$(CC) $(CFLAGS) $(filter-out -include $(BUILD_CONFIG_HEADER),$(CPPFLAGS)) -c $< -o $@

FORCE:
