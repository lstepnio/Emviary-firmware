#include <gtest/gtest.h>
#include <zlib.h>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
extern "C" {
#include "GUI_EPDGZfile.h"
PAINT Paint;
static size_t pixels;
void Paint_SetPixel(UWORD, UWORD, UWORD) { ++pixels; }
}

class EpdgzTest : public ::testing::Test {
protected:
    std::string path;
    void SetUp() override {
        char name[] = "/tmp/emviary-epdgz-XXXXXX";
        int fd = mkstemp(name); ASSERT_GE(fd, 0); close(fd); path = name;
        Paint = {}; Paint.Width = 800; Paint.Height = 480; Paint.Scale = 6; pixels = 0;
    }
    void TearDown() override { unlink(path.c_str()); }
    std::vector<uint8_t> Gzip(size_t size, uint8_t byte = 0x16) {
        std::vector<uint8_t> src(size, byte), out(compressBound(size) + 32);
        z_stream z = {}; EXPECT_EQ(deflateInit2(&z, 6, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY), Z_OK);
        z.next_in = src.data(); z.avail_in = src.size(); z.next_out = out.data(); z.avail_out = out.size();
        EXPECT_EQ(deflate(&z, Z_FINISH), Z_STREAM_END); out.resize(z.total_out); deflateEnd(&z); return out;
    }
    int Read(const std::vector<uint8_t>& bytes) {
        std::ofstream f(path, std::ios::binary); f.write((const char*)bytes.data(), bytes.size()); f.close();
        return GUI_ReadEPDGZ(path.c_str());
    }
    void Reject(const std::vector<uint8_t>& bytes) { EXPECT_NE(Read(bytes), 0); EXPECT_EQ(pixels, 0u); }
};
TEST_F(EpdgzTest, ExactFrame) { EXPECT_EQ(Read(Gzip(192000)), 0); EXPECT_EQ(pixels, 384000u); }
TEST_F(EpdgzTest, ShortValidGzip) { Reject(Gzip(1)); }
TEST_F(EpdgzTest, EmptyFile) { Reject({}); }
TEST_F(EpdgzTest, TruncatedTrailer) { auto b=Gzip(192000); b.resize(b.size()-4); Reject(b); }
TEST_F(EpdgzTest, CorruptCrc) { auto b=Gzip(192000); b[b.size()-8]^=1; Reject(b); }
TEST_F(EpdgzTest, ExcessOutput) { Reject(Gzip(192001)); }
TEST_F(EpdgzTest, TrailingInput) { auto b=Gzip(192000); b.push_back(0); Reject(b); }
TEST_F(EpdgzTest, SecondGzipMember) { auto b=Gzip(192000), extra=Gzip(1); b.insert(b.end(), extra.begin(),extra.end()); Reject(b); }
TEST_F(EpdgzTest, InvalidSpectraNibble) { Reject(Gzip(192000, 0x14)); }
TEST_F(EpdgzTest, OversizeCompressedFile) { Reject(std::vector<uint8_t>(256*1024,0)); }
TEST_F(EpdgzTest, OddWidthUsesRowStride) { Paint.Width=3; Paint.Height=2; EXPECT_EQ(Read(Gzip(4)),0); EXPECT_EQ(pixels,6u); }
TEST_F(EpdgzTest, Gray16AcceptsAllNibbles) { Paint.Scale=16; EXPECT_EQ(Read(Gzip(192000,0x4F)),0); }
TEST_F(EpdgzTest, UnseekableInput) { EXPECT_NE(GUI_ReadEPDGZ("/tmp"),0); EXPECT_EQ(pixels,0u); }
