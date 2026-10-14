#ifndef CETTA_HOST_NOTICE_H
#define CETTA_HOST_NOTICE_H
#include <stdint.h>
typedef struct CettaHostNoticeRegistration CettaHostNoticeRegistration;
/* Notification only: no payload, evaluator objects or callbacks. A borrowed
 * nonblocking wake descriptor is usable until unregister returns. */
uint64_t cetta_host_notice_generation(void);
void cetta_host_notice_publish(void);
CettaHostNoticeRegistration *cetta_host_notice_register(int fd);
void cetta_host_notice_unregister(CettaHostNoticeRegistration *registration);
#endif
