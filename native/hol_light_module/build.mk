# The HOL Light kernel module compiled by rulec (tools/rulec), run by CeTTa as the native module
# "holmodule".  generated/ is written by tools/rulec/regen_hol_light_module.sh from the module's
# core rows and validated there (decision DAGs and the generated dispatch code against the rules).
# Names, types and terms are the native kernel's interned objects, id tables arrays; the judgments of
# hol_blocks.table are answered by hol_blocks.c, holr_tables.c and hol_native_ops.c.
HOL_LIGHT_MODULE_SRC = native/rule_runtime/rc_rt.c \
	native/hol_light_module/generated/hol_module.c \
	native/hol_light_module/hol_native_ops.c \
	native/hol_light_module/hol_blocks.c \
	native/hol_light_module/holr_tables.c \
	native/hol_light_module/holr_bridge.c \
	native/hol_light_module/holm_cetta_module.c
SRC += $(HOL_LIGHT_MODULE_SRC)
