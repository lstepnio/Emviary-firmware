#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool stable_high;
    bool candidate_high;
    uint32_t changed_ms;
} button_debounce_t;

static inline void button_debounce_init(button_debounce_t *button, bool high, uint32_t now)
{
    *button = (button_debounce_t){high, high, now};
}

// Initialize from the actual pin so a held wake button is not counted twice.
// Fire once after 20 ms of stable low; release rearms without blocking.
static inline bool button_debounce_pressed(button_debounce_t *button, bool high, uint32_t now)
{
    if (high != button->candidate_high) {
        button->candidate_high = high;
        button->changed_ms = now;
    }
    if (high != button->stable_high && (uint32_t)(now - button->changed_ms) >= 20) {
        button->stable_high = high;
        return !high;
    }
    return false;
}
