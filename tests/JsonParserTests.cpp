// Regression test for D1 (DEC-021): parsed JSON numbers must be typed Number.
//
// Before the fix Parser::parseNumber built JsonValue(std::string), which sets
// Type::String, so asDouble/asInt64 returned their fallback for every parsed
// number. That silently zeroed the real MT5 bridge feed (NON_POSITIVE_PRICE).

#include "TestHarness.h"

#include "foundation/Json.h"

#include <string>

using namespace aura;

namespace {

JsonValue parseOrFail(const std::string& text) {
    JsonValue root;
    std::string error;
    CHECK(JsonValue::parse(text, root, error));
    return root;
}

}  // namespace

TEST_CASE(parsed_numbers_are_typed_number) {
    const JsonValue root = parseOrFail(
        R"({"i":12,"f":-4076.56,"e":1.5e3,"s":"4076.56","b":true,"n":null})");

    CHECK(root["i"].isNumber());
    CHECK(root["f"].isNumber());
    CHECK(root["e"].isNumber());
    CHECK(!root["s"].isNumber());
    CHECK(root["s"].isString());
    CHECK(root["b"].isBool());
    CHECK(root["n"].isNull());
}

TEST_CASE(parsed_numbers_read_back_exact_values) {
    const JsonValue root = parseOrFail(
        R"({"i":12,"f":-4076.56,"e":1.5e3,"big":9007199254740993})");

    CHECK_EQ(root["i"].asInt64(-1), 12);
    CHECK(root["f"].asDouble(-1.0) > -4076.57);
    CHECK(root["f"].asDouble(-1.0) < -4076.55);
    CHECK_EQ(root["e"].asDouble(-1.0), 1500.0);
    // Numbers stay text-backed to preserve integer precision (no double store).
    CHECK_EQ(root["big"].asString(), std::string("9007199254740993"));
    CHECK_EQ(root["big"].asInt64(-1), static_cast<std::int64_t>(9007199254740993LL));
}

TEST_CASE(parsed_number_array_elements_are_numbers) {
    const JsonValue root = parseOrFail(R"({"candles":[{"open":4076.56,"time":1782299280}]})");

    const JsonValue& candle = root["candles"].at(0);
    CHECK(candle["open"].isNumber());
    CHECK(candle["open"].asDouble(-1.0) > 0.0);
    CHECK_EQ(candle["time"].asInt64(-1), static_cast<std::int64_t>(1782299280));
}

TEST_CASE(parsed_number_round_trips_through_dump) {
    JsonValue root = parseOrFail(R"({"open":4076.56,"time":1782299280})");
    JsonValue again;
    std::string error;
    CHECK(JsonValue::parse(root.dump(), again, error));
    CHECK(again["open"].isNumber());
    CHECK(again["time"].isNumber());
    CHECK_EQ(again["time"].asInt64(-1), static_cast<std::int64_t>(1782299280));
}
