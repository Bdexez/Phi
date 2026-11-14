#include <phi/core/Json.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string>

using namespace phi;

TEST(Json, ParsesEveryKindOfValue) {
    const auto parsed = json::parse(R"({
        "asset": {"version": "2.0", "generator": "test"},
        "count": 3, "scale": -1.5e2, "small": 2.5E-3, "zero": -0,
        "flags": [true, false, null],
        "empty": {}, "none": [],
        "nested": [[1, [2]], {"a": {"b": "c"}}]
    })");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    const json::Value& root = *parsed;
    EXPECT_TRUE(root.isObject());
    EXPECT_EQ(root["asset"]["version"].asString(), "2.0");
    EXPECT_DOUBLE_EQ(root["count"].asNumber(), 3.0);
    EXPECT_DOUBLE_EQ(root["scale"].asNumber(), -150.0);
    EXPECT_DOUBLE_EQ(root["small"].asNumber(), 0.0025);
    EXPECT_EQ(root["flags"].size(), 3u);
    EXPECT_TRUE(root["flags"][0].asBool());
    EXPECT_FALSE(root["flags"][1].asBool(true));
    EXPECT_TRUE(root["flags"][2].isNull());
    EXPECT_TRUE(root["empty"].isObject());
    EXPECT_EQ(root["none"].size(), 0u);
    EXPECT_EQ(root["nested"][0][1][0].asNumber(), 2.0);
    EXPECT_EQ(root["nested"][1]["a"]["b"].asString(), "c");
    // Members keep their order.
    EXPECT_EQ(root.asObject().front().first, "asset");

    // Missing members and wrong types fall back instead of failing.
    EXPECT_TRUE(root["missing"]["deeper"][7].isNull());
    EXPECT_EQ(root["count"].asString("n/a"), "n/a");
    EXPECT_DOUBLE_EQ(root["asset"].asNumber(-1.0), -1.0);
    EXPECT_EQ(root.find("missing"), nullptr);
}

TEST(Json, DecodesStringEscapesToUtf8) {
    const auto parsed = json::parse(R"(["a\"b\\c\/d\n\t", "\u00e9\u20ac", "\ud83d\ude00", "plain é"])");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ((*parsed)[0].asString(), "a\"b\\c/d\n\t");
    EXPECT_EQ((*parsed)[1].asString(), "\xC3\xA9\xE2\x82\xAC");      // é€
    EXPECT_EQ((*parsed)[2].asString(), "\xF0\x9F\x98\x80");          // a surrogate pair
    EXPECT_EQ((*parsed)[3].asString(), "plain \xC3\xA9");            // raw UTF-8 kept
}

TEST(Json, NumbersBeyondADoubleReadAsJavaScriptDoes) {
    const auto parsed = json::parse("[1e400, -1e400, 1e-400, 1" + std::string(400, '0') + "]");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_TRUE(std::isinf((*parsed)[0].asNumber()));
    EXPECT_LT((*parsed)[1].asNumber(), 0.0);
    EXPECT_EQ((*parsed)[2].asNumber(), 0.0);
    EXPECT_TRUE(std::isinf((*parsed)[3].asNumber()));
}

TEST(Json, RejectsMalformedTextWithItsPosition) {
    for (const char* bad : {"", "{", "[1,]", "{\"a\" 1}", "{\"a\":1,}", "[01]", "[.5]", "[1.]", "[+1]", "[1e]",
                            "tru", "\"open", "\"tab\there\"", "\"\\x\"", "\"\\ud800\"", "[1] 2", "{a:1}", "[nul]"}) {
        EXPECT_FALSE(json::parse(bad).has_value()) << bad;
    }
    const auto located = json::parse("{\n  \"a\": [1,\n   2,,]\n}");
    ASSERT_FALSE(located.has_value());
    EXPECT_NE(located.error().message.find("line 3"), std::string::npos) << located.error().message;

    // Deep nesting is refused rather than overflowing the stack.
    const std::string deep = std::string(100000, '[') + std::string(100000, ']');
    const auto refused = json::parse(deep);
    ASSERT_FALSE(refused.has_value());
    EXPECT_NE(refused.error().message.find("deeply"), std::string::npos);
    EXPECT_TRUE(json::parse(std::string(200, '[') + std::string(200, ']')).has_value());

    // A byte order mark is fine.
    EXPECT_TRUE(json::parse("\xEF\xBB\xBF{}").has_value());
}

TEST(Json, WritesWhatItReadsBack) {
    const std::string text = R"({"name":"cube \"one\"\n","values":[1,-2.5,0.1,1e+21],"ok":true,"none":null,"o":{}})";
    const auto parsed = json::parse(text);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    const std::string written = json::write(*parsed);
    const auto again = json::parse(written);
    ASSERT_TRUE(again.has_value()) << written;
    EXPECT_EQ(json::write(*again), written);
    EXPECT_EQ((*again)["name"].asString(), "cube \"one\"\n");
    EXPECT_DOUBLE_EQ((*again)["values"][2].asNumber(), 0.1);  // shortest round-trip form
    json::Array built = {1, "two", true, nullptr, json::Object{{"k", 3.5}}};
    EXPECT_EQ(json::write(json::Value(built)), R"([1,"two",true,null,{"k":3.5}])");
}
