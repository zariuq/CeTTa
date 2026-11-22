/* mg_reader_instance.c - Megalodon's syntax presentations (langdef/megalodon/) compiled to C through
 * rulec at build time (runtime/generated/megalodon/reader_v1.generated.c, native/megalodon/test.mk),
 * as one instance of the rule runtime, the atoms component and the generated module, with the
 * reader around them (mg_gslt_reader.c): one translation unit whose global names are its own
 * (reader_v1_names.h, written by tools/rulec/instance_names.sh), but for the reader's API
 * (mg_gslt_reader.h, mg_source.h).  The runtime's heap is per thread (RC_THREAD_HEAP), and each
 * reading keeps its own between steps. */
#include "runtime/generated/megalodon/reader_v1_names.h"
#define RC_THREAD_HEAP
#include "native/rule_runtime/rc_rt.c"
#include "native/rule_components/rc_atoms.c"
#include "runtime/generated/megalodon/reader_v1.generated.c"
#include "native/megalodon/mg_gslt_reader.c"
