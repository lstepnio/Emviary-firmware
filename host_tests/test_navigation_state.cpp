#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
extern "C" {
#include "navigation_state.h"
#include "freertos/queue.h"
#include "utils.h"
}
namespace {
struct Queue { std::mutex mutex; std::condition_variable cv; bool occupied=false, previous=false; };
std::recursive_timed_mutex operation;
std::atomic<unsigned> attempts{0};
thread_local unsigned critical_depth=0, owned_operation=0;
std::vector<bool> executed;
bool inject_taps=false;
unsigned sleep_resets=0;
esp_err_t fetch_result=ESP_OK;
}
extern "C" {
void test_navigation_enter(portMUX_TYPE *mutex) { pthread_mutex_lock(mutex); ++critical_depth; }
void test_navigation_exit(portMUX_TYPE *mutex) { --critical_depth; pthread_mutex_unlock(mutex); }
QueueHandle_t xQueueCreate(unsigned length,unsigned item_size) {
    EXPECT_EQ(length,1u); EXPECT_EQ(item_size,sizeof(bool)); return new Queue;
}
BaseType_t xQueueOverwrite(QueueHandle_t handle,const void *item) {
    auto& q=*(Queue*)handle; std::lock_guard<std::mutex> lock(q.mutex);
    q.previous=*(const bool*)item; q.occupied=true; q.cv.notify_all(); return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t handle,void *item,TickType_t timeout) {
    EXPECT_EQ(timeout,0u); EXPECT_GT(owned_operation,0u);
    auto& q=*(Queue*)handle; std::lock_guard<std::mutex> lock(q.mutex);
    if(!q.occupied)return pdFALSE; *(bool*)item=q.previous; q.occupied=false;return pdTRUE;
}
BaseType_t xQueuePeek(QueueHandle_t handle,void *item,TickType_t timeout) {
    auto& q=*(Queue*)handle; std::unique_lock<std::mutex> lock(q.mutex);
    if(timeout==portMAX_DELAY)q.cv.wait(lock,[&]{return q.occupied;});
    if(!q.occupied)return pdFALSE; *(bool*)item=q.previous;return pdTRUE;
}
bool utils_image_operation_begin(TickType_t timeout) {
    EXPECT_EQ(critical_depth,0u); ++attempts;
    bool acquired=operation.try_lock_for(std::chrono::milliseconds(timeout));
    if(acquired)++owned_operation;return acquired;
}
void utils_image_operation_end(void) {
    EXPECT_GT(owned_operation,0u); --owned_operation; operation.unlock();
}
void power_manager_reset_sleep_timer(void) { ++sleep_resets; }
esp_err_t trigger_image_navigation(bool previous) {
    EXPECT_GT(owned_operation,0u); EXPECT_EQ(critical_depth,0u);
    EXPECT_TRUE(emviary_navigation_pending()); EXPECT_FALSE(emviary_navigation_suspend_for_sleep());
    // Exercise the real fetch's recursive reservation contract.
    EXPECT_TRUE(utils_image_operation_begin(0));
    executed.push_back(previous);
    if(inject_taps) {
        inject_taps=false;
        for(unsigned i=0;i<20;++i)emviary_navigation_queue((i&1)==0);
    }
    utils_image_operation_end();
    return fetch_result;
}
}
class NavigationStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(emviary_navigation_init(),ESP_OK);
        emviary_navigation_resume_after_sleep_deferred(); inject_taps=false;fetch_result=ESP_OK;
        while(emviary_navigation_process_next(0)){}
        executed.clear();sleep_resets=0;attempts=0;
    }
    void TearDown() override {
        EXPECT_EQ(owned_operation,0u);EXPECT_EQ(critical_depth,0u);
    }
    void WaitForAttempt() {
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!attempts && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        EXPECT_GT(attempts,0u);
    }
};
TEST_F(NavigationStateTest, ExternalRefreshKeepsOnePendingLatestDirection) {
    std::unique_lock<std::recursive_timed_mutex> http(operation);
    emviary_navigation_queue(false);
    std::thread worker([]{EXPECT_TRUE(emviary_navigation_process_next(1000));});
    WaitForAttempt();
    for(unsigned i=0;i<10;++i)emviary_navigation_queue((i&1)==0);
    emviary_navigation_queue(true);
    EXPECT_TRUE(emviary_navigation_pending());EXPECT_FALSE(emviary_navigation_suspend_for_sleep());
    http.unlock();worker.join();
    ASSERT_EQ(executed.size(),1u);EXPECT_TRUE(executed[0]);
    EXPECT_FALSE(emviary_navigation_process_next(0));EXPECT_FALSE(emviary_navigation_pending());
}
TEST_F(NavigationStateTest, OperationTimeoutPreservesLatestDirectionForScheduledDrain) {
    std::unique_lock<std::recursive_timed_mutex> ota(operation);
    emviary_navigation_queue(false);
    std::thread worker([]{EXPECT_FALSE(emviary_navigation_process_next(30));});
    WaitForAttempt();emviary_navigation_queue(true);worker.join();
    EXPECT_TRUE(executed.empty());EXPECT_EQ(sleep_resets,0u);EXPECT_TRUE(emviary_navigation_pending());
    ota.unlock();
    EXPECT_TRUE(emviary_navigation_process_next(0));ASSERT_EQ(executed.size(),1u);EXPECT_TRUE(executed[0]);
    EXPECT_FALSE(emviary_navigation_process_next(0));
}
TEST_F(NavigationStateTest, TapsDuringOwnRefreshCoalesceIntoOneFollowingRequest) {
    inject_taps=true;emviary_navigation_queue(true);
    EXPECT_TRUE(emviary_navigation_process_next(0));
    EXPECT_TRUE(emviary_navigation_pending());
    EXPECT_TRUE(emviary_navigation_process_next(0));
    ASSERT_EQ(executed.size(),2u);EXPECT_TRUE(executed[0]);EXPECT_FALSE(executed[1]);
    EXPECT_FALSE(emviary_navigation_process_next(0));EXPECT_EQ(sleep_resets,2u);
}
TEST_F(NavigationStateTest, SleepBarrierRejectsTapsUntilDeferredSleepResumes) {
    EXPECT_TRUE(emviary_navigation_suspend_for_sleep());emviary_navigation_queue(false);
    EXPECT_FALSE(emviary_navigation_pending());EXPECT_FALSE(emviary_navigation_process_next(0));
    emviary_navigation_resume_after_sleep_deferred();emviary_navigation_queue(false);
    EXPECT_FALSE(emviary_navigation_suspend_for_sleep());EXPECT_TRUE(emviary_navigation_process_next(0));
    EXPECT_TRUE(emviary_navigation_suspend_for_sleep());
}
TEST_F(NavigationStateTest, QueueWaitWakesWithoutConsumingDirection) {
    std::atomic<bool> awakened{false};
    std::thread waiter([&]{emviary_navigation_wait();awakened=true;});
    emviary_navigation_queue(true);waiter.join();EXPECT_TRUE(awakened);
    EXPECT_TRUE(emviary_navigation_pending());EXPECT_TRUE(emviary_navigation_process_next(0));
    ASSERT_EQ(executed.size(),1u);EXPECT_TRUE(executed[0]);
}

TEST_F(NavigationStateTest, FailedNavigationReleasesOperationAndPreservesFollowingTap) {
    fetch_result=ESP_FAIL;inject_taps=true;emviary_navigation_queue(true);
    EXPECT_TRUE(emviary_navigation_process_next(0));
    EXPECT_EQ(owned_operation,0u);EXPECT_TRUE(emviary_navigation_pending());
    fetch_result=ESP_OK;EXPECT_TRUE(emviary_navigation_process_next(0));
    ASSERT_EQ(executed.size(),2u);EXPECT_FALSE(executed[1]);
    EXPECT_FALSE(emviary_navigation_pending());EXPECT_TRUE(emviary_navigation_suspend_for_sleep());
}
