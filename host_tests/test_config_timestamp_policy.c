#include "config_timestamp_policy.h"

_Static_assert(REMOTE_CONFIG_TOUCH_TIMESTAMP == EXPECT_REMOTE_TIMESTAMP,
               "Only cloud-only E1002 remote payloads skip timestamp persistence");
int main(void)
{
    return 0;
}
