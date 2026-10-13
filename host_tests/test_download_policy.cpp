#include <gtest/gtest.h>
#include "download_policy.h"
#include "https_origin.h"
TEST(DownloadPolicy, BoundsChunkAndOverflow) {
    download_policy_t p={100,1000};
    EXPECT_TRUE(download_policy_accept_bytes(&p, 90,10));
    EXPECT_FALSE(download_policy_accept_bytes(&p,90,11));
    EXPECT_FALSE(download_policy_accept_bytes(&p,SIZE_MAX,1));
    EXPECT_FALSE(download_policy_accept_bytes(&p,1,SIZE_MAX));
}
TEST(DownloadPolicy, DeadlineIncludesAllBodyTime) {
    download_policy_t p={100,1000};
    EXPECT_TRUE(download_policy_within_deadline(&p,999));
    EXPECT_FALSE(download_policy_within_deadline(&p,1000));
}
TEST(DownloadPolicy, RequiresTransportAndDeclaredCompleteness) {
    download_policy_t p={100,1000};
    EXPECT_TRUE(download_policy_complete(&p,100,100,true));
    EXPECT_TRUE(download_policy_complete(&p,100,-1,true));
    EXPECT_FALSE(download_policy_complete(&p,99,100,true));
    EXPECT_FALSE(download_policy_complete(&p,100,100,false));
    EXPECT_FALSE(download_policy_complete(&p,101,-1,true));
}

TEST(DownloadPolicy, StorageWriteFailureDoesNotCountUnwrittenBytes) {
    download_policy_t p={100,1000};
    FILE *file = fopen(__FILE__, "rb"); ASSERT_NE(file,nullptr);
    size_t received=0;
    EXPECT_FALSE(download_policy_write(&p,file,&received,"test",4,999));
    EXPECT_EQ(received,0u); fclose(file);
}
TEST(DownloadPolicy, DeadlineAndSizeFailureWriteNothing) {
    download_policy_t p={3,1000}; FILE *file=tmpfile(); ASSERT_NE(file,nullptr);
    size_t received=0;
    EXPECT_FALSE(download_policy_write(&p,file,&received,"test",4,999));
    EXPECT_FALSE(download_policy_write(&p,file,&received,"ok",2,1000));
    EXPECT_EQ(ftell(file),0); EXPECT_EQ(received,0u); fclose(file);
}
TEST(DownloadPolicy, SuccessfulWriteCountsPersistedBytes) {
    download_policy_t p={4,1000}; FILE *file=tmpfile(); ASSERT_NE(file,nullptr);
    size_t received=0;
    EXPECT_TRUE(download_policy_write(&p,file,&received,"test",4,999));
    EXPECT_EQ(received,4u); EXPECT_EQ(fclose(file),0);
}

TEST(HttpsOrigin, SameOriginAndDefaultPort) {
    EXPECT_TRUE(https_origin_same("https://EXAMPLE.com/a", "https://example.com:443/b"));
    EXPECT_TRUE(https_origin_same("https://example.com:8443/a", "https://example.com:8443/b"));
    EXPECT_FALSE(https_origin_same("https://example.com/a", "https://example.com:8443/b"));
}
TEST(HttpsOrigin, RejectsCredentialAndSchemeTricks) {
    EXPECT_FALSE(https_origin_same("https://example.com/a", "http://example.com/b"));
    EXPECT_FALSE(https_origin_same("https://example.com/a", "https://example.com@evil.com/b"));
    EXPECT_FALSE(https_origin_same("https://example.com/a", "https://example.com\\@evil.com/b"));
    EXPECT_FALSE(https_origin_same("https://example.com/a", "https://example.com.evil.com/b"));
    EXPECT_FALSE(https_origin_same("https://example.com/a", "https://example.com:65536/b"));
    EXPECT_FALSE(https_origin_same("https://example.com/a", "https://example.com:/b"));
}

TEST(DownloadProgress, IdleFirstResponseExpiresAfterFifteenSeconds) {
    download_progress_t p; download_progress_begin(&p,1000);
    EXPECT_FALSE(download_progress_idle_expired(&p,15000999));
    EXPECT_TRUE(download_progress_idle_expired(&p,15001000));
}
TEST(DownloadProgress, OnlyMeaningfulProgressExtendsIdleWindow) {
    download_progress_t p; download_progress_begin(&p,0);
    download_progress_observed(&p,14000000,false); // EAGAIN, connection or empty data
    EXPECT_TRUE(download_progress_idle_expired(&p,15000000));
    download_progress_observed(&p,14000000,true); // complete header or persisted body bytes
    EXPECT_FALSE(download_progress_idle_expired(&p,15000000));
    EXPECT_TRUE(download_progress_idle_expired(&p,29000000));
}
TEST(DownloadProgress, ProgressCannotExtendAbsoluteDeadline) {
    download_policy_t policy={1000,60000000};
    download_progress_t p; download_progress_begin(&p,0);
    for(int64_t now=10000000;now<60000000;now+=10000000)
        download_progress_observed(&p,now,true);
    EXPECT_FALSE(download_progress_idle_expired(&p,60000000));
    EXPECT_FALSE(download_policy_within_deadline(&policy,60000000));
}
TEST(DownloadProgress, NewRequestStartsFreshButPollingDoesNot) {
    download_progress_t p;download_progress_begin(&p,0);
    for(int64_t now=0;now<15000000;now+=20000)
        download_progress_observed(&p,now,false);
    EXPECT_TRUE(download_progress_idle_expired(&p,15000000));
    download_progress_begin(&p,20000000);
    EXPECT_FALSE(download_progress_idle_expired(&p,20000000));
    download_progress_observed(&p,19000000,true);
    EXPECT_EQ(p.last_progress_us,20000000);
}
TEST(DownloadProgress, IdleTimeoutNeverStartsAnotherRetryWindow) {
    download_policy_t policy={1000,60000000};
    EXPECT_TRUE(download_policy_retry_allowed(&policy,1000000,false)); // quick failure
    EXPECT_FALSE(download_policy_retry_allowed(&policy,15000000,true)); // idle timeout
    EXPECT_FALSE(download_policy_retry_allowed(&policy,60000000,false)); // absolute cap
}
