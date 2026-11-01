#include "DeclarativeTestSupport.h"

#include <set>
#include <string>
#include <vector>

using namespace uirepo;
using namespace uirepo::declarative;
using namespace uirepo::declarative::testing;

namespace {

struct Case {
    std::string label;
    std::string text;
    ErrorCode code;
    std::string path;
    std::uint32_t line;
    std::uint32_t column;
    Limits limits;
};

constexpr std::string_view kMarker = "@@";

Case make(std::string label, std::string marked, ErrorCode code, std::string path, Limits limits = {}) {
    const std::size_t at = marked.find(kMarker);
    require(at != std::string::npos, label + ": case has no position marker");
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    for (std::size_t index = 0; index < at; ++index) {
        if (marked[index] == '\n') {
            ++line;
            column = 1;
        } else if ((static_cast<unsigned char>(marked[index]) & 0xC0u) != 0x80u) {
            ++column;
        }
    }
    marked.erase(at, kMarker.size());
    return Case{std::move(label), std::move(marked), code, std::move(path), line, column, limits};
}

const std::string kBase = R"({"format":"uirepo.ui","version":1,"id":"r")";

std::string root(const std::string& tail) {
    return kBase + tail + "}";
}

std::string child(const std::string& node) {
    return root(R"(,"children":[)" + node + "]");
}

std::vector<Case> buildCases() {
    std::vector<Case> cases;
    const auto add = [&cases](Case value) { cases.push_back(std::move(value)); };

    add(make("empty input", "@@", ErrorCode::SyntaxError, ""));
    add(make("trailing comma", R"({"format":"uirepo.ui",@@})", ErrorCode::SyntaxError, ""));
    add(make("line comment", "@@// note\n{}", ErrorCode::SyntaxError, ""));
    add(make("byte order mark", "@@\xEF\xBB\xBF{}", ErrorCode::SyntaxError, ""));
    add(make("leading zero", R"({"version":@@01})", ErrorCode::SyntaxError, "/version"));
    add(make("trailing content", "{} @@x", ErrorCode::SyntaxError, ""));
    add(make("unterminated string", R"({"a":@@"abc)", ErrorCode::SyntaxError, "/a"));
    add(make("truncated literal", R"({"a":@@tru})", ErrorCode::SyntaxError, "/a"));
    add(make("missing colon", R"({"a" @@1})", ErrorCode::SyntaxError, ""));
    add(make("NaN literal", R"({"version":@@NaN})", ErrorCode::SyntaxError, "/version"));
    add(make("single quoted key", R"({@@'a':1})", ErrorCode::SyntaxError, ""));
    add(make("unexpected end", R"({"a":1@@)", ErrorCode::SyntaxError, ""));
    add(make("missing array comma", root(R"(,"children":[1 @@2])"), ErrorCode::SyntaxError, "/children"));
    add(make("position on second line", "{\n  \"a\": @@+1\n}", ErrorCode::SyntaxError, "/a"));
    add(make("column counts code points", "{\"x-\xC3\xA9\xC3\xA9\":1,@@}", ErrorCode::SyntaxError, ""));
    add(make("CRLF line counting", "{\r\n\"a\":1,\r\n@@}", ErrorCode::SyntaxError, ""));

    add(make("invalid utf-8", "{\"format\":\"@@\xFF\"}", ErrorCode::InvalidUtf8, "/format"));
    add(make("overlong utf-8", "{\"format\":\"@@\xC0\x80\"}", ErrorCode::InvalidUtf8, "/format"));
    add(make("invalid escape", R"({"format":"a@@\qb"})", ErrorCode::InvalidString, "/format"));
    add(make("control character", "{\"format\":\"a@@\tb\"}", ErrorCode::InvalidString, "/format"));
    add(make("NUL escape", R"({"format":"@@\u0000"})", ErrorCode::InvalidString, "/format"));
    add(make("lone surrogate", R"({"format":"@@\ud800x"})", ErrorCode::InvalidString, "/format"));

    add(make("duplicate key", root(R"(,@@"id":"s")"), ErrorCode::DuplicateKey, "/id"));
    add(make("duplicate nested key", child(R"({"type":"panel","id":"c","style":{"gap":1,@@"gap":2}})"),
             ErrorCode::DuplicateKey, "/children/0/style/gap"));
    add(make("float out of range", root(R"(,"style":{"gap":@@1e999})"), ErrorCode::NumberRange, "/style/gap"));
    add(make("integer out of range", R"({"format":"uirepo.ui","version":@@99999999999999999999,"id":"r"})",
             ErrorCode::NumberRange, "/version"));
    add(make("zIndex beyond 32 bits", root(R"(,"style":{"zIndex":@@4294967296})"), ErrorCode::InvalidValue, "/style/zIndex"));

    Limits bytes;
    bytes.maxBytes = 10;
    add(make("size limit", "@@" + root(""), ErrorCode::LimitSize, "", bytes));
    Limits depth;
    depth.maxDepth = 2;
    add(make("depth limit", root(R"(,"children":[@@{}])"), ErrorCode::LimitDepth, "/children/0", depth));
    Limits nodes;
    nodes.maxNodes = 1;
    add(make("node limit", child(R"(@@{"type":"panel","id":"c"})"), ErrorCode::LimitNodes, "/children/0", nodes));

    add(make("document is not an object", "@@[]", ErrorCode::TypeMismatch, ""));
    add(make("visible is not boolean", root(R"(,"visible":@@"yes")"), ErrorCode::TypeMismatch, "/visible"));
    add(make("enabled is a number", child(R"({"type":"panel","id":"c","enabled":@@0})"), ErrorCode::TypeMismatch,
             "/children/0/enabled"));
    add(make("children is not an array", root(R"(,"children":@@{})"), ErrorCode::TypeMismatch, "/children"));
    add(make("child is not an object", root(R"(,"children":[@@3])"), ErrorCode::TypeMismatch, "/children/0"));
    add(make("number is a string", root(R"(,"style":{"gap":@@"6"})"), ErrorCode::TypeMismatch, "/style/gap"));
    add(make("zIndex is fractional", root(R"(,"style":{"zIndex":@@1.5})"), ErrorCode::TypeMismatch, "/style/zIndex"));
    add(make("version is fractional", R"({"format":"uirepo.ui","version":@@1.0,"id":"r"})", ErrorCode::TypeMismatch, "/version"));
    add(make("stepper option is not a string", child(R"({"type":"stepper","id":"p","options":["a",@@2]})"),
             ErrorCode::TypeMismatch, "/children/0/options/1"));
    add(make("style is not an object", root(R"(,"style":@@[])"), ErrorCode::TypeMismatch, "/style"));

    add(make("missing id", R"(@@{"format":"uirepo.ui","version":1})", ErrorCode::MissingField, ""));
    add(make("missing version", R"(@@{"format":"uirepo.ui","id":"r"})", ErrorCode::MissingField, ""));
    add(make("missing node type", child(R"(@@{"id":"c"})"), ErrorCode::MissingField, "/children/0"));
    add(make("missing slider max", child(R"(@@{"type":"slider","id":"s","min":0})"), ErrorCode::MissingField,
             "/children/0"));
    add(make("missing stack axis", child(R"(@@{"type":"stack","id":"s"})"), ErrorCode::MissingField, "/children/0"));
    add(make("missing image source", child(R"(@@{"type":"image","id":"i"})"), ErrorCode::MissingField, "/children/0"));

    add(make("unknown document field", root(R"(,@@"bogus":1)"), ErrorCode::UnknownField, "/bogus"));
    add(make("field not valid for type", child(R"({"type":"panel","id":"c",@@"text":"x"})"), ErrorCode::UnknownField,
             "/children/0/text"));
    add(make("unknown style field", root(R"(,"style":{@@"color":"#fff"})"), ErrorCode::UnknownField, "/style/color"));
    add(make("type on the root", root(R"(,@@"type":"panel")"), ErrorCode::UnknownField, "/type"));
    add(make("slash in key is escaped", root(R"(,@@"a/b~c":1)"), ErrorCode::UnknownField, "/a~1b~0c"));

    add(make("other format", R"({"format":@@"other.ui","version":1,"id":"r"})", ErrorCode::UnsupportedFormat, "/format"));
    add(make("newer major version", R"({"format":"uirepo.ui","version":@@2,"id":"r"})", ErrorCode::UnsupportedVersion, "/version"));
    add(make("zero version", R"({"format":"uirepo.ui","version":@@0,"id":"r"})", ErrorCode::InvalidValue, "/version"));
    add(make("unknown node type", child(R"({"type":@@"carousel","id":"c"})"), ErrorCode::UnsupportedType, "/children/0/type"));
    add(make("node type is case sensitive", child(R"({"type":@@"Panel","id":"c"})"), ErrorCode::UnsupportedType, "/children/0/type"));

    add(make("id starts with a digit", R"({"format":"uirepo.ui","version":1,"id":@@"1abc"})", ErrorCode::InvalidId, "/id"));
    add(make("id has a space", child(R"({"type":"panel","id":@@"a b"})"), ErrorCode::InvalidId, "/children/0/id"));
    add(make("id is empty", child(R"({"type":"panel","id":@@""})"), ErrorCode::InvalidId, "/children/0/id"));
    add(make("id is too long", child(R"({"type":"panel","id":@@")" + std::string(129, 'a') + R"("})"), ErrorCode::InvalidId,
             "/children/0/id"));
    add(make("duplicate id", child(R"({"type":"panel","id":@@"r"})"), ErrorCode::DuplicateId, "/children/0/id"));
    add(make("duplicate sibling id",
             root(R"(,"children":[{"type":"panel","id":"c"},{"type":"text","id":@@"c"}])"), ErrorCode::DuplicateId,
             "/children/1/id"));

    add(make("unknown alignment", root(R"(,"style":{"alignItems":@@"middle"})"), ErrorCode::InvalidEnum, "/style/alignItems"));
    add(make("unknown axis", child(R"({"type":"stack","id":"s","axis":@@"diagonal"})"), ErrorCode::InvalidEnum,
             "/children/0/axis"));
    add(make("unknown wrapping", child(R"({"type":"text","id":"t","textLayout":{"wrapping":@@"yes"}})"),
             ErrorCode::InvalidEnum, "/children/0/textLayout/wrapping"));
    add(make("unknown playback", child(R"({"type":"video","id":"v","source":"a","playback":@@"rewind"})"),
             ErrorCode::InvalidEnum, "/children/0/playback"));
    add(make("unknown length keyword", root(R"(,"style":{"width":@@"fill"})"), ErrorCode::InvalidEnum, "/style/width"));
    add(make("unknown color", root(R"(,"style":{"background":@@"red"})"), ErrorCode::InvalidColor, "/style/background"));
    add(make("short color", root(R"(,"style":{"background":@@"#fff"})"), ErrorCode::InvalidColor, "/style/background"));
    add(make("non hex color", child(R"({"type":"text","id":"t","color":@@"#gg0000"})"), ErrorCode::InvalidColor,
             "/children/0/color"));

    add(make("negative width", root(R"(,"style":{"width":@@-1})"), ErrorCode::InvalidValue, "/style/width"));
    add(make("negative percent", root(R"(,"style":{"height":{"percent":@@-5}})"), ErrorCode::InvalidValue,
             "/style/height/percent"));
    add(make("negative minimum", root(R"(,"style":{"minWidth":@@-1})"), ErrorCode::InvalidValue, "/style/minWidth"));
    add(make("zero maximum", root(R"(,"style":{"maxWidth":@@0})"), ErrorCode::InvalidValue, "/style/maxWidth"));
    add(make("negative padding side", root(R"(,"style":{"padding":[1,2,@@-3,4]})"), ErrorCode::InvalidValue,
             "/style/padding/2"));
    add(make("padding needs four sides", root(R"(,"style":{"padding":@@[1,2,3]})"), ErrorCode::InvalidValue, "/style/padding"));
    add(make("opacity above one", root(R"(,"style":{"opacity":@@2})"), ErrorCode::InvalidValue, "/style/opacity"));
    add(make("negative gap", root(R"(,"style":{"gap":@@-0.5})"), ErrorCode::InvalidValue, "/style/gap"));
    add(make("negative flex grow", root(R"(,"style":{"flexGrow":@@-1})"), ErrorCode::InvalidValue, "/style/flexGrow"));
    add(make("negative corner radius", root(R"(,"style":{"cornerRadius":@@-1})"), ErrorCode::InvalidValue,
             "/style/cornerRadius"));
    add(make("zero font size", child(R"({"type":"text","id":"t","fontSize":@@0})"), ErrorCode::InvalidValue,
             "/children/0/fontSize"));
    add(make("volume above one", child(R"({"type":"video","id":"v","source":"a","volume":@@1.5})"), ErrorCode::InvalidValue,
             "/children/0/volume"));
    add(make("negative natural size", child(R"({"type":"image","id":"i","source":"a","naturalSize":[1,@@-2]})"),
             ErrorCode::InvalidValue, "/children/0/naturalSize/1"));
    add(make("natural size needs two numbers", child(R"({"type":"image","id":"i","source":"a","naturalSize":@@[1]})"),
             ErrorCode::InvalidValue, "/children/0/naturalSize"));
    add(make("empty asset", child(R"({"type":"image","id":"i","source":@@""})"), ErrorCode::InvalidValue,
             "/children/0/source"));
    add(make("asset with control character", child("{\"type\":\"video\",\"id\":\"v\",\"source\":@@\"a\\nb\"}"),
             ErrorCode::InvalidValue, "/children/0/source"));
    add(make("empty stepper options", child(R"({"type":"stepper","id":"p","options":@@[]})"), ErrorCode::InvalidValue,
             "/children/0/options"));
    add(make("zero text input budget", child(R"({"type":"textInput","id":"i","maxBytes":@@0})"), ErrorCode::InvalidValue,
             "/children/0/maxBytes"));
    add(make("text input budget above the cap", child(R"({"type":"textInput","id":"i","maxBytes":@@1048577})"),
             ErrorCode::InvalidValue, "/children/0/maxBytes"));
    add(make("percent beyond float range", root(R"(,"style":{"width":{"percent":@@1e999}})"), ErrorCode::NumberRange,
             "/style/width/percent"));
    add(make("percent fraction overflows float", root(R"(,"style":{"width":{"percent":@@3.5e40}})"), ErrorCode::NumberRange,
             "/style/width/percent"));
    add(make("percent object needs percent", root(R"(,"style":{"width":@@{}})"), ErrorCode::MissingField, "/style/width"));

    add(make("min above max", child(R"({"type":"stack","id":"s","axis":"row","style":{"minWidth":100,"maxWidth":@@50}})"),
             ErrorCode::InvalidRange, "/children/0/style/maxWidth"));
    add(make("min above max height", root(R"(,"style":{"minHeight":10,"maxHeight":@@9})"), ErrorCode::InvalidRange,
             "/style/maxHeight"));
    add(make("slider min equals max", child(R"({"type":"slider","id":"s","min":5,"max":@@5})"), ErrorCode::InvalidRange,
             "/children/0/max"));
    add(make("slider min above max", child(R"({"type":"slider","id":"s","min":5,"max":@@1})"), ErrorCode::InvalidRange,
             "/children/0/max"));
    add(make("zero slider step", child(R"({"type":"slider","id":"s","min":0,"max":1,"step":@@0})"), ErrorCode::InvalidStep,
             "/children/0/step"));
    add(make("negative slider step", child(R"({"type":"slider","id":"s","min":0,"max":1,"step":@@-0.1})"),
             ErrorCode::InvalidStep, "/children/0/step"));
    add(make("slider step above range", child(R"({"type":"slider","id":"s","min":0,"max":1,"step":@@5})"),
             ErrorCode::InvalidStep, "/children/0/step"));
    add(make("default step above range", child(R"({"type":"slider","id":"s","min":0,"max":@@0.5})"), ErrorCode::InvalidStep,
             "/children/0/max"));
    add(make("slider step too fine for the value precision",
             child(R"({"type":"slider","id":"s","min":-117.285714,"max":88.4,"step":@@0.00001,"value":-44})"),
             ErrorCode::InvalidStep, "/children/0/step"));
    add(make("slider range overflows float",
             child(R"({"type":"slider","id":"s","min":-3e38,"max":@@3e38,"step":1e30,"value":1e38})"),
             ErrorCode::InvalidRange, "/children/0/max"));
    add(make("float overflow is still a number range error", child(R"({"type":"panel","id":"p","style":{"gap":@@1e39}})"),
             ErrorCode::NumberRange, "/children/0/style/gap"));

    add(make("slider value too high", child(R"({"type":"slider","id":"s","min":0,"max":10,"value":@@11})"),
             ErrorCode::ValueOutOfRange, "/children/0/value"));
    add(make("slider value too low", child(R"({"type":"slider","id":"s","min":0,"max":10,"value":@@-1})"),
             ErrorCode::ValueOutOfRange, "/children/0/value"));
    add(make("stepper index out of range", child(R"({"type":"stepper","id":"p","options":["a","b"],"index":@@2})"),
             ErrorCode::ValueOutOfRange, "/children/0/index"));
    add(make("text input value too long", child(R"({"type":"textInput","id":"i","maxBytes":3,"value":@@"toolong"})"),
             ErrorCode::ValueOutOfRange, "/children/0/value"));
    add(make("multi byte value counts bytes", child("{\"type\":\"textInput\",\"id\":\"i\",\"maxBytes\":3,\"value\":@@\"\xC3\xA9\xC3\xA9\"}"),
             ErrorCode::ValueOutOfRange, "/children/0/value"));

    add(make("children on text", child(R"({"type":"text","id":"t","children":@@[]})"), ErrorCode::ChildrenNotAllowed,
             "/children/0/children"));
    add(make("children on slider", child(R"({"type":"slider","id":"s","min":0,"max":1,"children":@@[{"type":"panel","id":"c"}]})"),
             ErrorCode::ChildrenNotAllowed, "/children/0/children"));
    return cases;
}

} // namespace

namespace uirepo::declarative::testing {

void runErrorTable() {
    const std::vector<Case> cases = buildCases();
    std::set<std::uint16_t> reached;
    for (const Case& entry : cases) {
        const ParseResult result = parseDocument(entry.text, entry.limits);
        require(!result.ok() && !result.diagnostics.empty(), entry.label + ": malformed input was accepted");
        const Diagnostic& first = result.diagnostics.front();
        const std::string got = std::string(errorCodeName(first.code)) + " " + first.path + " " +
                                std::to_string(first.line) + ":" + std::to_string(first.column) + " (" + first.message + ")";
        const std::string want = std::string(errorCodeName(entry.code)) + " " + entry.path + " " +
                                 std::to_string(entry.line) + ":" + std::to_string(entry.column);
        require(first.code == entry.code && first.path == entry.path && first.line == entry.line &&
                    first.column == entry.column,
                entry.label + ": expected " + want + " but got " + got);
        reached.insert(static_cast<std::uint16_t>(first.code));
    }
    for (std::uint16_t code = 1; code <= static_cast<std::uint16_t>(ErrorCode::ChildrenNotAllowed); ++code) {
        require(reached.count(code) == 1, "error code " + std::to_string(code) + " is never reached by the table");
        require(errorCodeName(static_cast<ErrorCode>(code)) != "unknown", "error code has no name");
    }
    std::cout << cases.size() << " malformed-input cases reach all " << reached.size() << " error codes\n";
}

} // namespace uirepo::declarative::testing
