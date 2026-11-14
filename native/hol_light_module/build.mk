# The HOL Light kernel module compiled by rulec (tools/rulec), run by CeTTa as the native module
# "holmodule".  generated/ is written by tools/rulec/regen_hol_light_module.sh from the module's
# core rows and validated there (decision DAGs and the generated dispatch code against the rules).
# Names, types and terms are interned nodes, id tables arrays; the judgments of hol.bindings are
# answered by the generic components of native/rule_components.  Streams are read by the HOLR reader
# compiled from its GSLT modules (native/holr_reader_module); of native/hol_light only the stream
# format's constants (opcodes, flags, policies) are used.  hlm_instance.c is the
# module's instance of the rule runtime, the components, the bridge and the host glue: one
# translation unit with the instance's own global names (as every compiled module).
SRC += native/hol_light_module/hlm_instance.c

# The module on fixture streams (tests/rulec/holmodule/check.metta): accepted, refused by the kernel,
# and malformed (a wrong checksum, cut inside a record, a wrong magic number, a byte after the end).
# The rule compiler's own client gate: the lambda calculus compiled with and without its bindings.
.PHONY: test-holmodule test-rulec-lambda
test-holmodule: $(BIN) tests/rulec/holmodule/check.metta tests/rulec/holmodule/check.expected
	@mkdir -p runtime
	@$(call cetta_exec,./$(BIN)) --lang he tests/rulec/holmodule/check.metta > runtime/test-holmodule.out 2>&1
	@diff -u tests/rulec/holmodule/check.expected runtime/test-holmodule.out
	@echo "PASS: holmodule reads every fixture stream through the compiled HOLR reader and gives the kernel's verdicts, refusals and malformations"
test-rulec-lambda: $(BIN)
	@sh tests/rulec/lambda/gate.sh ./$(BIN)
