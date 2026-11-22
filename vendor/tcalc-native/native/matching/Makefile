CC ?= cc
AR ?= ar
BUILD ?= build
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror
DEVELOPMENT_REFERENCE ?= 0
CPPFLAGS += -I. -I../p1 -I../../tisa -DTISA_DEV_REFERENCE=$(DEVELOPMENT_REFERENCE)

OBJECTS = $(BUILD)/native_component.o $(BUILD)/native_match.o $(BUILD)/native_emit.o $(BUILD)/native_output.o $(BUILD)/native_match_view.o $(BUILD)/native_query.o \
          $(BUILD)/native_admission.o $(BUILD)/native_census.o $(BUILD)/p1_load.o $(BUILD)/tisa_ref.o \
          $(BUILD)/tisa_x86.o $(BUILD)/tisa_print.o $(BUILD)/native_unify.o $(BUILD)/native_emit_image.o $(BUILD)/native_selection.o

.PHONY: all check
TESTS = runtime loader_failure view view_failure query query_failure call_failures \
        selection solution admission admission_failure census census_failure emit output output_failure
all: $(BUILD)/libnative_match.a

ifeq ($(DEVELOPMENT_REFERENCE),1)
all: $(addprefix $(BUILD)/test_,$(TESTS))
endif

$(BUILD):
	mkdir -p "$@"

$(BUILD)/native_component.o: native_component.c native_component.h native_match.h ../p1/p1_load.h ../../tisa/tisa_x86.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/native_match.o: native_match.c native_match.h native_component.h ../p1/p1_load.h ../../tisa/tisa_x86.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/native_emit.o: native_emit.c native_emit.h native_match.h native_component.h emit_layout.generated.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/native_output.o: native_output.c native_output.h native_emit.h native_query.h native_match_view.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/native_match_view.o: native_match_view.c native_match_view.h native_match.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/native_query.o: native_query.c native_query.h native_match_view.h native_match.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/native_admission.o: native_admission.c native_admission.h native_query.h native_match_view.h native_match.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/native_census.o: native_census.c native_census.h native_query.h native_match_view.h native_match.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/p1_load.o: ../p1/p1_load.c ../p1/p1_load.h ../../tisa/tisa.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/tisa_ref.o: ../../tisa/tisa_ref.c ../../tisa/tisa_ref.h ../../tisa/tisa.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/tisa_x86.o: ../../tisa/tisa_x86.c ../../tisa/tisa_x86.h ../../tisa/tisa.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/tisa_print.o: ../../tisa/tisa_print.c ../../tisa/tisa_print.h ../../tisa/tisa.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/native_unify.o: native_unify.S | $(BUILD)
	$(CC) -c $< -o $@
$(BUILD)/native_emit_image.o: native_emit.S | $(BUILD)
	$(CC) -c $< -o $@
$(BUILD)/native_selection.o: native_selection.S | $(BUILD)
	$(CC) -c $< -o $@

$(BUILD)/libnative_match.a: $(OBJECTS)
	rm -f "$@.tmp"
	$(AR) rcs "$@.tmp" $(OBJECTS)
	mv "$@.tmp" "$@"
$(BUILD)/test_runtime: test_runtime.c native_match.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_runtime.c $(BUILD)/libnative_match.a -o $@
$(BUILD)/test_loader_failure: test_loader_failure.c native_match.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_loader_failure.c $(BUILD)/libnative_match.a -Wl,--wrap=realloc -o $@
$(BUILD)/test_view: test_view.c native_match_view.h native_match.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_view.c $(BUILD)/libnative_match.a -o $@
$(BUILD)/test_view_failure: test_view_failure.c native_match_view.h native_match.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_view_failure.c $(BUILD)/libnative_match.a -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -o $@
$(BUILD)/test_query: test_query.c native_query.h native_match_view.h native_match.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_query.c $(BUILD)/libnative_match.a -o $@
$(BUILD)/test_query_failure: test_query_failure.c native_query.h native_match_view.h native_match.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_query_failure.c $(BUILD)/libnative_match.a -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -o $@
$(BUILD)/test_call_failures: test_call_failures.c native_match.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_call_failures.c $(BUILD)/libnative_match.a -Wl,--wrap=malloc -Wl,--wrap=tisa_x86_run -o $@
$(BUILD)/test_selection: test_selection.c $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_selection.c $(BUILD)/libnative_match.a -o $@
$(BUILD)/test_solution: test_solution.c native_match_view.h native_match.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_solution.c $(BUILD)/libnative_match.a -o $@
$(BUILD)/test_admission: test_admission.c native_admission.h native_query.h native_match_view.h native_match.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_admission.c $(BUILD)/libnative_match.a -o $@
$(BUILD)/test_admission_failure: test_admission_failure.c native_admission.h native_query.h native_match_view.h native_match.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_admission_failure.c $(BUILD)/libnative_match.a -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -o $@
$(BUILD)/test_census: test_census.c native_census.h native_query.h native_match_view.h native_match.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_census.c $(BUILD)/libnative_match.a -o $@
$(BUILD)/test_census_failure: test_census_failure.c native_census.h native_query.h native_match_view.h native_match.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_census_failure.c $(BUILD)/libnative_match.a -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -o $@
$(BUILD)/test_emit: test_emit.c native_emit.h native_match.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_emit.c $(BUILD)/libnative_match.a -o $@
$(BUILD)/test_output: test_output.c native_output.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_output.c $(BUILD)/libnative_match.a -o $@
$(BUILD)/test_output_failure: test_output_failure.c native_output.h $(BUILD)/libnative_match.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test_output_failure.c $(BUILD)/libnative_match.a -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -o $@

ifeq ($(DEVELOPMENT_REFERENCE),1)
check: all
	$(BUILD)/test_runtime native_unify_prog.txt
	$(BUILD)/test_loader_failure native_unify_prog.txt
	$(BUILD)/test_view
	$(BUILD)/test_view_failure
	$(BUILD)/test_query
	$(BUILD)/test_query_failure
	$(BUILD)/test_call_failures
	$(BUILD)/test_selection
	$(BUILD)/test_solution
	$(BUILD)/test_admission
	$(BUILD)/test_admission_failure
	$(BUILD)/test_census
	$(BUILD)/test_census_failure
	$(BUILD)/test_emit native_emit_prog.txt
	$(BUILD)/test_output
	$(BUILD)/test_output_failure

else
check:
	$(MAKE) DEVELOPMENT_REFERENCE=1 BUILD="$(abspath $(BUILD))/reference-checks" check
endif

$(OBJECTS): ../../tisa/tisa.h ../../tisa/tisa_ref.h ../../tisa/tisa_x86.h ../../tisa/tisa_print.h ../p1/p1_load.h

# Optional host client. CETTA names a checkout containing native/tcalc;
# the standalone matching library has no CeTTa build dependency.
.PHONY: check-tcalc
ifeq ($(DEVELOPMENT_REFERENCE),1)
check-tcalc: $(BUILD)/libnative_match.a
	@test -n "$(CETTA)" || { echo 'set CETTA to the CeTTa checkout'; exit 1; }
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -D_POSIX_C_SOURCE=200809L -I"$(CETTA)/native/tcalc" \
		test_tcalc_view.c native_tcalc_view.c $(BUILD)/libnative_match.a -o $(BUILD)/test_tcalc_view
	$(BUILD)/test_tcalc_view
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -D_POSIX_C_SOURCE=200809L -I"$(CETTA)/native/tcalc" \
		test_emit_tcalc.c native_tcalc_view.c $(BUILD)/libnative_match.a -o $(BUILD)/test_emit_tcalc
	$(BUILD)/test_emit_tcalc

else
check-tcalc:
	$(MAKE) DEVELOPMENT_REFERENCE=1 BUILD="$(abspath $(BUILD))/reference-checks" CETTA="$(CETTA)" check-tcalc
endif
