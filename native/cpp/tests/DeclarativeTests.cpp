#include "DeclarativeTestSupport.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace uirepo::declarative::testing {
void runErrorTable();
void runPercentTests();
}

using namespace uirepo;
using namespace uirepo::declarative;
using namespace uirepo::declarative::testing;

namespace {

constexpr char kMinimal[] = R"({"format":"uirepo.ui","version":1,"id":"root"})";

constexpr char kFull[] = R"JSON({
  "format": "uirepo.ui",
  "version": 1,
  "id": "main",
  "x-editor": {"zoom": 2, "notes": [1, 2.5e3, null, true, "free form"]},
  "style": {
    "width": {"percent": 100},
    "height": 600,
    "minWidth": 100,
    "minHeight": 50,
    "maxWidth": 1920,
    "maxHeight": 1080,
    "padding": 8,
    "margin": [1, 2, 3, 4],
    "gap": 6,
    "flexGrow": 1,
    "alignItems": "center",
    "background": "#102030",
    "opacity": 0.5,
    "zIndex": -3,
    "cornerRadius": 4,
    "clipToBounds": true
  },
  "children": [
    {
      "type": "stack", "id": "column", "axis": "column", "scrollable": true, "style": {"gap": 4},
      "children": [
        {"type": "text", "id": "title", "text": "Héllo \"quoted\"\n😀",
         "fontSize": 24.5, "color": "#FFCC00",
         "textLayout": {"wrapping": "wrap", "alignment": "center", "overflow": "ellipsis"}},
        {"type": "button", "id": "ok", "text": "OK", "fontSize": 16, "textColor": "#ffffff80",
         "focusedBackground": "#112233", "textLayout": {"alignment": "end"},
         "style": {"padding": [4, 2, 4, 2], "x-hint": "ignored"}},
        {"type": "image", "id": "logo", "source": "textures/logo.png", "naturalSize": [64, 32],
         "style": {"width": {"percent": 50}}},
        {"type": "video", "id": "intro", "source": "video/intro.bk2", "naturalSize": [1280, 720],
         "playback": "playing", "loop": true, "volume": 0.25},
        {"type": "toggle", "id": "mute", "value": true, "enabled": false, "style": {"width": "auto"}},
        {"type": "slider", "id": "volume", "min": 0, "max": 10, "step": 0.5, "value": 7.5},
        {"type": "stepper", "id": "quality", "options": ["Low", "Medium", "High"], "index": 2},
        {"type": "textInput", "id": "name", "value": "Ricky", "maxBytes": 64, "visible": false}
      ]
    },
    {"type": "panel", "id": "overlay", "style": {"alignItems": "end"}, "children": []}
  ]
})JSON";

const std::string kFullGolden =
    R"({"format":"uirepo.ui","version":1,"id":"main","style":{"width":{"percent":100},"height":600,)"
    R"("minWidth":100,"minHeight":50,"maxWidth":1920,"maxHeight":1080,"padding":8,"margin":[1,2,3,4],)"
    R"("gap":6,"flexGrow":1,"alignItems":"center","background":"#102030","opacity":0.5,"zIndex":-3,)"
    R"("cornerRadius":4,"clipToBounds":true},"children":[{"type":"stack","id":"column","axis":"column",)"
    R"("scrollable":true,"style":{"gap":4},"children":[{"type":"text","id":"title","text":"H)"
    "\xC3\xA9"
    R"(llo \"quoted\"\n)"
    "\xF0\x9F\x98\x80"
    R"(","fontSize":24.5,"color":"#ffcc00","textLayout":{"wrapping":"wrap","alignment":"center",)"
    R"("overflow":"ellipsis"}},{"type":"button","id":"ok","text":"OK","fontSize":16,"textColor":"#ffffff80",)"
    R"("focusedBackground":"#112233","textLayout":{"alignment":"end"},"style":{"padding":[4,2,4,2]}},)"
    R"({"type":"image","id":"logo","source":"textures/logo.png","naturalSize":[64,32],)"
    R"("style":{"width":{"percent":50}}},{"type":"video","id":"intro","source":"video/intro.bk2",)"
    R"("naturalSize":[1280,720],"playback":"playing","loop":true,"volume":0.25},)"
    R"({"type":"toggle","id":"mute","value":true,"style":{"width":"auto"},"enabled":false},)"
    R"({"type":"slider","id":"volume","min":0,"max":10,"step":0.5,"value":7.5},)"
    R"({"type":"stepper","id":"quality","options":["Low","Medium","High"],"index":2},)"
    R"({"type":"textInput","id":"name","value":"Ricky","maxBytes":64,"visible":false}]},)"
    R"({"type":"panel","id":"overlay","style":{"alignItems":"end"}}]})";

std::unique_ptr<Document> buildFullProgrammatically() {
    auto root = std::make_unique<PanelNode>("main");
    Style& rootStyle = root->style();
    rootStyle.width = Length::Percent(1.0f);
    rootStyle.height = Length::Pixels(600.0f);
    rootStyle.minWidth = 100.0f;
    rootStyle.minHeight = 50.0f;
    rootStyle.maxWidth = 1920.0f;
    rootStyle.maxHeight = 1080.0f;
    rootStyle.padding = Thickness(8.0f);
    rootStyle.margin = Thickness{1.0f, 2.0f, 3.0f, 4.0f};
    rootStyle.gap = 6.0f;
    rootStyle.flexGrow = 1.0f;
    rootStyle.alignItems = Align::Center;
    rootStyle.background = Color{0x10, 0x20, 0x30, 255};
    rootStyle.opacity = 0.5f;
    rootStyle.zIndex = -3;
    rootStyle.cornerRadius = 4.0f;
    rootStyle.clipToBounds = true;

    auto& column = root->emplaceChild<StackNode>("column", Axis::Column);
    column.setScrollable(true);
    column.style().gap = 4.0f;

    auto& title = column.emplaceChild<TextNode>("title", "H\xC3\xA9llo \"quoted\"\n\xF0\x9F\x98\x80");
    title.setFontSize(24.5f);
    title.setColor(Color{0xFF, 0xCC, 0x00, 255});
    title.setTextLayout(TextLayoutOptions{TextWrapping::Wrap, TextAlignment::Center, TextOverflow::Ellipsis});

    auto& ok = column.emplaceChild<ButtonNode>("ok", "OK");
    ok.setFontSize(16.0f);
    ok.setTextColor(Color{255, 255, 255, 0x80});
    ok.setFocusedBackground(Color{0x11, 0x22, 0x33, 255});
    ok.setTextLayout(TextLayoutOptions{TextWrapping::NoWrap, TextAlignment::End, TextOverflow::Clip});
    ok.style().padding = Thickness{4.0f, 2.0f, 4.0f, 2.0f};

    auto& logo = column.emplaceChild<ImageNode>("logo", "textures/logo.png", Size{64.0f, 32.0f});
    logo.style().width = Length::Percent(0.5f);

    auto& intro = column.emplaceChild<VideoNode>("intro", "video/intro.bk2", Size{1280.0f, 720.0f});
    intro.setPlaybackState(VideoPlaybackState::Playing);
    intro.setLoop(true);
    intro.setVolume(0.25f);

    auto& mute = column.emplaceChild<ToggleNode>("mute", true);
    mute.setEnabled(false);
    mute.style().width = Length::Auto();

    column.emplaceChild<SliderNode>("volume", 7.5f, 0.0f, 10.0f, 0.5f);
    column.emplaceChild<StepperNode>("quality", std::vector<std::string>{"Low", "Medium", "High"}, 2);
    auto& name = column.emplaceChild<TextInputNode>("name", "Ricky", 64);
    name.setVisible(false);

    auto& overlay = root->emplaceChild<PanelNode>("overlay");
    overlay.style().alignItems = Align::End;
    return std::make_unique<Document>(std::move(root));
}

void testMinimalDocument() {
    ParseResult result = parseOk(kMinimal, "minimal");
    const Document& document = *result.document;
    require(document.root().id() == "root", "minimal root id");
    require(as<PanelNode>(document.root()) != nullptr, "document root must be a panel");
    require(document.root().children().empty(), "minimal root has no children");
    require(canonical(document) == kMinimal, "minimal canonical form");
    require(sameStyle(document.root().style(), PanelNode("x").style()), "omitted style keeps constructor defaults");
}

void testFullDocumentValues() {
    ParseResult result = parseOk(kFull, "full");
    Document& document = *result.document;

    const Style& style = document.root().style();
    require(sameLength(style.width, Length::Percent(1.0f)) && sameLength(style.height, Length::Pixels(600.0f)),
            "root width/height");
    require(style.minWidth == 100.0f && style.minHeight == 50.0f && style.maxWidth == 1920.0f &&
                style.maxHeight == 1080.0f, "root min/max");
    require(sameThickness(style.padding, Thickness(8.0f)) && sameThickness(style.margin, Thickness{1, 2, 3, 4}),
            "root padding/margin");
    require(style.gap == 6.0f && style.flexGrow == 1.0f && style.alignItems == Align::Center, "root gap/flex/align");
    require(sameColor(style.background, Color{0x10, 0x20, 0x30, 255}) && style.opacity == 0.5f && style.zIndex == -3 &&
                style.cornerRadius == 4.0f && style.clipToBounds, "root background/opacity/z/radius/clip");

    const auto* column = as<StackNode>(*document.find("column"));
    require(column != nullptr && column->axis() == Axis::Column && column->scrollable(), "stack fields");
    const auto* title = as<TextNode>(*document.find("title"));
    require(title != nullptr && title->text() == "H\xC3\xA9llo \"quoted\"\n\xF0\x9F\x98\x80", "text escapes decode");
    require(title->fontSize() == 24.5f && sameColor(title->color(), Color{255, 204, 0, 255}), "text font/color");
    require(title->textLayout() == TextLayoutOptions{TextWrapping::Wrap, TextAlignment::Center, TextOverflow::Ellipsis},
            "text layout");
    const auto* ok = as<ButtonNode>(*document.find("ok"));
    require(ok != nullptr && sameColor(ok->textColor(), Color{255, 255, 255, 0x80}) &&
                sameColor(ok->focusedBackground(), Color{0x11, 0x22, 0x33, 255}), "button colors");
    require(ok->textLayout().alignment == TextAlignment::End && ok->textLayout().wrapping == TextWrapping::NoWrap,
            "partial text layout keeps defaults");
    require(sameColor(ok->background(), ButtonNode("x", "").background()), "button keeps constructor background");
    const auto* logo = as<ImageNode>(*document.find("logo"));
    require(logo != nullptr && logo->source() == "textures/logo.png" && logo->naturalSize().width == 64.0f, "image");
    const auto* intro = as<VideoNode>(*document.find("intro"));
    require(intro != nullptr && intro->playbackState() == VideoPlaybackState::Playing && intro->loop() &&
                intro->volume() == 0.25f && intro->naturalSize().height == 720.0f, "video");
    const auto* mute = as<ToggleNode>(*document.find("mute"));
    require(mute != nullptr && mute->value() && !mute->enabled() && mute->style().width.kind == LengthKind::Auto,
            "toggle value, enabled flag and width override of the constructor default");
    const auto* volume = as<SliderNode>(*document.find("volume"));
    require(volume != nullptr && volume->value() == 7.5f && volume->step() == 0.5f && volume->maximum() == 10.0f,
            "slider");
    const auto* quality = as<StepperNode>(*document.find("quality"));
    require(quality != nullptr && quality->index() == 2 && quality->value() == "High", "stepper");
    const auto* name = as<TextInputNode>(*document.find("name"));
    require(name != nullptr && name->value() == "Ricky" && name->maxBytes() == 64 && !name->visible(), "text input");
    require(as<PanelNode>(*document.find("overlay")) != nullptr &&
                document.find("overlay")->style().alignItems == Align::End, "overlay panel");
}

void testCanonicalGolden() {
    ParseResult result = parseOk(kFull, "full");
    require(canonical(*result.document) == kFullGolden, "full document canonical golden");
    require(canonical(*buildFullProgrammatically()) == kFullGolden, "programmatic document canonical golden");
}

void testRoundTrip() {
    const std::string spaced =
        "\r\n  {\t\"id\" : \"r\" ,\n \"version\":1,\"format\":\"uirepo.ui\",\r\n"
        " \"children\":[ {\"id\":\"s\",\"type\":\"slider\",\"min\":-1.5,\"max\":2.5,\"step\":0.25,\"value\":0.3}, "
        "{\"type\":\"textInput\",\"id\":\"t\",\"value\":\"a\\u0001b\\\\\\/\"} ] }\n";
    const std::vector<std::string> corpus{kMinimal, kFull, spaced};
    for (const std::string& text : corpus) {
        ParseResult first = parseOk(text, "round trip input");
        const std::string once = canonical(*first.document);
        ParseResult second = parseOk(once, "canonical output");
        require(sameTree(first.document->root(), second.document->root()), "parse(serialize(parse(x))) differs from parse(x)");
        require(canonical(*second.document) == once, "canonical form is not a fixed point");
    }
    ParseResult spacedResult = parseOk(spaced, "spaced");
    require(canonical(*spacedResult.document).find("\"value\":0.25") != std::string::npos,
            "slider value snaps to the step grid");
    require(canonical(*spacedResult.document).find("a\\u0001b\\\\/") != std::string::npos,
            "control characters use lowercase \\u escapes and slash is not escaped");

    auto programmatic = buildFullProgrammatically();
    ParseResult reparsed = parseOk(canonical(*programmatic), "programmatic canonical");
    require(sameTree(programmatic->root(), reparsed.document->root()), "programmatic document survives a round trip");
}

void testProgrammaticEquivalence() {
    ParseResult parsed = parseOk(kFull, "full");
    auto built = buildFullProgrammatically();
    require(sameTree(parsed.document->root(), built->root()), "parsed tree equals the programmatic tree");

    for (const Size viewport : {Size{1280.0f, 720.0f}, Size{320.0f, 200.0f}}) {
        parsed.document->layout(viewport);
        built->layout(viewport);
        const auto left = parsed.document->buildDisplayList();
        const auto right = built->buildDisplayList();
        require(!left.empty() && left.size() == right.size(), "display list sizes differ");
        for (std::size_t index = 0; index < left.size(); ++index) {
            require(sameCommand(left[index], right[index]), "display list command " + std::to_string(index) + " differs");
        }
    }
}

void testDefaultsAndOmission() {
    ParseResult result = parseOk(
        R"({"format":"uirepo.ui","version":1,"id":"r","children":[
            {"type":"button","id":"b"},{"type":"toggle","id":"t"},{"type":"slider","id":"s","min":0,"max":4},
            {"type":"stepper","id":"p","options":["a"]},{"type":"textInput","id":"i"},{"type":"text","id":"x"},
            {"type":"stack","id":"k","axis":"row"},{"type":"image","id":"m","source":"a"},
            {"type":"video","id":"v","source":"a"}]})",
        "defaults");
    Document& document = *result.document;
    require(sameStyle(document.find("b")->style(), ButtonNode("d", "").style()), "button style defaults");
    require(sameStyle(document.find("t")->style(), ToggleNode("d").style()), "toggle style defaults");
    require(sameStyle(document.find("i")->style(), TextInputNode("d").style()), "text input style defaults");
    require(std::isinf(document.find("x")->style().maxWidth), "omitted maximum is unbounded");
    require(as<SliderNode>(*document.find("s"))->step() == 1.0f && as<SliderNode>(*document.find("s"))->value() == 0.0f,
            "slider step and value defaults");
    require(as<TextInputNode>(*document.find("i"))->maxBytes() == TextInputNode("d").maxBytes(),
            "text input byte budget default matches the constructor");
    require(as<VideoNode>(*document.find("v"))->volume() == VideoNode("d", "a", Size{}).volume(), "video volume default");
    require(as<StackNode>(*document.find("k"))->scrollable() == false, "stack is not scrollable by default");
    require(as<TextNode>(*document.find("x"))->text().empty() &&
                as<TextNode>(*document.find("x"))->fontSize() == TextNode("d", "").fontSize(), "text defaults");
    const std::string text = canonical(document);
    require(text.find("style") == std::string::npos && text.find("visible") == std::string::npos &&
                text.find("scrollable") == std::string::npos, "defaults are omitted from canonical output");
}

void testLengthColorAndThicknessForms() {
    ParseResult result = parseOk(
        R"({"format":"uirepo.ui","version":1,"id":"r","style":{"width":"auto","height":{"percent":12.5},
            "padding":[0,1,2,3],"margin":0,"background":"#AbCdEf","maxWidth":1e3,"minHeight":-0}})",
        "forms");
    const Style& style = result.document->root().style();
    require(style.width.kind == LengthKind::Auto && sameLength(style.height, Length::Percent(0.125f)), "length forms");
    require(sameThickness(style.padding, Thickness{0, 1, 2, 3}) && sameThickness(style.margin, Thickness(0.0f)),
            "thickness forms");
    require(sameColor(style.background, Color{0xAB, 0xCD, 0xEF, 255}), "mixed case color");
    require(style.maxWidth == 1000.0f && style.minHeight == 0.0f && !std::signbit(style.minHeight), "numbers");
    require(canonical(*result.document) ==
                R"({"format":"uirepo.ui","version":1,"id":"r","style":{"height":{"percent":12.5},"maxWidth":1000,)"
                R"("padding":[0,1,2,3],"background":"#abcdef"}})",
            "canonical forms drop defaults and normalise -0");
}

void testExtensionFields() {
    const std::string plain = R"({"format":"uirepo.ui","version":1,"id":"r","children":[{"type":"text","id":"t","text":"a"}]})";
    const std::string extended =
        R"({"x-a":1,"format":"uirepo.ui","x-b":{"deep":[1,2,{"x":null}]},"version":1,"id":"r","x-":0,
            "children":[{"x-c":[],"type":"text","id":"t","text":"a","x-d":"v","style":{"x-e":1},
            "textLayout":{"x-f":1}}]})";
    ParseResult left = parseOk(plain, "plain");
    ParseResult right = parseOk(extended, "extended");
    require(sameTree(left.document->root(), right.document->root()), "x- fields must not affect the model");
    require(canonical(*right.document) == plain, "x- fields are not preserved by serialization");
    require(!parseDocument(R"({"format":"uirepo.ui","version":1,"id":"r","X-a":1})").ok(), "x- prefix is case sensitive");
    require(!parseDocument(R"({"format":"uirepo.ui","version":1,"id":"r","xa":1})").ok(), "x without dash is unknown");
}

void testEveryEnumValueRoundTrips() {
    const char* aligns[] = {"start", "center", "end", "stretch"};
    const char* wrapping[] = {"noWrap", "wrap"};
    const char* alignment[] = {"start", "center", "end"};
    const char* overflow[] = {"clip", "ellipsis"};
    const char* playback[] = {"stopped", "playing", "paused"};
    std::string kids;
    const auto append = [&kids](const std::string& node) { kids += (kids.empty() ? "" : ",") + node; };
    for (const char* value : aligns) {
        append(std::string(R"({"type":"panel","id":"align_)") + value + R"(","style":{"alignItems":")" + value + R"("}})");
    }
    for (const char* value : {"row", "column"}) {
        append(std::string(R"({"type":"stack","id":"axis_)") + value + R"(","axis":")" + value + R"("})");
    }
    for (const char* w : wrapping) {
        for (const char* a : alignment) {
            for (const char* o : overflow) {
                const std::string id = std::string("layout_") + w + "_" + a + "_" + o;
                const std::string layout = std::string(R"("textLayout":{"wrapping":")") + w + R"(","alignment":")" + a +
                                           R"(","overflow":")" + o + R"("})";
                append(R"({"type":"text","id":")" + id + R"(",)" + layout + "}");
                append(R"({"type":"button","id":"b_)" + id + R"(",)" + layout + "}");
            }
        }
    }
    for (const char* value : playback) {
        append(std::string(R"({"type":"video","id":"play_)") + value + R"(","source":"v","playback":")" + value + R"("})");
    }
    ParseResult first = parseOk(R"({"format":"uirepo.ui","version":1,"id":"r","children":[)" + kids + "]}", "enum sweep");
    const std::string once = canonical(*first.document);
    ParseResult second = parseOk(once, "enum sweep canonical");
    require(sameTree(first.document->root(), second.document->root()) && canonical(*second.document) == once,
            "every enum value must survive a round trip");
    require(as<VideoNode>(*first.document->find("play_paused"))->playbackState() == VideoPlaybackState::Paused &&
                as<TextNode>(*first.document->find("layout_wrap_end_ellipsis"))->textLayout() ==
                    TextLayoutOptions{TextWrapping::Wrap, TextAlignment::End, TextOverflow::Ellipsis} &&
                first.document->find("align_stretch")->style().alignItems == Align::Stretch &&
                as<StackNode>(*first.document->find("axis_row"))->axis() == Axis::Row,
            "enum names map to the matching model values");
}

void testNumberForms() {
    ParseResult result = parseOk(
        R"({"format":"uirepo.ui","version":1,"id":"r","style":{"gap":0.00001,"maxWidth":1e10,"minWidth":1E3,
            "flexGrow":1000.0,"cornerRadius":0.1e1,"zIndex":-2147483648,"opacity":1e-1,"minHeight":16777217}})",
        "numbers");
    const Style& style = result.document->root().style();
    require(style.gap == 0.00001f && style.maxWidth == 1e10f && style.minWidth == 1000.0f && style.flexGrow == 1000.0f &&
                style.cornerRadius == 1.0f && style.zIndex == std::numeric_limits<std::int32_t>::min() &&
                style.opacity == 0.1f && style.minHeight == 16777216.0f, "number forms decode to the nearest float");
    const std::string once = canonical(*result.document);
    require(once.find("\"gap\":1e-05") != std::string::npos && once.find("\"maxWidth\":1e+10") != std::string::npos &&
                once.find("\"zIndex\":-2147483648") != std::string::npos && once.find("\"opacity\":0.1") != std::string::npos,
            "canonical numbers use the shortest round-trip form: " + once);
    ParseResult again = parseOk(once, "canonical numbers");
    require(sameTree(result.document->root(), again.document->root()), "canonical numbers parse back to the same floats");
    require(SliderNode("d", 0.0f, 0.0f, 1.0f).step() == 1.0f, "slider step default matches the documented default");
}

void testFloatUnderflowReadsAsZero() {
    std::string zeros(60, '0');
    ParseResult result = parseOk(
        R"({"format":"uirepo.ui","version":1,"id":"r","style":{"opacity":1e-50,"gap":0.)" + zeros +
            R"(1,"cornerRadius":-1E-400,"flexGrow":12345678901234567890e-400,"minWidth":0e-999,"zIndex":0}})",
        "underflow");
    const Style& style = result.document->root().style();
    require(style.opacity == 0.0f && style.gap == 0.0f && style.cornerRadius == 0.0f && style.flexGrow == 0.0f &&
                style.minWidth == 0.0f && !std::signbit(style.cornerRadius), "underflowing numbers read as positive zero");
    require(parseDocument(R"({"format":"uirepo.ui","version":1,"id":"r","style":{"gap":1e-40}})").ok(), "denormals still parse");
    const ParseResult overflow = parseDocument(
        R"({"format":"uirepo.ui","version":1,"id":"r","style":{"gap":1)" + zeros + R"(e-20}})");
    require(!overflow.ok() && overflow.diagnostics.front().code == ErrorCode::NumberRange,
            "a large mantissa with an extreme exponent is not an underflow");
}

void testSliderFixedPoint() {
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    const auto next = [&state]() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(state >> 11) / 9007199254740992.0;
    };
    std::size_t accepted = 0;
    std::size_t refused = 0;
    for (int index = 0; index < 20000; ++index) {
        const double minimum = (next() - 0.5) * std::pow(10.0, 1.0 + next() * 5.0);
        const double span = std::pow(10.0, -1.0 + next() * 6.0);
        const double steps = std::pow(2.0, 2.0 + next() * 22.0);
        char text[256];
        std::snprintf(text, sizeof text,
                      R"({"format":"uirepo.ui","version":1,"id":"r","children":[{"type":"slider","id":"s","min":%.9g,"max":%.9g,"step":%.9g,"value":%.9g}]})",
                      minimum, minimum + span, span / steps, minimum + span * next());
        const ParseResult first = parseDocument(text);
        if (!first.ok()) {
            ++refused;
            require(first.diagnostics.front().code != ErrorCode::NumberRange, "fuzzed slider text must be well formed");
            continue;
        }
        ++accepted;
        const std::string once = canonical(*first.document);
        ParseResult second = parseOk(once, "slider canonical output");
        require(sameTree(first.document->root(), second.document->root()) && canonical(*second.document) == once,
                std::string("slider does not survive a round trip: ") + text);
    }
    require(accepted > 10000 && refused > 0, "slider fuzz must exercise both accepted and refused documents");
}

void testSerializeDiagnostics() {
    const auto expectFailure = [](Document& document, ErrorCode code, const std::string& path, const char* label) {
        const SerializeResult result = serializeDocument(document);
        require(!result.ok() && result.text.empty(), std::string(label) + ": serialize must fail without text");
        require(result.diagnostics.front().code == code && result.diagnostics.front().path == path &&
                    result.diagnostics.front().line == 0, std::string(label) + ": diagnostic " +
                        std::string(errorCodeName(result.diagnostics.front().code)) + " " + result.diagnostics.front().path);
    };

    {
        auto root = std::make_unique<PanelNode>("root");
        root->emplaceChild<PanelNode>("bad id");
        Document document(std::move(root));
        expectFailure(document, ErrorCode::InvalidId, "/children/0/id", "invalid id");
    }
    {
        auto root = std::make_unique<PanelNode>("root");
        root->emplaceChild<TextNode>("t", "caf\xE9");
        Document document(std::move(root));
        expectFailure(document, ErrorCode::InvalidUtf8, "/children/0/text", "bad utf-8");
    }
    {
        auto root = std::make_unique<PanelNode>("root");
        root->emplaceChild<TextNode>("t", std::string("a\0b", 3));
        Document document(std::move(root));
        expectFailure(document, ErrorCode::InvalidString, "/children/0/text", "nul");
    }
    {
        auto root = std::make_unique<PanelNode>("root");
        auto& text = root->emplaceChild<TextNode>("t", "x");
        text.emplaceChild<PanelNode>("c");
        Document document(std::move(root));
        expectFailure(document, ErrorCode::ChildrenNotAllowed, "/children/0/children", "leaf children");
    }
    {
        auto root = std::make_unique<PanelNode>("root");
        root->style().gap = std::numeric_limits<float>::quiet_NaN();
        Document document(std::move(root));
        expectFailure(document, ErrorCode::InvalidValue, "/style", "non-finite style");
    }
    {
        auto root = std::make_unique<PanelNode>("root");
        root->emplaceChild<ImageNode>("i", "a.png", Size{-1.0f, 2.0f});
        Document document(std::move(root));
        expectFailure(document, ErrorCode::InvalidValue, "/children/0/naturalSize", "negative natural size");
    }
    {
        auto root = std::make_unique<PanelNode>("root");
        root->emplaceChild<ImageNode>("i", "", Size{});
        Document document(std::move(root));
        expectFailure(document, ErrorCode::InvalidValue, "/children/0/source", "empty asset");
    }
    {
        auto root = std::make_unique<PanelNode>("root");
        root->emplaceChild<StepperNode>("p", std::vector<std::string>{});
        Document document(std::move(root));
        expectFailure(document, ErrorCode::InvalidValue, "/children/0/options", "empty stepper");
    }
    {
        auto root = std::make_unique<PanelNode>("root");
        root->emplaceChild<SliderNode>("s", 0.0f, 0.0f, 1.0f, 5.0f);
        Document document(std::move(root));
        expectFailure(document, ErrorCode::InvalidStep, "/children/0/step", "slider step above range");
    }
    {
        auto root = std::make_unique<PanelNode>("root");
        root->emplaceChild<SliderNode>("s", 0.0f, 0.0f, 0.5f);
        Document document(std::move(root));
        expectFailure(document, ErrorCode::InvalidStep, "/children/0/step", "default slider step above range");
        require(serializeDocument(document).diagnostics.front().message.find("default step") != std::string::npos,
                "default step diagnostic names the default step");
    }
    {
        auto root = std::make_unique<PanelNode>("root");
        root->emplaceChild<SliderNode>("s", 0.0f, -3e38f, 3e38f, 1e30f);
        Document document(std::move(root));
        expectFailure(document, ErrorCode::InvalidRange, "/children/0/max", "slider range overflows float");
    }
    {
        auto root = std::make_unique<PanelNode>("root");
        root->emplaceChild<SliderNode>("s", -44.0f, -117.285714f, 88.4f, 1e-5f);
        Document document(std::move(root));
        expectFailure(document, ErrorCode::InvalidStep, "/children/0/step", "slider step too fine for the value precision");
    }
    {
        auto root = std::make_unique<StackNode>("root", Axis::Row);
        Document document(std::move(root));
        expectFailure(document, ErrorCode::UnsupportedType, "", "non-panel root");
    }
}

void testProgrammaticMaximumFolding() {
    auto root = std::make_unique<PanelNode>("root");
    root->style().maxWidth = 0.0f;
    Document document(std::move(root));
    require(canonical(document) == kMinimal, "a zero maximum serializes as unbounded");
}

void testDeterminism() {
    ParseResult one = parseOk(kFull, "full");
    ParseResult two = parseOk(kFull, "full");
    require(canonical(*one.document) == canonical(*two.document), "serialization is deterministic");
    std::string crlf;
    for (const char c : std::string(kFull)) {
        if (c == '\n') {
            crlf += '\r';
        }
        crlf += c;
    }
    ParseResult three = parseOk(crlf, "crlf");
    require(canonical(*three.document) == canonical(*one.document), "line endings do not affect the model");
}

std::string nestedPanels(std::size_t levels) {
    std::string text = R"({"format":"uirepo.ui","version":1,"id":"n0")";
    for (std::size_t level = 1; level <= levels; ++level) {
        text += R"(,"children":[{"type":"panel","id":"n)" + std::to_string(level) + "\"";
    }
    for (std::size_t level = 0; level < levels; ++level) {
        text += "}]";
    }
    return text + "}";
}

std::string flatPanels(std::size_t count) {
    std::string text = R"({"format":"uirepo.ui","version":1,"id":"root","children":[)";
    for (std::size_t index = 1; index < count; ++index) {
        text += (index > 1 ? "," : "") + std::string(R"({"type":"panel","id":"p)") + std::to_string(index) + "\"}";
    }
    return text + "]}";
}

void testLimits() {
    Limits tight;
    tight.maxDepth = 7;
    require(parseDocument(nestedPanels(3), tight).ok(), "depth exactly at the limit parses");
    ParseResult deep = parseDocument(nestedPanels(4), tight);
    require(!deep.ok() && deep.diagnostics.front().code == ErrorCode::LimitDepth, "one level past the depth limit");

    ParseResult huge = parseDocument(nestedPanels(20000));
    require(!huge.ok() && huge.diagnostics.front().code == ErrorCode::LimitDepth, "very deep input is refused, not recursed");
    Limits generous;
    generous.maxDepth = 1'000'000;
    generous.maxBytes = 1u << 24;
    ParseResult clamped = parseDocument(nestedPanels(20000), generous);
    require(!clamped.ok() && clamped.diagnostics.front().code == ErrorCode::LimitDepth,
            "a caller supplied depth limit is clamped to the hard maximum");
    ParseResult brackets = parseDocument(std::string(100000, '['), generous);
    require(!brackets.ok() && brackets.diagnostics.front().code == ErrorCode::LimitDepth, "bracket flood");

    require(parseDocument(flatPanels(4096)).ok(), "node count exactly at the limit parses");
    ParseResult many = parseDocument(flatPanels(4097));
    require(!many.ok() && many.diagnostics.front().code == ErrorCode::LimitNodes &&
                many.diagnostics.front().path == "/children/4095", "one node past the limit: " + many.diagnostics.front().path);
    Limits small;
    small.maxNodes = 2;
    require(parseDocument(flatPanels(2), small).ok() && !parseDocument(flatPanels(3), small).ok(), "custom node limit");

    const std::string exact(kMinimal);
    Limits bytes;
    bytes.maxBytes = exact.size();
    require(parseDocument(exact, bytes).ok(), "size exactly at the limit parses");
    bytes.maxBytes = exact.size() - 1;
    ParseResult large = parseDocument(exact, bytes);
    require(!large.ok() && large.diagnostics.front().code == ErrorCode::LimitSize &&
                large.diagnostics.front().line == 1 && large.diagnostics.front().column == 1, "one byte past the size limit");

    Limits capped;
    capped.maxDiagnostics = 2;
    ParseResult noisy = parseDocument(
        R"({"format":"uirepo.ui","version":1,"id":"r","a":1,"b":2,"c":3})", capped);
    require(!noisy.ok() && noisy.diagnostics.size() == 2, "diagnostics are capped");
    ParseResult all = parseDocument(R"({"format":"uirepo.ui","version":1,"id":"r","a":1,"b":2,"c":3})");
    require(all.diagnostics.size() == 3, "all independent diagnostics are collected");

    Document bigDocument(std::make_unique<PanelNode>("root"));
    for (int index = 0; index < 5000; ++index) {
        bigDocument.root().emplaceChild<PanelNode>("p" + std::to_string(index));
    }
    SerializeResult tooMany = serializeDocument(bigDocument);
    require(!tooMany.ok() && tooMany.diagnostics.front().code == ErrorCode::LimitNodes, "serialize honours the node limit");
    Limits roomy;
    roomy.maxNodes = 5001;
    SerializeResult okay = serializeDocument(bigDocument, roomy);
    require(okay.ok() && parseDocument(okay.text, roomy).ok(), "serialize output re-parses under the same limits");
    Limits shallow;
    shallow.maxDepth = 3;
    ParseResult nested = parseOk(nestedPanels(3), "nested");
    SerializeResult refused = serializeDocument(*nested.document, shallow);
    require(!refused.ok() && refused.diagnostics.front().code == ErrorCode::LimitDepth, "serialize honours the depth limit");
}

std::uint64_t nextRandom(std::uint64_t& state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

void testMutationLoop() {
    const std::vector<std::string> seeds{kFull, kMinimal, nestedPanels(5), flatPanels(8)};
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    std::size_t parsed = 0;
    for (std::size_t iteration = 0; iteration < 60000; ++iteration) {
        std::string text = seeds[nextRandom(state) % seeds.size()];
        const std::size_t edits = 1 + nextRandom(state) % 4;
        for (std::size_t edit = 0; edit < edits && !text.empty(); ++edit) {
            const std::size_t at = nextRandom(state) % text.size();
            switch (nextRandom(state) % 6) {
            case 0: text[at] = static_cast<char>(nextRandom(state)); break;
            case 1: text.erase(at, 1 + nextRandom(state) % 8); break;
            case 2: text.insert(at, 1, static_cast<char>(nextRandom(state))); break;
            case 3: text.insert(at, text.substr(nextRandom(state) % text.size(), 1 + nextRandom(state) % 24)); break;
            case 4: text.resize(at); break;
            default: text[at] = "{}[],:\"\\0123456789-+.eE tfn#"[nextRandom(state) % 28]; break;
            }
        }
        ParseResult result = parseDocument(text);
        require(result.ok() != !result.diagnostics.empty(), "parse result must carry exactly one of document or diagnostics");
        if (!result.ok()) {
            const Diagnostic& first = result.diagnostics.front();
            require(first.line >= 1 && first.column >= 1 && !errorCodeName(first.code).empty() && first.path.find('\0') == std::string::npos,
                    "diagnostics always carry a position");
            continue;
        }
        ++parsed;
        const SerializeResult once = serializeDocument(*result.document);
        require(once.ok(), "a parsed document must serialize");
        ParseResult again = parseDocument(once.text);
        require(again.ok(), "canonical output must parse: " + once.text);
        require(sameTree(result.document->root(), again.document->root()), "mutated round trip differs: " + once.text);
        require(canonical(*again.document) == once.text, "mutated canonical form is not a fixed point");
    }
    require(parsed > 100, "the mutation loop should keep reaching valid documents");
}

} // namespace

int main() {
    testMinimalDocument();
    testFullDocumentValues();
    testCanonicalGolden();
    testRoundTrip();
    testProgrammaticEquivalence();
    testDefaultsAndOmission();
    testLengthColorAndThicknessForms();
    testExtensionFields();
    testEveryEnumValueRoundTrips();
    testNumberForms();
    testFloatUnderflowReadsAsZero();
    testSliderFixedPoint();
    testSerializeDiagnostics();
    testProgrammaticMaximumFolding();
    testDeterminism();
    testLimits();
    testMutationLoop();
    uirepo::declarative::testing::runErrorTable();
    uirepo::declarative::testing::runPercentTests();
    std::cout << "declarative tests passed\n";
    return 0;
}
