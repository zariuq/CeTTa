SWIPL_LD ?= swipl-ld
CETTA_ROOT ?=
CETTA_OBJ_TAG ?= core.pic

ifeq ($(strip $(CETTA_ROOT)),)
$(error set CETTA_ROOT to the CeTTa foundation checkout)
endif

CETTA_NATIVE_DIR = $(CETTA_ROOT)/experiments/gslt2parse_foundation/native
CETTA_ADAPTER_DIR = $(CETTA_ROOT)/experiments/gslt2parse_foundation/adapters
CETTA_RUNTIME_DIR = $(CETTA_ROOT)/runtime
CETTA_LIBRARY_SOURCE = $(CETTA_RUNTIME_DIR)/libcetta_parser_pack_native_v1-$(CETTA_OBJ_TAG).so
CETTA_LIBRARY_LOCAL = $(notdir $(CETTA_LIBRARY_SOURCE))
CETTA_PIPELINE_LIBRARY_SOURCE = $(CETTA_RUNTIME_DIR)/libcetta_petta_document_pipeline_v1-$(CETTA_OBJ_TAG).so
CETTA_PIPELINE_LIBRARY_LOCAL = $(notdir $(CETTA_PIPELINE_LIBRARY_SOURCE))
FOREIGN_LIBRARY = parser_pack_native_ffi_v1.so
FOREIGN_SOURCE = parser_pack_native_ffi_v1.c
PIPELINE_FOREIGN_LIBRARY = petta_document_pipeline_ffi_v1.so
PIPELINE_FOREIGN_SOURCE = petta_document_pipeline_ffi_v1.c
PETTA_ROOT = $(abspath ../../..)
PETTA_EXPERIMENT_DIR = $(PETTA_ROOT)/experiments/gslt2parse_foundation
CETTA_PRESENTATION_ROOT = $(CETTA_ROOT)/experiments/gslt2parse_foundation/presentations

.PHONY: all
all: $(FOREIGN_LIBRARY) $(PIPELINE_FOREIGN_LIBRARY)

.PHONY: test test-prolog test-surface
test: test-prolog test-surface

test-prolog: all
	cd $(PETTA_EXPERIMENT_DIR) && \
		swipl --stack_limit=8g -q -s test_parser_pack_native_v1.pl -- \
		$(CETTA_PRESENTATION_ROOT)

test-surface: all
	@raw=$$(cd $(PETTA_ROOT) && \
		./run.sh \
			experiments/gslt2parse_foundation/test_parser_pack_native_surface_v1.metta \
			$(CETTA_PRESENTATION_ROOT) --silent 2>&1); \
	status=$$?; \
	result=$$(printf '%s\n' "$$raw" | rg '^(true|completed|unsupported)$$'); \
	expected=$$(printf 'true\ncompleted\nunsupported'); \
	if [ "$$status" -eq 0 ] && [ "$$result" = "$$expected" ]; then \
		echo '(ParserPackNativePeTTaSurfaceV1Summary 1 1 0)'; \
	else \
		printf '%s\n' "$$raw"; \
		exit 1; \
	fi

$(CETTA_LIBRARY_LOCAL): $(CETTA_LIBRARY_SOURCE)
	cp $< $@

$(CETTA_PIPELINE_LIBRARY_LOCAL): $(CETTA_PIPELINE_LIBRARY_SOURCE)
	cp $< $@

$(FOREIGN_LIBRARY): $(FOREIGN_SOURCE) $(CETTA_LIBRARY_LOCAL) $(CETTA_NATIVE_DIR)/parser_pack_native_api_v1.h Makefile
	$(SWIPL_LD) -shared -fPIC -O2 -Wall -Werror \
		-I$(CETTA_NATIVE_DIR) -o $@ $(FOREIGN_SOURCE) \
		-L. -l:$(CETTA_LIBRARY_LOCAL) -Wl,-rpath,'\$$ORIGIN'

$(PIPELINE_FOREIGN_LIBRARY): $(PIPELINE_FOREIGN_SOURCE) $(CETTA_PIPELINE_LIBRARY_LOCAL) $(CETTA_LIBRARY_LOCAL) $(CETTA_ADAPTER_DIR)/petta_document_pipeline_v1.h Makefile
	$(SWIPL_LD) -shared -fPIC -O2 -Wall -Werror \
		-I$(CETTA_ADAPTER_DIR) -I$(CETTA_NATIVE_DIR) -I$(CETTA_ROOT)/src \
		-o $@ $(PIPELINE_FOREIGN_SOURCE) \
		-L. -l:$(CETTA_PIPELINE_LIBRARY_LOCAL) -Wl,-rpath,'\$$ORIGIN'
