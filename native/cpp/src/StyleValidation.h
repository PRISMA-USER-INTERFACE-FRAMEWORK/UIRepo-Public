#pragma once

#include "uirepo/Core.h"

namespace uirepo::style_validation {
void validateOpacity(float value);
void validateCornerRadius(float value);
void validate(const Style& style);
void validateTree(const Node& root);
}
