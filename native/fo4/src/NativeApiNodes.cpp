#include "DocumentRegistry.h"
#include "NativeApiConvert.h"
#include "NativeApiEntries.h"

#include "uirepo/NativeApi.h"

#include "ApiResult.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace uirepo::fo4::native_api {

constexpr std::uint32_t kMaxStepperOptions = 4096;

[[nodiscard]] UIRepoNodeHandle apiCreatePanel(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id) noexcept {
    if (id == nullptr || *id == '\0') {
        return 0;
    }
    try {
        return DocumentRegistry::instance().createPanel(document, parent, id);
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] UIRepoNodeHandle apiCreateStack(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    std::uint32_t axisValue) noexcept {
    if (id == nullptr || *id == '\0') {
        return 0;
    }
    Axis axis{};
    if (!makeAxis(axisValue, axis)) {
        return 0;
    }
    try {
        return DocumentRegistry::instance().createStack(document, parent, id, axis);
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] UIRepoNodeHandle apiCreateText(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* text) noexcept {
    if (id == nullptr || *id == '\0' || text == nullptr) {
        return 0;
    }
    try {
        return DocumentRegistry::instance().createText(document, parent, id, text);
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] UIRepoNodeHandle apiCreateButton(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* text) noexcept {
    if (id == nullptr || *id == '\0' || text == nullptr) {
        return 0;
    }
    try {
        return DocumentRegistry::instance().createButton(document, parent, id, text);
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] UIRepoNodeHandle apiCreateToggle(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    std::uint8_t value) noexcept {
    if (id == nullptr || *id == '\0') {
        return 0;
    }
    try {
        return DocumentRegistry::instance().createToggle(document, parent, id, value != 0);
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] UIRepoNodeHandle apiCreateSlider(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    float value,
    float minimum,
    float maximum,
    float step) noexcept {
    if (id == nullptr || *id == '\0' ||
        !std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum) ||
        !std::isfinite(step) || maximum <= minimum || step <= 0.0f) {
        return 0;
    }
    try {
        return DocumentRegistry::instance().createSlider(
            document, parent, id, value, minimum, maximum, step);
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] UIRepoNodeHandle apiCreateStepper(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* const* options,
    std::uint32_t optionCount,
    std::uint32_t index) noexcept {
    if (id == nullptr || *id == '\0' || optionCount > kMaxStepperOptions ||
        (optionCount != 0 && options == nullptr)) {
        return 0;
    }

    try {
        std::vector<std::string> ownedOptions;
        ownedOptions.reserve(optionCount);
        for (std::uint32_t optionIndex = 0; optionIndex < optionCount; ++optionIndex) {
            if (options[optionIndex] == nullptr) {
                return 0;
            }
            ownedOptions.emplace_back(options[optionIndex]);
        }
        return DocumentRegistry::instance().createStepper(
            document,
            parent,
            id,
            std::move(ownedOptions),
            static_cast<std::size_t>(index));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] UIRepoNodeHandle apiCreateTextInput(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* value,
    std::uint32_t maxBytes) noexcept {
    if (id == nullptr || *id == '\0' || value == nullptr || maxBytes == 0) {
        return 0;
    }
    const std::size_t length = std::strlen(value);
    if (length > static_cast<std::size_t>(maxBytes)) {
        return 0;
    }
    try {
        return DocumentRegistry::instance().createTextInput(
            document,
            parent,
            id,
            value,
            static_cast<std::size_t>(maxBytes));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] UIRepoNodeHandle apiCreateImage(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* source,
    float naturalWidth,
    float naturalHeight) noexcept {
    if (id == nullptr || *id == '\0' || source == nullptr ||
        !finiteNonNegative(naturalWidth) || !finiteNonNegative(naturalHeight)) {
        return 0;
    }
    try {
        return DocumentRegistry::instance().createImage(
            document, parent, id, source, Size{naturalWidth, naturalHeight});
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] UIRepoNodeHandle apiCreateVideo(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* source,
    float naturalWidth,
    float naturalHeight) noexcept {
    if (id == nullptr || *id == '\0' || source == nullptr ||
        !finiteNonNegative(naturalWidth) || !finiteNonNegative(naturalHeight)) {
        return 0;
    }
    try {
        return DocumentRegistry::instance().createVideo(
            document, parent, id, source, Size{naturalWidth, naturalHeight});
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetNodeVisible(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint8_t visible) noexcept {
    try {
        return result(DocumentRegistry::instance().setNodeVisible(document, node, visible != 0));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetNodeEnabled(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint8_t enabled) noexcept {
    try {
        return result(DocumentRegistry::instance().setNodeEnabled(document, node, enabled != 0));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetNodeStyle(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const UIRepoStyleV1* source) noexcept {
    Style style{};
    if (!makeStyle(source, style)) {
        return 0;
    }
    try {
        return result(DocumentRegistry::instance().setNodeStyle(document, node, style));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetStackScrollable(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint8_t scrollable) noexcept {
    try {
        return result(DocumentRegistry::instance().setStackScrollable(document, node, scrollable != 0));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetToggleValue(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint8_t value) noexcept {
    try {
        return result(DocumentRegistry::instance().setToggleValue(document, node, value != 0));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiGetToggleValue(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint8_t* value) noexcept {
    if (value == nullptr) {
        return 0;
    }
    try {
        bool current{};
        if (!DocumentRegistry::instance().getToggleValue(document, node, current)) {
            return 0;
        }
        *value = current ? 1u : 0u;
        return 1u;
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetSliderValue(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    float value) noexcept {
    if (!std::isfinite(value)) {
        return 0;
    }
    try {
        return result(DocumentRegistry::instance().setSliderValue(document, node, value));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiGetSliderValue(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    float* value) noexcept {
    if (value == nullptr) {
        return 0;
    }
    try {
        return result(DocumentRegistry::instance().getSliderValue(document, node, *value));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetStepperIndex(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint32_t index) noexcept {
    try {
        return result(DocumentRegistry::instance().setStepperIndex(
            document, node, static_cast<std::size_t>(index)));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiGetStepperIndex(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint32_t* index) noexcept {
    if (index == nullptr) {
        return 0;
    }
    try {
        std::size_t current{};
        if (!DocumentRegistry::instance().getStepperIndex(document, node, current) ||
            current > static_cast<std::size_t>(UINT32_MAX)) {
            return 0;
        }
        *index = static_cast<std::uint32_t>(current);
        return 1u;
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetTextInputValue(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const char* value) noexcept {
    if (value == nullptr) {
        return 0;
    }
    try {
        return result(DocumentRegistry::instance().setTextInputValue(document, node, value));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiGetTextInputValue(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    char* buffer,
    std::uint32_t capacity,
    std::uint32_t* required) noexcept {
    if (required == nullptr) {
        return 0;
    }

    try {
        std::string value;
        if (!DocumentRegistry::instance().getTextInputValue(document, node, value)) {
            return 0;
        }
        if (value.size() >= static_cast<std::size_t>(UINT32_MAX)) {
            return 0;
        }

        const std::uint32_t needed = static_cast<std::uint32_t>(value.size() + 1);
        *required = needed;
        if (buffer == nullptr) {
            return capacity == 0 ? 1u : 0u;
        }
        if (capacity < needed) {
            return 0;
        }

        std::memcpy(buffer, value.c_str(), needed);
        return 1u;
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetText(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const char* text) noexcept {
    if (text == nullptr) {
        return 0;
    }
    try {
        return result(DocumentRegistry::instance().setText(document, node, text));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetTextFontSize(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    float fontSize) noexcept {
    if (!std::isfinite(fontSize) || fontSize <= 0.0f) {
        return 0;
    }
    try {
        return result(DocumentRegistry::instance().setTextFontSize(document, node, fontSize));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetTextColor(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    UIRepoColorV1 color) noexcept {
    try {
        return result(DocumentRegistry::instance().setTextColor(document, node, makeColor(color)));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetTextLayout(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const UIRepoTextLayoutV1* source) noexcept {
    TextLayoutOptions layout{};
    if (!makeTextLayout(source, layout)) {
        return 0;
    }
    try {
        return result(DocumentRegistry::instance().setTextLayout(document, node, layout));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetMediaSource(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const char* source) noexcept {
    if (source == nullptr) {
        return 0;
    }
    try {
        return result(DocumentRegistry::instance().setMediaSource(document, node, source));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetVideoPlayback(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint32_t stateValue,
    std::uint8_t loop,
    float volume) noexcept {
    VideoPlaybackState state{};
    if (!makePlayback(stateValue, state) ||
        !std::isfinite(volume) || volume < 0.0f || volume > 1.0f) {
        return 0;
    }
    try {
        return result(DocumentRegistry::instance().setVideoPlayback(
            document, node, state, loop != 0, volume));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

} // namespace uirepo::fo4::native_api
