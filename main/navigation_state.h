#pragma once
#include <stdbool.h>
bool emviary_navigation_pending(void);
bool emviary_navigation_suspend_for_sleep(void);
void emviary_navigation_resume_after_sleep_deferred(void);
