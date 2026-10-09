#include <gtest/gtest.h>
extern "C" {
#include "button_debounce.h"
}

TEST(ButtonDebounce, QuickTapAndHeldPressTriggerOnlyOnce) {
    button_debounce_t button;
    button_debounce_init(&button, true, 0);
    EXPECT_FALSE(button_debounce_pressed(&button, false, 10));
    EXPECT_FALSE(button_debounce_pressed(&button, true, 15));
    EXPECT_FALSE(button_debounce_pressed(&button, false, 18));
    EXPECT_FALSE(button_debounce_pressed(&button, false, 37));
    EXPECT_TRUE(button_debounce_pressed(&button, false, 38));
    EXPECT_FALSE(button_debounce_pressed(&button, false, 1000));
    EXPECT_FALSE(button_debounce_pressed(&button, true, 1010));
    EXPECT_FALSE(button_debounce_pressed(&button, true, 1030));
    EXPECT_FALSE(button_debounce_pressed(&button, false, 1040));
    EXPECT_TRUE(button_debounce_pressed(&button, false, 1060));
}

TEST(ButtonDebounce, WakeButtonHeldAtStartupIsNotCountedAgain) {
    button_debounce_t button;
    button_debounce_init(&button, false, 0);
    EXPECT_FALSE(button_debounce_pressed(&button, false, 100));
    EXPECT_FALSE(button_debounce_pressed(&button, true, 110));
    EXPECT_FALSE(button_debounce_pressed(&button, true, 130));
    EXPECT_FALSE(button_debounce_pressed(&button, false, 140));
    EXPECT_TRUE(button_debounce_pressed(&button, false, 160));
}

TEST(ButtonDebounce, ClockWrapDoesNotLosePress) {
    button_debounce_t button;
    button_debounce_init(&button, true, UINT32_MAX - 30);
    EXPECT_FALSE(button_debounce_pressed(&button, false, UINT32_MAX - 10));
    EXPECT_TRUE(button_debounce_pressed(&button, false, 10));
}
