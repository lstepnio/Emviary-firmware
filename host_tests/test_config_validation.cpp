#include <gtest/gtest.h>
#include <string>
extern "C" {
#include "config_validation.h"
}
TEST(ConfigValidation, ExactLengthsAndHeaders) {
    EXPECT_TRUE(config_input_valid("", 4, false));
    EXPECT_TRUE(config_input_valid("abc", 4, false));
    EXPECT_FALSE(config_input_valid("abcd", 4, false));
    EXPECT_FALSE(config_input_valid("a\r\nb", 20, false));
    EXPECT_TRUE(config_input_valid("X-Frame_Auth", 64, true));
    EXPECT_FALSE(config_input_valid("Bad:Header", 64, true));
    EXPECT_FALSE(config_input_valid("Bad Header", 64, true));
}
TEST(ConfigValidation, FormEscapesAndBounds) {
    char out[5];
    EXPECT_TRUE(config_form_field("other=x&name=a%2Bb+&z=y", "name", out, sizeof(out)));
    EXPECT_STREQ(out, "a+b ");
    EXPECT_FALSE(config_form_field("name=abcde", "name", out, sizeof(out)));
    EXPECT_FALSE(config_form_field("name=%00", "name", out, sizeof(out)));
    EXPECT_FALSE(config_form_field("name=%ZZ", "name", out, sizeof(out)));
    EXPECT_FALSE(config_form_field("name=%2", "name", out, sizeof(out)));
    EXPECT_TRUE(config_form_field("name=", "name", out, sizeof(out)));
    EXPECT_FALSE(config_form_field("xname=a", "name", out, sizeof(out)));
}

TEST(ConfigValidation, JsonStructuralBoundsBeforeParsing) {
    std::string object = "{\"nested\":[{\"text\":\"braces{}[] and \\\" quote\"}]}\n";
    EXPECT_TRUE(config_json_shape_valid(object.data(), object.size()));
    for (size_t depth : {16U, 17U}) {
        std::string nested(depth, '['); nested += "0"; nested += std::string(depth, ']');
        EXPECT_EQ(config_json_shape_valid(nested.data(), nested.size()), depth == 16);
    }
    for (const char *bad : {"{}{}", "{}garbage", "{]", "{", "{\"x\":\"\\u0000\"}",
                           "{\"x\":\"raw\ncontrol\"}", "{}\x01"}) {
        std::string value = bad;
        EXPECT_FALSE(config_json_shape_valid(value.data(), value.size())) << value;
    }
    std::string embedded = "{}"; embedded += '\0'; embedded += "hidden";
    EXPECT_FALSE(config_json_shape_valid(embedded.data(), embedded.size()));
}
