#include "recovery_checkpoint.h"
#include "recovery_protocol.h"
#include <string.h>
bool recovery_checkpoint_valid(const recovery_checkpoint_t *r)
{
    return r->magic == RECOVERY_CHECKPOINT_MAGIC && r->active == 1 &&
           r->boots_left <= RECOVERY_CHECKPOINT_BOOTS && r->writes <= RECOVERY_CHECKPOINT_WRITES &&
           memchr(r->session, 0, sizeof(r->session)) && recovery_identifier_valid(r->session) &&
           memchr(r->stage, 0, sizeof(r->stage)) && memchr(r->error, 0, sizeof(r->error));
}
bool recovery_checkpoint_resume(recovery_checkpoint_t *r, int64_t now, bool scheduled)
{
    if (!recovery_checkpoint_valid(r) || scheduled || !r->boots_left ||
        (now >= 1735689600 && r->expires_at > 0 && now >= r->expires_at)) {
        memset(r, 0, sizeof(*r));
        return false;
    }
    r->boots_left--;
    return true;
}
bool recovery_checkpoint_write_allowed(const recovery_checkpoint_t *r)
{
    return recovery_checkpoint_valid(r) && r->writes < RECOVERY_CHECKPOINT_WRITES;
}
