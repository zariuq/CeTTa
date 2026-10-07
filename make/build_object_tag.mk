# A forced object tag names a namespace, never a configuration-independent
# object address. The primary executable keeps its requested name; recursive
# variants select their own executable and objects, including return legs.
BUILD_BINARY_TAG = $(BUILD_OBJ_TAG)
BUILD_BINARY_FORCE =
ifeq ($(origin BUILD_OBJ_TAG),command line)
build_tag_quote = '$(subst ','"'"',$(1))'
BUILD_TAG_CONFIG_ID := $(shell printf '%s\n' \
	$(call build_tag_quote,$(BUILD_CANONICAL_OBJ_TAG)) \
	$(call build_tag_quote,$(CC)) \
	$(call build_tag_quote,$(CFLAGS)) \
	$(call build_tag_quote,$(filter-out -include $(BUILD_CONFIG_HEADER),$(CPPFLAGS))) \
	$(call build_tag_quote,$(LDFLAGS)) \
	$(call build_tag_quote,$(ENABLE_PYTHON) $(ENABLE_MORK_STATIC) $(ENABLE_PATHMAP_SPACE)) \
	$(call build_tag_quote,$(ENABLE_RUNTIME_STATS) $(ENABLE_RUNTIME_TIMING)) \
	| sha256sum | cut -c1-64)
ifneq ($(CETTA_BUILD_OBJ_TAG_NAMESPACE),$(BUILD_OBJ_TAG))
CETTA_BUILD_OBJ_TAG_NAMESPACE := $(BUILD_OBJ_TAG)
CETTA_BUILD_OBJ_TAG_PRIMARY_CONFIG := $(BUILD_TAG_CONFIG_ID)
endif
export CETTA_BUILD_OBJ_TAG_NAMESPACE CETTA_BUILD_OBJ_TAG_PRIMARY_CONFIG
override BUILD_OBJ_TAG := $(CETTA_BUILD_OBJ_TAG_NAMESPACE).config-$(BUILD_TAG_CONFIG_ID)
ifeq ($(shell test $$(printf '%s' $(call build_tag_quote,$(BUILD_OBJ_TAG)) | wc -c) -gt 160 && printf yes),yes)
override BUILD_OBJ_TAG := tagged.config-$(shell printf '%s\n' \
	$(call build_tag_quote,$(BUILD_OBJ_TAG)) | sha256sum | cut -c1-64)
endif
ifeq ($(CETTA_BUILD_OBJ_TAG_PRIMARY_CONFIG),$(BUILD_TAG_CONFIG_ID))
BUILD_BINARY_TAG := $(CETTA_BUILD_OBJ_TAG_NAMESPACE)
# A separate top-level invocation may reuse the namespace with new options.
# Relink this alias even when that configuration's objects are older than it.
BUILD_BINARY_FORCE = FORCE
endif
endif
