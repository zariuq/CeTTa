# The HOLR stream reader compiled by rulec (tools/rulec) from its GSLT reader modules (Mettapedia
# metta/gslt/module/readers/{bytes,hol}): the input boundary of the HOL Light kernel module
# (native/hol_light_module reads every stream through it).  generated/ is written by
# tools/rulec/regen_holr_reader_module.sh and validated there.  Bytes are words, naturals binary
# numbers of any size, byte strings slices of the input; the judgments of holr_reader.bindings are
# answered by the generic components of native/rule_components.  hrm_instance.c is the reader's
# instance: one translation unit with its own global names, exporting the table cetta_holr_reader.
SRC += native/holr_reader_module/hrm_instance.c
