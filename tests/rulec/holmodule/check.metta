; The HOL Light kernel module (holmodule): each stream read by the HOLR reader compiled from its GSLT
; modules and checked by the kernel module.  Two streams the kernel accepts, two it refuses (a term
; that is not boolean; an axiom under the sealed policy), and four malformed copies of an accepted one:
; a wrong checksum, the stream cut inside a record, a wrong magic number, a byte after the end record.
!(import! &self holmodule)
!(holm:check "tests/rulec/holmodule/fixtures/p_assume.holr" standard)
!(holm:check "tests/rulec/holmodule/fixtures/p_inst_type.holr" standard)
!(holm:check "tests/rulec/holmodule/fixtures/n_assume_not_bool.holr" standard)
!(holm:check "tests/rulec/holmodule/fixtures/n_new_axiom_sealed.holr" sealed)
!(holm:check "tests/rulec/holmodule/fixtures/m_bad_checksum.holr" standard)
!(holm:check "tests/rulec/holmodule/fixtures/m_truncated.holr" standard)
!(holm:check "tests/rulec/holmodule/fixtures/m_bad_magic.holr" standard)
!(holm:check "tests/rulec/holmodule/fixtures/m_trailing.holr" standard)
!(holm:check "tests/rulec/holmodule/fixtures/no_such_stream.holr" standard)
