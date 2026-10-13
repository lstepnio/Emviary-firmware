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

TEST(ButtonHold, ShortLeftResolvesOnlyOnDebouncedRelease) {
    button_hold_t b; button_hold_init(&b,true,0);
    EXPECT_EQ(button_hold_update(&b,false,10),BUTTON_HOLD_NONE);
    EXPECT_EQ(button_hold_update(&b,false,30),BUTTON_HOLD_NONE);
    EXPECT_EQ(button_hold_update(&b,true,100),BUTTON_HOLD_NONE);
    EXPECT_EQ(button_hold_update(&b,true,120),BUTTON_HOLD_SHORT);
    EXPECT_EQ(button_hold_update(&b,true,200),BUTTON_HOLD_NONE);
}
TEST(ButtonHold, FiveSecondHoldConsumesReleaseAndFiresOnce) {
    button_hold_t b; button_hold_init(&b,true,0);
    button_hold_update(&b,false,10); button_hold_update(&b,false,30);
    EXPECT_EQ(button_hold_update(&b,false,5029),BUTTON_HOLD_NONE);
    EXPECT_EQ(button_hold_update(&b,false,5030),BUTTON_HOLD_LONG);
    EXPECT_EQ(button_hold_update(&b,false,12000),BUTTON_HOLD_NONE);
    button_hold_update(&b,true,12010);
    EXPECT_EQ(button_hold_update(&b,true,12030),BUTTON_HOLD_NONE);
    button_hold_update(&b,false,13000); button_hold_update(&b,false,13020);
    button_hold_update(&b,true,13040);
    EXPECT_EQ(button_hold_update(&b,true,13060),BUTTON_HOLD_SHORT);
}
TEST(ButtonHold, AlreadyHeldWakeCanBeExplicitlyArmedFromBootTimestamp) {
    button_hold_t b; button_hold_init(&b,false,4500);
    b.armed=true; b.pressed_ms=0;
    EXPECT_EQ(button_hold_update(&b,false,4999),BUTTON_HOLD_NONE);
    EXPECT_EQ(button_hold_update(&b,false,5000),BUTTON_HOLD_LONG);
    button_hold_update(&b,true,5100);
    EXPECT_EQ(button_hold_update(&b,true,5120),BUTTON_HOLD_NONE);
}
TEST(ButtonHold, HeldAtWorkerStartupIsSuppressedAndClockWrapWorks) {
    button_hold_t b; button_hold_init(&b,false,0);
    EXPECT_EQ(button_hold_update(&b,false,6000),BUTTON_HOLD_NONE);
    button_hold_update(&b,true,6010);
    EXPECT_EQ(button_hold_update(&b,true,6030),BUTTON_HOLD_NONE);
    button_hold_init(&b,true,UINT32_MAX-100);
    button_hold_update(&b,false,UINT32_MAX-80);
    button_hold_update(&b,false,UINT32_MAX-60);
    EXPECT_EQ(button_hold_update(&b,false,4939),BUTTON_HOLD_LONG);
}
TEST(ButtonHold, ReleaseJustBeforeThresholdRemainsShortDespiteDebounce) {
    button_hold_t b; button_hold_init(&b,true,0);
    button_hold_update(&b,false,0); button_hold_update(&b,false,20);
    EXPECT_EQ(button_hold_update(&b,true,5019),BUTTON_HOLD_NONE);
    EXPECT_EQ(button_hold_update(&b,true,5039),BUTTON_HOLD_SHORT);
}
