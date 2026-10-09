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
