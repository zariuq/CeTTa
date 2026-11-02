#ifndef CETTA_SPACE_SOURCE_SNAPSHOT_H
#define CETTA_SPACE_SOURCE_SNAPSHOT_H
#include "space.h"
/* Freeze the source's owning scan, including staged rows, without decoding or
 * copying its complete corpus. The initialized empty target is read-only. */
bool space_source_snapshot_init(Space *target, Space *source);
#endif
