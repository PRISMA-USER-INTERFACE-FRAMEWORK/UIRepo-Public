#pragma once

#include "uirepo/Core.h"

namespace uirepo::layout_engine {
void perform(Node& root, Size viewport, const TextMeasurer& textMeasurer);
}
