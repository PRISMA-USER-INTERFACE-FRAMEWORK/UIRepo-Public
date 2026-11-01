#include "uirepo/NativeApiCpp.h"
#include "uirepo/NativeApiLoader.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

using namespace uirepo::sdk::native;

namespace {

constexpr UIRepoDocumentHandle kDocument = 0x1001;
constexpr UIRepoNodeHandle kRoot = 0x2001;
constexpr UIRepoNodeHandle kPanel = 0x2002;
constexpr UIRepoNodeHandle kText = 0x2003;
constexpr UIRepoNodeHandle kButton = 0x2004;
constexpr UIRepoNodeHandle kToggle = 0x2005;
constexpr UIRepoNodeHandle kSlider = 0x2006;
constexpr UIRepoNodeHandle kStepper = 0x2007;
constexpr UIRepoNodeHandle kTextInput = 0x2008;
constexpr UIRepoSurfaceHandle kSurface = 0x3001;

std::uint32_t gLastResult = UIREPO_RESULT_OK;
int gDestroyedDocuments = 0;
int gDestroyedSurfaces = 0;
bool gSawStyleSize = false;
bool gSawTextLayoutSize = false;
bool gSawFocusSize = false;
bool gSawPointerSize = false;
bool gSawKeySize = false;
bool gSawSurfaceDescSize = false;
bool gToggleValue = true;
float gSliderValue = 25.0f;
std::uint32_t gStepperIndex = 1;
char gTextInputValue[32] = "initial";

std::uint8_t ok() noexcept {
    gLastResult = UIREPO_RESULT_OK;
    return 1;
}

std::uint8_t rendererReady() noexcept { return ok(); }

UIRepoDocumentHandle createDocument(const char* rootId) noexcept {
    if (rootId == nullptr || std::strcmp(rootId, "fail") == 0) {
        gLastResult = UIREPO_RESULT_INVALID_ARGUMENT;
        return 0;
    }
    gLastResult = UIREPO_RESULT_OK;
    return kDocument;
}

std::uint8_t destroyDocument(UIRepoDocumentHandle document) noexcept {
    if (document != kDocument) {
        gLastResult = UIREPO_RESULT_INVALID_HANDLE;
        return 0;
    }
    ++gDestroyedDocuments;
    return ok();
}

std::uint8_t documentUnary(UIRepoDocumentHandle document) noexcept {
    if (document != kDocument) {
        gLastResult = UIREPO_RESULT_INVALID_HANDLE;
        return 0;
    }
    return ok();
}

std::uint8_t setDocumentVisible(UIRepoDocumentHandle document, std::uint8_t) noexcept {
    return documentUnary(document);
}

std::uint8_t setDocumentOrder(UIRepoDocumentHandle document, std::int32_t) noexcept {
    return documentUnary(document);
}

std::uint8_t setDocumentInputEnabled(UIRepoDocumentHandle document, std::uint8_t) noexcept {
    return documentUnary(document);
}

UIRepoNodeHandle getRootNode(UIRepoDocumentHandle document) noexcept {
    if (!documentUnary(document)) return 0;
    return kRoot;
}

UIRepoNodeHandle createPanel(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id) noexcept {
    if (document != kDocument || parent != kRoot || id == nullptr) {
        gLastResult = UIREPO_RESULT_INVALID_ARGUMENT;
        return 0;
    }
    gLastResult = UIREPO_RESULT_OK;
    return kPanel;
}

UIRepoNodeHandle createStack(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    std::uint32_t) noexcept {
    return createPanel(document, parent, id);
}

UIRepoNodeHandle createText(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* text) noexcept {
    if (document != kDocument || parent != kPanel || id == nullptr || text == nullptr) {
        gLastResult = UIREPO_RESULT_INVALID_ARGUMENT;
        return 0;
    }
    gLastResult = UIREPO_RESULT_OK;
    return kText;
}

UIRepoNodeHandle createButton(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* text) noexcept {
    if (document != kDocument || parent != kPanel || id == nullptr || text == nullptr) {
        gLastResult = UIREPO_RESULT_INVALID_ARGUMENT;
        return 0;
    }
    gLastResult = UIREPO_RESULT_OK;
    return kButton;
}

UIRepoNodeHandle createToggle(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    std::uint8_t value) noexcept {
    if (document != kDocument || parent != kPanel || id == nullptr) return 0;
    gToggleValue = value != 0;
    gLastResult = UIREPO_RESULT_OK;
    return kToggle;
}

UIRepoNodeHandle createSlider(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    float value,
    float,
    float,
    float) noexcept {
    if (document != kDocument || parent != kPanel || id == nullptr) return 0;
    gSliderValue = value;
    gLastResult = UIREPO_RESULT_OK;
    return kSlider;
}

UIRepoNodeHandle createStepper(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* const* options,
    std::uint32_t optionCount,
    std::uint32_t index) noexcept {
    if (document != kDocument || parent != kPanel || id == nullptr || options == nullptr || optionCount < 2) {
        return 0;
    }
    gStepperIndex = index;
    gLastResult = UIREPO_RESULT_OK;
    return kStepper;
}

UIRepoNodeHandle createTextInput(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* value,
    std::uint32_t maxBytes) noexcept {
    if (document != kDocument || parent != kPanel || id == nullptr || value == nullptr || maxBytes == 0) {
        return 0;
    }
    std::strncpy(gTextInputValue, value, sizeof(gTextInputValue) - 1);
    gTextInputValue[sizeof(gTextInputValue) - 1] = '\0';
    gLastResult = UIREPO_RESULT_OK;
    return kTextInput;
}

std::uint8_t setNodeStyle(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const UIRepoStyleV1* style) noexcept {
    gSawStyleSize = style != nullptr && style->size == sizeof(UIRepoStyleV1);
    if (document != kDocument || node != kPanel || !gSawStyleSize) {
        gLastResult = UIREPO_RESULT_INVALID_STRUCT_SIZE;
        return 0;
    }
    return ok();
}

std::uint8_t setTextLayout(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const UIRepoTextLayoutV1* layout) noexcept {
    gSawTextLayoutSize = layout != nullptr && layout->size == sizeof(UIRepoTextLayoutV1);
    if (document != kDocument || node != kText || !gSawTextLayoutSize) {
        gLastResult = UIREPO_RESULT_INVALID_STRUCT_SIZE;
        return 0;
    }
    return ok();
}

std::uint8_t setFocusRequirements(
    UIRepoDocumentHandle document,
    const UIRepoFocusRequirementsV1* requirements) noexcept {
    gSawFocusSize = requirements != nullptr && requirements->size == sizeof(UIRepoFocusRequirementsV1);
    if (document != kDocument || !gSawFocusSize) {
        gLastResult = UIREPO_RESULT_INVALID_STRUCT_SIZE;
        return 0;
    }
    return ok();
}

std::uint8_t routePointer(const UIRepoPointerEventV1* event) noexcept {
    gSawPointerSize = event != nullptr && event->size == sizeof(UIRepoPointerEventV1);
    gLastResult = gSawPointerSize ? UIREPO_RESULT_OK : UIREPO_RESULT_INVALID_STRUCT_SIZE;
    return gSawPointerSize ? 1 : 0;
}

std::uint8_t routeKey(const UIRepoKeyEventV1* event) noexcept {
    gSawKeySize = event != nullptr && event->size == sizeof(UIRepoKeyEventV1);
    gLastResult = gSawKeySize ? UIREPO_RESULT_OK : UIREPO_RESULT_INVALID_STRUCT_SIZE;
    return gSawKeySize ? 1 : 0;
}

std::uint8_t setToggleValue(UIRepoDocumentHandle document, UIRepoNodeHandle node, std::uint8_t value) noexcept {
    if (document != kDocument || node != kToggle) return 0;
    gToggleValue = value != 0;
    return ok();
}

std::uint8_t getToggleValue(UIRepoDocumentHandle document, UIRepoNodeHandle node, std::uint8_t* value) noexcept {
    if (document != kDocument || node != kToggle || value == nullptr) return 0;
    *value = gToggleValue ? 1u : 0u;
    return ok();
}

std::uint8_t setSliderValue(UIRepoDocumentHandle document, UIRepoNodeHandle node, float value) noexcept {
    if (document != kDocument || node != kSlider) return 0;
    gSliderValue = value;
    return ok();
}

std::uint8_t getSliderValue(UIRepoDocumentHandle document, UIRepoNodeHandle node, float* value) noexcept {
    if (document != kDocument || node != kSlider || value == nullptr) return 0;
    *value = gSliderValue;
    return ok();
}

std::uint8_t setStepperIndex(UIRepoDocumentHandle document, UIRepoNodeHandle node, std::uint32_t index) noexcept {
    if (document != kDocument || node != kStepper) return 0;
    gStepperIndex = index;
    return ok();
}

std::uint8_t getStepperIndex(UIRepoDocumentHandle document, UIRepoNodeHandle node, std::uint32_t* index) noexcept {
    if (document != kDocument || node != kStepper || index == nullptr) return 0;
    *index = gStepperIndex;
    return ok();
}

std::uint8_t setTextInputValue(UIRepoDocumentHandle document, UIRepoNodeHandle node, const char* value) noexcept {
    if (document != kDocument || node != kTextInput || value == nullptr) return 0;
    std::strncpy(gTextInputValue, value, sizeof(gTextInputValue) - 1);
    gTextInputValue[sizeof(gTextInputValue) - 1] = '\0';
    return ok();
}

std::uint8_t getTextInputValue(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    char* buffer,
    std::uint32_t capacity,
    std::uint32_t* required) noexcept {
    if (document != kDocument || node != kTextInput || required == nullptr) return 0;
    const auto needed = static_cast<std::uint32_t>(std::strlen(gTextInputValue) + 1);
    *required = needed;
    if (buffer == nullptr) return capacity == 0 ? ok() : 0;
    if (capacity < needed) return 0;
    std::memcpy(buffer, gTextInputValue, needed);
    return ok();
}

UIRepoSurfaceHandle createSurface(
    UIRepoDocumentHandle document,
    const UIRepoSurfaceDescV1* desc) noexcept {
    gSawSurfaceDescSize = desc != nullptr && desc->size == sizeof(UIRepoSurfaceDescV1);
    if (document != kDocument || !gSawSurfaceDescSize || desc->width != 320 || desc->height != 180) {
        gLastResult = UIREPO_RESULT_INVALID_ARGUMENT;
        return 0;
    }
    gLastResult = UIREPO_RESULT_OK;
    return kSurface;
}

std::uint8_t destroySurface(UIRepoSurfaceHandle surface) noexcept {
    if (surface != kSurface) {
        gLastResult = UIREPO_RESULT_INVALID_HANDLE;
        return 0;
    }
    ++gDestroyedSurfaces;
    return ok();
}

std::uint8_t resizeSurface(UIRepoSurfaceHandle surface, std::uint32_t, std::uint32_t) noexcept {
    if (surface != kSurface) return 0;
    return ok();
}

std::uint8_t renderToSurface(UIRepoDocumentHandle document, UIRepoSurfaceHandle surface) noexcept {
    if (document != kDocument || surface != kSurface) return 0;
    return ok();
}

std::uint8_t surfaceInfo(UIRepoSurfaceHandle surface, UIRepoSurfaceInfoV1* out) noexcept {
    if (surface != kSurface || out == nullptr || out->size != sizeof(UIRepoSurfaceInfoV1)) {
        gLastResult = UIREPO_RESULT_INVALID_STRUCT_SIZE;
        return 0;
    }
    out->width = 320;
    out->height = 180;
    out->generation = 7;
    out->byteSize = 320u * 180u * 4u;
    return ok();
}

std::uint8_t surfaceStats(UIRepoSurfaceStatsV1* out) noexcept {
    if (out == nullptr || out->size != sizeof(UIRepoSurfaceStatsV1)) {
        gLastResult = UIREPO_RESULT_INVALID_STRUCT_SIZE;
        return 0;
    }
    out->liveCount = 1;
    out->liveBytes = 320u * 180u * 4u;
    return ok();
}

std::uint32_t getLastResult() noexcept { return gLastResult; }

const char* describeResult(std::uint32_t code) noexcept {
    return code == UIREPO_RESULT_INVALID_ARGUMENT ? "invalid argument" : "result";
}

UIRepoNativeApiV1 makeTable() noexcept {
    UIRepoNativeApiV1 api{};
    api.size = sizeof(api);
    api.abiVersion = UIREPO_NATIVE_API_VERSION_V1;
    api.capabilities = UIREPO_CAP_RETAINED_DOCUMENTS |
                       UIREPO_CAP_POINTER_INPUT |
                       UIREPO_CAP_KEYBOARD_INPUT |
                       UIREPO_CAP_NATIVE_SETTINGS_CONTROLS |
                       UIREPO_CAP_FOCUS_LEASE |
                       UIREPO_CAP_TEXT_LAYOUT |
                       UIREPO_CAP_OFFSCREEN_SURFACES |
                       UIREPO_CAP_RESULT_CODES;
    api.isRendererReady = &rendererReady;
    api.createDocument = &createDocument;
    api.destroyDocument = &destroyDocument;
    api.setDocumentVisible = &setDocumentVisible;
    api.setDocumentOrder = &setDocumentOrder;
    api.setDocumentInputEnabled = &setDocumentInputEnabled;
    api.getRootNode = &getRootNode;
    api.createPanel = &createPanel;
    api.createStack = &createStack;
    api.createText = &createText;
    api.createButton = &createButton;
    api.setNodeStyle = &setNodeStyle;
    api.createToggle = &createToggle;
    api.createSlider = &createSlider;
    api.setToggleValue = &setToggleValue;
    api.getToggleValue = &getToggleValue;
    api.setSliderValue = &setSliderValue;
    api.getSliderValue = &getSliderValue;
    api.createStepper = &createStepper;
    api.setStepperIndex = &setStepperIndex;
    api.getStepperIndex = &getStepperIndex;
    api.createTextInput = &createTextInput;
    api.setTextInputValue = &setTextInputValue;
    api.getTextInputValue = &getTextInputValue;
    api.setTextLayout = &setTextLayout;
    api.setDocumentFocusRequirements = &setFocusRequirements;
    api.routePointerEvent = &routePointer;
    api.routeKeyEvent = &routeKey;
    api.createOffscreenSurface = &createSurface;
    api.destroyOffscreenSurface = &destroySurface;
    api.resizeOffscreenSurface = &resizeSurface;
    api.renderDocumentToSurface = &renderToSurface;
    api.getOffscreenSurfaceInfo = &surfaceInfo;
    api.getOffscreenSurfaceStats = &surfaceStats;
    api.getLastResult = &getLastResult;
    api.describeResult = &describeResult;
    return api;
}

int gEnumerated = 0;
int gDispatched = 0;

std::uint8_t getDocumentStateStub(UIRepoDocumentHandle document, UIRepoDocumentStateV1* out) noexcept {
    if (out == nullptr) {
        gLastResult = UIREPO_RESULT_INVALID_ARGUMENT;
        return 0;
    }
    if (out->size != sizeof(UIRepoDocumentStateV1)) {
        gLastResult = UIREPO_RESULT_INVALID_STRUCT_SIZE;
        return 0;
    }
    if (document != kDocument) {
        gLastResult = UIREPO_RESULT_INVALID_HANDLE;
        return 0;
    }
    out->visible = 1;
    out->health = UIREPO_DOCUMENT_HEALTH_LIVE;
    out->role = UIREPO_DOCUMENT_ROLE_PANEL;
    out->order = 9;
    out->generation = 4;
    return ok();
}

std::uint8_t enumerateDocumentsStub(UIRepoDocumentEnumerateCallback callback, void* userData) noexcept {
    if (callback == nullptr) {
        gLastResult = UIREPO_RESULT_INVALID_ARGUMENT;
        return 0;
    }
    UIRepoDocumentStateV1 state = makeDocumentState();
    state.order = 3;
    callback(kDocument, &state, userData);
    return ok();
}

std::uint8_t isGameThreadStub() noexcept {
    return 1;
}

std::uint8_t dispatchToGameThreadStub(UIRepoGameThreadCallback callback, void* userData) noexcept {
    if (callback == nullptr) {
        gLastResult = UIREPO_RESULT_INVALID_ARGUMENT;
        return 0;
    }
    callback(userData);
    return ok();
}

UIRepoNativeApiV2 makeTableV2() noexcept {
    UIRepoNativeApiV2 api{};
    api.v1 = makeTable();
    api.v1.size = sizeof(UIRepoNativeApiV2);
    api.v1.abiVersion = UIREPO_NATIVE_API_VERSION_V2;
    api.v1.capabilities |= UIREPO_CAP_DOCUMENT_STATE | UIREPO_CAP_DOCUMENT_ENUMERATION |
                           UIREPO_CAP_GAME_THREAD_DISPATCH;
    api.getDocumentState = &getDocumentStateStub;
    api.enumerateDocuments = &enumerateDocumentsStub;
    api.isGameThread = &isGameThreadStub;
    api.dispatchToGameThread = &dispatchToGameThreadStub;
    return api;
}

void countDocument(UIRepoDocumentHandle document, const UIRepoDocumentStateV1* state, void*) {
    assert(document == kDocument && state != nullptr && state->order == 3);
    ++gEnumerated;
}

void countDispatch(void* userData) {
    assert(userData == &gDispatched);
    ++gDispatched;
}

void testClientV2() {
    UIRepoNativeApiV2 table = makeTableV2();
    Client client(&table.v1);
    assert(client && client.hasV2());
    assert(client.has(UIREPO_CAP_DOCUMENT_STATE));

    const auto state = client.documentState(DocumentHandle{kDocument});
    assert(state);
    assert(state.value().size == sizeof(UIRepoDocumentStateV1));
    assert(state.value().visible == 1 && state.value().order == 9 && state.value().generation == 4);
    assert(state.value().health == UIREPO_DOCUMENT_HEALTH_LIVE);

    const auto unknown = client.documentState(DocumentHandle{0xDEAD});
    assert(!unknown && unknown.code() == UIREPO_RESULT_INVALID_HANDLE);

    assert(client.enumerateDocuments(&countDocument, nullptr));
    assert(gEnumerated == 1);
    const auto nullCallback = client.enumerateDocuments(nullptr, nullptr);
    assert(!nullCallback && nullCallback.code() == UIREPO_RESULT_INVALID_ARGUMENT);

    assert(client.isGameThread());
    assert(client.dispatchToGameThread(&countDispatch, &gDispatched));
    assert(gDispatched == 1);
    const auto nullDispatch = client.dispatchToGameThread(nullptr, nullptr);
    assert(!nullDispatch && nullDispatch.code() == UIREPO_RESULT_INVALID_ARGUMENT);

    UIRepoNativeApiV2 noBits = makeTableV2();
    noBits.v1.capabilities &= ~static_cast<std::uint64_t>(
        UIREPO_CAP_DOCUMENT_STATE | UIREPO_CAP_DOCUMENT_ENUMERATION | UIREPO_CAP_GAME_THREAD_DISPATCH);
    Client gated(&noBits.v1);
    assert(gated.documentState(DocumentHandle{kDocument}).code() == UIREPO_RESULT_UNSUPPORTED);
    assert(gated.enumerateDocuments(&countDocument, nullptr).code() == UIREPO_RESULT_UNSUPPORTED);
    assert(!gated.isGameThread());
    assert(gated.dispatchToGameThread(&countDispatch, &gDispatched).code() == UIREPO_RESULT_UNSUPPORTED);

    UIRepoNativeApiV1 v1Only = makeTable();
    v1Only.abiVersion = UIREPO_NATIVE_API_VERSION_V1;
    Client old(&v1Only);
    assert(old && !old.hasV2());
    assert(old.createDocument("still-works"));
    assert(old.documentState(DocumentHandle{kDocument}).code() == UIREPO_RESULT_UNSUPPORTED);
    assert(!old.isGameThread());

    UIRepoNativeApiV2 truncated = makeTableV2();
    truncated.v1.size = sizeof(UIRepoNativeApiV2) - sizeof(void*);
    Client cut(&truncated.v1);
    assert(cut && !cut.hasV2());
    assert(cut.dispatchToGameThread(&countDispatch, &gDispatched).code() == UIREPO_RESULT_UNSUPPORTED);
}

void testLoaderV2() {
    UIRepoNativeApiV2 table = makeTableV2();
    const auto v2 = uirepo::sdk::Api::adoptV2(&table.v1);
    assert(v2.status() == uirepo::sdk::LoadStatus::Ok);
    assert(v2.tableV2() == &table);
    assert(v2.has(UIREPO_CAP_GAME_THREAD_DISPATCH));
    assert(uirepo::sdk::Api::adopt(&table.v1).tableV2() == &table);

    UIRepoNativeApiV1 v1Only = makeTable();
    v1Only.abiVersion = UIREPO_NATIVE_API_VERSION_V1;
    assert(uirepo::sdk::Api::adopt(&v1Only).status() == uirepo::sdk::LoadStatus::Ok);
    assert(uirepo::sdk::Api::adopt(&v1Only).tableV2() == nullptr);
    assert(uirepo::sdk::Api::adoptV2(&v1Only).status() == uirepo::sdk::LoadStatus::VersionRefused);
    assert(!uirepo::sdk::Api::adoptV2(&v1Only));

    UIRepoNativeApiV2 truncated = makeTableV2();
    truncated.v1.size = sizeof(UIRepoNativeApiV2) - sizeof(void*);
    assert(uirepo::sdk::Api::adoptV2(&truncated.v1).status() == uirepo::sdk::LoadStatus::TableTooSmall);
    assert(uirepo::sdk::Api::adopt(&truncated.v1).tableV2() == nullptr);

    assert(uirepo::sdk::Api::adoptV2(nullptr).status() == uirepo::sdk::LoadStatus::VersionRefused);

    UIRepoNativeApiV2 tooShortForV1 = makeTableV2();
    tooShortForV1.v1.size = sizeof(UIRepoNativeApiV1) - sizeof(void*);
    assert(uirepo::sdk::Api::adoptV2(&tooShortForV1.v1).status() == uirepo::sdk::LoadStatus::TableTooSmall);
}

} // namespace

int main() {
    static_assert(!std::is_same_v<DocumentHandle, NodeHandle>);
    static_assert(!std::is_same_v<DocumentHandle, SurfaceHandle>);
    static_assert(!std::is_same_v<NodeHandle, SurfaceHandle>);
    static_assert(!std::is_copy_constructible_v<Document>);
    static_assert(!std::is_copy_constructible_v<Surface>);
    static_assert(std::is_nothrow_move_constructible_v<Document>);
    static_assert(std::is_nothrow_move_constructible_v<Surface>);

    assert(makeStyle().size == sizeof(UIRepoStyleV1));
    assert(makeTextLayout().size == sizeof(UIRepoTextLayoutV1));
    assert(makePointerEvent().size == sizeof(UIRepoPointerEventV1));
    assert(makeKeyEvent().size == sizeof(UIRepoKeyEventV1));
    assert(makeFocusRequirements().size == sizeof(UIRepoFocusRequirementsV1));
    assert(makeSurfaceDesc().size == sizeof(UIRepoSurfaceDescV1));
    assert(makeSurfaceInfo().size == sizeof(UIRepoSurfaceInfoV1));
    assert(makeSurfaceStats().size == sizeof(UIRepoSurfaceStatsV1));

    UIRepoNativeApiV1 table = makeTable();
    Client client(&table);
    assert(client);
    assert(client.has(UIREPO_CAP_RESULT_CODES));
    assert(client.rendererReady());

    auto failed = client.createDocument("fail");
    assert(!failed);
    assert(failed.code() == UIREPO_RESULT_INVALID_ARGUMENT);
    assert(std::strcmp(client.describe(failed.code()), "invalid argument") == 0);

    {
        auto documentResult = client.createDocument("wrapper-test");
        assert(documentResult);
        Document document = std::move(documentResult).value();
        assert(document);

        assert(client.setVisible(document.handle(), false));
        assert(client.setOrder(document.handle(), 42));
        assert(client.setInputEnabled(document.handle(), false));

        auto rootResult = client.root(document.handle());
        assert(rootResult);
        const NodeHandle root = rootResult.value();

        auto panelResult = client.createPanel(document.handle(), root, "panel");
        assert(panelResult);
        const NodeHandle panel = panelResult.value();

        auto textResult = client.createText(document.handle(), panel, "label", "hello");
        assert(textResult);
        const NodeHandle text = textResult.value();
        assert(client.createButton(document.handle(), panel, "apply", "Apply"));

        UIRepoStyleV1 style{}; // size intentionally left zero
        style.opacity = 1.0f;
        assert(client.setStyle(document.handle(), panel, style));
        assert(gSawStyleSize);

        UIRepoTextLayoutV1 layout{}; // size intentionally left zero
        assert(client.setTextLayout(document.handle(), text, layout));
        assert(gSawTextLayoutSize);

        const auto toggle = client.createToggle(document.handle(), panel, "toggle", true);
        assert(toggle);
        assert(client.setToggleValue(document.handle(), toggle.value(), false));
        auto toggleValue = client.toggleValue(document.handle(), toggle.value());
        assert(toggleValue && !toggleValue.value());

        const auto slider = client.createSlider(document.handle(), panel, "slider", 25.0f, 0.0f, 100.0f, 1.0f);
        assert(slider);
        assert(client.setSliderValue(document.handle(), slider.value(), 50.0f));
        auto sliderValue = client.sliderValue(document.handle(), slider.value());
        assert(sliderValue && sliderValue.value() == 50.0f);

        const char* options[] = {"Low", "High"};
        const auto stepper = client.createStepper(document.handle(), panel, "stepper", options, 2, 1);
        assert(stepper);
        assert(client.setStepperIndex(document.handle(), stepper.value(), 0));
        auto stepperIndex = client.stepperIndex(document.handle(), stepper.value());
        assert(stepperIndex && stepperIndex.value() == 0);

        const auto input = client.createTextInput(document.handle(), panel, "input", "initial", 31);
        assert(input);
        assert(client.setTextInputValue(document.handle(), input.value(), "updated"));
        auto needed = client.textInputValue(document.handle(), input.value(), nullptr, 0);
        assert(needed && needed.value() == 8);
        char buffer[32]{};
        auto copied = client.textInputValue(document.handle(), input.value(), buffer, sizeof(buffer));
        assert(copied && std::strcmp(buffer, "updated") == 0);

        UIRepoFocusRequirementsV1 focus{}; // size intentionally left zero
        assert(client.setFocusRequirements(document.handle(), focus));
        assert(gSawFocusSize);

        UIRepoPointerEventV1 pointer{}; // size intentionally left zero
        assert(client.routePointer(pointer));
        assert(gSawPointerSize);

        UIRepoKeyEventV1 key{}; // size intentionally left zero
        assert(client.routeKey(key));
        assert(gSawKeySize);

        auto surfaceResult = client.createSurface(document.handle(), 320, 180);
        assert(surfaceResult);
        assert(gSawSurfaceDescSize);
        Surface surface = std::move(surfaceResult).value();
        assert(surface);

        assert(client.resizeSurface(surface.handle(), 640, 360));
        assert(client.renderToSurface(document.handle(), surface.handle()));

        auto info = client.surfaceInfo(surface.handle());
        assert(info);
        assert(info.value().size == sizeof(UIRepoSurfaceInfoV1));
        assert(info.value().width == 320);
        assert(info.value().generation == 7);

        auto stats = client.surfaceStats();
        assert(stats);
        assert(stats.value().size == sizeof(UIRepoSurfaceStatsV1));
        assert(stats.value().liveCount == 1);

        Surface movedSurface = std::move(surface);
        assert(!surface);
        assert(movedSurface);
    }

    assert(gDestroyedSurfaces == 1);
    assert(gDestroyedDocuments == 1);

    {
        auto documentResult = client.createDocument("release-test");
        Document document = std::move(documentResult).value();
        const DocumentHandle released = document.release();
        assert(released.value == kDocument);
        assert(!document);
    }
    assert(gDestroyedDocuments == 1); // release transfers ownership

    UIRepoNativeApiV1 noResults = makeTable();
    noResults.capabilities &= ~static_cast<std::uint64_t>(UIREPO_CAP_RESULT_CODES);
    noResults.getLastResult = nullptr;
    Client noResultClient(&noResults);
    auto failedWithoutCodes = noResultClient.createDocument("fail");
    assert(!failedWithoutCodes);
    assert(failedWithoutCodes.code() == UIREPO_RESULT_UNKNOWN);

    Client unavailable;
    auto unsupported = unavailable.createDocument("x");
    assert(!unsupported);
    assert(unsupported.code() == UIREPO_RESULT_UNSUPPORTED);

    UIRepoNativeApiV1 shortTable = makeTable();
    shortTable.size = sizeof(UIRepoNativeApiV1) - sizeof(void*);
    assert(!Client(&shortTable));

    UIRepoNativeApiV1 oldProvider = makeTable();
    oldProvider.abiVersion = 0;
    assert(!Client(&oldProvider));

    testClientV2();
    testLoaderV2();

    return 0;
}
