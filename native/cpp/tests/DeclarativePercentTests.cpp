#include "DeclarativeTestSupport.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace uirepo::declarative::testing {
void runPercentTests();
}

using namespace uirepo;
using namespace uirepo::declarative;
using namespace uirepo::declarative::testing;

namespace {

constexpr float kFloatMax = std::numeric_limits<float>::max();

std::string widthDocument(const std::string& number) {
    return R"({"format":"uirepo.ui","version":1,"id":"r","style":{"width":{"percent":)" + number + "}}}";
}

std::optional<float> parsedFraction(const std::string& number) {
    const ParseResult result = parseDocument(widthDocument(number));
    if (!result.ok()) {
        return std::nullopt;
    }
    require(result.document->root().style().width.kind == LengthKind::Percent, "percent length kind");
    return result.document->root().style().width.value;
}

std::string serializedPercent(float fraction, Document& document) {
    document.root().style().width = Length::Percent(fraction);
    return canonical(document);
}

std::uint64_t nextRandom(std::uint64_t& state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

ParseResult parsedLayoutDocument() {
    return parseOk(
        R"({"format":"uirepo.ui","version":1,"id":"root","style":{"padding":10},"children":[)"
        R"({"type":"panel","id":"half","style":{"width":{"percent":50},"height":20,"background":"#ff0000"}},)"
        R"({"type":"panel","id":"full","style":{"width":{"percent":100},"height":{"percent":25},"background":"#00ff00"}},)"
        R"({"type":"panel","id":"wide","style":{"width":{"percent":150},"height":20,"background":"#0000ff"}}]})",
        "percent layout");
}

std::unique_ptr<Document> builtLayoutDocument() {
    auto root = std::make_unique<PanelNode>("root");
    root->style().padding = Thickness(10.0f);
    auto& half = root->emplaceChild<PanelNode>("half");
    half.style().width = Length::Percent(0.5f);
    half.style().height = Length::Pixels(20.0f);
    half.style().background = Color{255, 0, 0, 255};
    auto& full = root->emplaceChild<PanelNode>("full");
    full.style().width = Length::Percent(1.0f);
    full.style().height = Length::Percent(0.25f);
    full.style().background = Color{0, 255, 0, 255};
    auto& wide = root->emplaceChild<PanelNode>("wide");
    wide.style().width = Length::Percent(1.5f);
    wide.style().height = Length::Pixels(20.0f);
    wide.style().background = Color{0, 0, 255, 255};
    return std::make_unique<Document>(std::move(root));
}

void testPercentLaysOutAsFraction() {
    ParseResult parsed = parsedLayoutDocument();
    const auto built = builtLayoutDocument();
    require(sameTree(parsed.document->root(), built->root()), "parsed percent tree equals the programmatic fraction tree");

    for (const Size viewport : {Size{400.0f, 200.0f}, Size{1280.0f, 720.0f}, Size{321.0f, 77.0f}}) {
        parsed.document->layout(viewport);
        built->layout(viewport);
        const float content = parsed.document->root().bounds().width - 20.0f;
        const float contentHeight = parsed.document->root().bounds().height - 20.0f;
        const Rect half = parsed.document->find("half")->bounds();
        const Rect full = parsed.document->find("full")->bounds();
        const Rect wide = parsed.document->find("wide")->bounds();
        require(content > 0.0f && half.width == content * 0.5f, "percent 50 lays out to half of the available width");
        require(full.width == content && full.height == contentHeight * 0.25f,
                "percent 100 lays out to all of the available width and percent 25 to a quarter of the height");
        require(wide.width == content * 1.5f, "percent above 100 lays out beyond the available width");
        for (const char* id : {"root", "half", "full", "wide"}) {
            require(sameRect(parsed.document->find(id)->bounds(), built->find(id)->bounds()),
                    std::string("layout rectangle differs for ") + id);
        }
        const auto left = parsed.document->buildDisplayList();
        const auto right = built->buildDisplayList();
        require(left.size() >= 3 && left.size() == right.size(), "percent display list sizes differ");
        for (std::size_t index = 0; index < left.size(); ++index) {
            require(sameCommand(left[index], right[index]), "percent display list command " + std::to_string(index) + " differs");
        }
    }
}

void testPercentDecodesToFraction() {
    require(parsedFraction("50") == 0.5f && parsedFraction("100") == 1.0f && parsedFraction("0") == 0.0f &&
                parsedFraction("12.5") == 0.125f && parsedFraction("150") == 1.5f && parsedFraction("25") == 0.25f,
            "exactly representable percents decode to the exact fraction");
    require(parsedFraction("30") == 0.3f && parsedFraction("33.333332") == 0.33333332f && parsedFraction("1e2") == 1.0f &&
                parsedFraction("1.5E+2") == 1.5f && parsedFraction("5e-1") == 0.005f,
            "percents decode to the float nearest to the decimal fraction, never through a float division");
    const auto negativeZero = parsedFraction("-0");
    require(negativeZero && *negativeZero == 0.0f && !std::signbit(*negativeZero), "negative zero percent reads as positive zero");
    const auto underflow = parsedFraction("1e-400");
    require(underflow && *underflow == 0.0f, "an underflowing percent reads as zero like every other number");
    require(parsedFraction("3.4e40") && !parsedFraction("4e40") && !parsedFraction("1e999"),
            "percents whose fraction overflows float are refused");
}

void testPercentCanonicalGoldens() {
    struct Golden {
        const char* input;
        const char* expected;
    };
    const Golden goldens[] = {
        {"0", "0"},
        {"-0", "0"},
        {"12.5", "12.5"},
        {"25", "25"},
        {"50", "50"},
        {"100", "100"},
        {"150", "150"},
        {"30", "30"},
        {"0.1", "0.1"},
        {"33.333332", "33.33333"},
        {"33.333334", "33.333334"},
        {"33.333333333333336", "33.333334"},
        {"1e2", "100"},
        {"1.5E+2", "150"},
        {"100.0000", "100"},
        {"0.00001", "0.00001"},
        {"1e-7", "1e-7"},
        {"1e20", "100000000000000000000"},
        {"1e21", "1e21"},
    };
    for (const Golden& entry : goldens) {
        const ParseResult result = parseOk(widthDocument(entry.input), entry.input);
        require(canonical(*result.document) == widthDocument(entry.expected),
                std::string("percent ") + entry.input + " canonical form is " + canonical(*result.document));
    }

    Document document(std::make_unique<PanelNode>("r"));
    require(serializedPercent(0.3f, document) == widthDocument("30"), "the float fraction 0.3 prints as 30, not 30.000002");
    require(serializedPercent(0.33333334f, document) == widthDocument("33.333334"), "one third prints shortest");
    require(serializedPercent(-0.0f, document) == widthDocument("0"), "negative zero fraction prints as 0");
}

std::size_t significantDigits(const std::string& document) {
    const std::size_t begin = document.find(R"("percent":)") + 10;
    std::string digits;
    for (std::size_t at = begin; at < document.size() && document[at] != 'e' && document[at] != '}'; ++at) {
        if (document[at] != '.') {
            digits += document[at];
        }
    }
    const std::size_t first = digits.find_first_not_of('0');
    const std::size_t last = digits.find_last_not_of('0');
    return first == std::string::npos ? 0 : last - first + 1;
}

void testPercentModelRoundTrip() {
    Document document(std::make_unique<PanelNode>("r"));
    const auto check = [&document](float fraction) {
        const std::string text = serializedPercent(fraction, document);
        const ParseResult again = parseDocument(text);
        require(again.ok(), "serialized percent must parse: " + text);
        require(std::bit_cast<std::uint32_t>(again.document->root().style().width.value) == std::bit_cast<std::uint32_t>(fraction),
                "fraction does not survive serialize then parse: " + text);
        require(canonical(*again.document) == text, "percent canonical form is not a fixed point: " + text);
        require(significantDigits(text) <= 9, "a float fraction never needs more than nine significant digits: " + text);
    };

    std::uint64_t state = 0xD1B54A32D192ED03ull;
    const std::uint32_t eight = std::bit_cast<std::uint32_t>(8.0f);
    for (int index = 0; index < 150000; ++index) {
        check(std::bit_cast<float>(static_cast<std::uint32_t>(nextRandom(state) % (eight + 1))));
        check(static_cast<float>(static_cast<double>(nextRandom(state) >> 11) / 9007199254740992.0 * 8.0));
    }
    for (std::uint32_t bits = 0; bits < 4096; ++bits) {
        check(std::bit_cast<float>(bits));
        check(std::bit_cast<float>(std::bit_cast<std::uint32_t>(kFloatMax) - bits));
    }
    for (int exponent = -149; exponent <= 127; ++exponent) {
        const float power = std::ldexp(1.0f, exponent);
        check(power);
        check(std::nextafter(power, 0.0f));
        check(std::nextafter(power, kFloatMax));
    }
    for (std::uint32_t denominator = 1; denominator <= 400; ++denominator) {
        check(1.0f / static_cast<float>(denominator));
        check(static_cast<float>(denominator) / 100.0f);
    }
    check(0.3f);
    check(kFloatMax);
}

std::string randomDecimal(std::uint64_t& state) {
    std::string text;
    const std::size_t integerDigits = 1 + nextRandom(state) % 12;
    for (std::size_t index = 0; index < integerDigits; ++index) {
        text += static_cast<char>('0' + nextRandom(state) % 10);
    }
    if (integerDigits > 1 && text[0] == '0') {
        text[0] = '1';
    }
    if (nextRandom(state) % 2 == 0) {
        text += '.';
        const std::size_t fractionDigits = 1 + nextRandom(state) % 12;
        for (std::size_t index = 0; index < fractionDigits; ++index) {
            text += static_cast<char>('0' + nextRandom(state) % 10);
        }
    }
    if (nextRandom(state) % 3 == 0) {
        text += nextRandom(state) % 2 == 0 ? "e" : "E";
        const std::size_t sign = nextRandom(state) % 3;
        text += sign == 0 ? "" : sign == 1 ? "+" : "-";
        text += std::to_string(nextRandom(state) % 45);
    }
    return text;
}

void testPercentTextRoundTrip() {
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    std::size_t accepted = 0;
    std::size_t refused = 0;
    for (int index = 0; index < 120000; ++index) {
        const std::string number = randomDecimal(state);
        const ParseResult first = parseDocument(widthDocument(number));
        if (!first.ok()) {
            ++refused;
            require(first.diagnostics.front().code == ErrorCode::NumberRange,
                    "only an overflowing percent may be refused: " + number);
            continue;
        }
        ++accepted;
        const std::string once = canonical(*first.document);
        const ParseResult second = parseOk(once, "percent canonical output");
        require(sameTree(first.document->root(), second.document->root()),
                "parse(serialize(parse(x))) differs from parse(x) for percent " + number + " via " + once);
        require(canonical(*second.document) == once, "percent canonical form is not a fixed point for " + number);
    }
    require(accepted > 60000 && refused > 0, "percent text fuzz must exercise accepted and refused numbers");
}

void testPercentRefusals() {
    for (const char* bad : {"-5", "-0.001", "-1e-3"}) {
        const ParseResult result = parseDocument(widthDocument(bad));
        require(!result.ok() && result.diagnostics.front().code == ErrorCode::InvalidValue &&
                    result.diagnostics.front().path == "/style/width/percent",
                std::string("negative percent stays an invalid value: ") + bad);
    }
    const ParseResult overflow = parseDocument(widthDocument("1e999"));
    require(!overflow.ok() && overflow.diagnostics.front().code == ErrorCode::NumberRange &&
                overflow.diagnostics.front().path == "/style/width/percent",
            "percent beyond float range is a number range error");
    const ParseResult fractionOverflow = parseDocument(widthDocument("3.5e40"));
    require(!fractionOverflow.ok() && fractionOverflow.diagnostics.front().code == ErrorCode::NumberRange,
            "a percent whose fraction exceeds the largest float is a number range error");

    for (const float bad : {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        auto root = std::make_unique<PanelNode>("r");
        root->style().height = Length::Percent(bad);
        Document document(std::move(root));
        const SerializeResult result = serializeDocument(document);
        require(!result.ok() && result.text.empty() && result.diagnostics.front().code == ErrorCode::InvalidValue &&
                    result.diagnostics.front().path == "/style",
                "serializer still refuses an invalid percent fraction");
    }
}

} // namespace

namespace uirepo::declarative::testing {

void runPercentTests() {
    testPercentLaysOutAsFraction();
    testPercentDecodesToFraction();
    testPercentCanonicalGoldens();
    testPercentModelRoundTrip();
    testPercentTextRoundTrip();
    testPercentRefusals();
}

} // namespace uirepo::declarative::testing
