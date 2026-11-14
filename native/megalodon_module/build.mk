# The Megalodon kernel module compiled by rulec (tools/rulec), run by CeTTa as the native module
# "mgmodule" (lib/mgmodule.metta).  generated/ is written by tools/rulec/regen_megalodon_module.sh
# from the module's core rows and validated there.  mgm_instance.c is the module's instance of the
# rule runtime, the components and the generic host glue (native/rule_module/rcm_cetta.c): one
# translation unit with the instance's own global names (as every compiled module).
SRC += native/megalodon_module/mgm_instance.c

# Its handles (tests/rulec/mgmodule/handles.metta): a theory kept, extended, read back, released, and
# every refusal (a stale, released or never issued handle; a handle of the wrong sort).
.PHONY: test-mgmodule
test-mgmodule: $(BIN) tests/rulec/mgmodule/handles.metta tests/rulec/mgmodule/handles.expected
	@mkdir -p runtime
	@$(call cetta_exec,./$(BIN)) --lang he tests/rulec/mgmodule/handles.metta > runtime/test-mgmodule.out 2>&1
	@diff -u tests/rulec/mgmodule/handles.expected runtime/test-mgmodule.out
	@echo "PASS: mgmodule keeps a theory under a handle, extends it through call-hold, never answers for the newer theory through an older handle, and refuses stale, released, unissued and mis-sorted handles"
