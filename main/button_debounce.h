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

// LEFT resolves on release so a long hold cannot also move history backward.
typedef enum { BUTTON_HOLD_NONE, BUTTON_HOLD_SHORT, BUTTON_HOLD_LONG } button_hold_event_t;
typedef struct {
    button_debounce_t debounce;
    uint32_t pressed_ms;
    bool armed, consumed;
} button_hold_t;
static inline void button_hold_init(button_hold_t *button, bool high, uint32_t now)
{
    button_debounce_init(&button->debounce, high, now);
    button->pressed_ms = now;
    button->armed = high;
    button->consumed = false;
}
static inline button_hold_event_t button_hold_update(button_hold_t *button, bool high, uint32_t now)
{
    bool was_high = button->debounce.stable_high;
    bool pressed = button_debounce_pressed(&button->debounce, high, now);
    if (pressed && button->armed) { button->pressed_ms = now; button->consumed = false; }
    if (!was_high && button->debounce.stable_high) {
        bool short_press = button->armed && !button->consumed;
        button->armed = true;
        button->consumed = false;
        return short_press ? BUTTON_HOLD_SHORT : BUTTON_HOLD_NONE;
    }
    if (button->armed && !button->debounce.stable_high && !button->consumed &&
        !button->debounce.candidate_high && (uint32_t)(now - button->pressed_ms) >= 5000) {
        button->consumed = true;
        return BUTTON_HOLD_LONG;
    }
    return BUTTON_HOLD_NONE;
}
