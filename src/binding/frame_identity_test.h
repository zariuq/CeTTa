/* frame_identity_test.h — test access to the frame identity facility.
 *
 * Tests reach the later handle classes without issuing every handle before
 * them: handles are issued in order from here on, and the ones skipped are
 * never issued (their cells are never allocated).  Production builds do not
 * define it. */
#ifndef CETTA_FRAME_IDENTITY_TEST_H
#define CETTA_FRAME_IDENTITY_TEST_H

#include <stdint.h>

#ifdef CETTA_TEST_HOOKS
/* Issue the next fresh handle at `handle`, when that is beyond the next one
 * the facility would issue and below CETTA_FRAME_HANDLE_LIMIT. */
void cetta_frame_identity_test_issue_from(uint32_t handle);
#endif

#endif
