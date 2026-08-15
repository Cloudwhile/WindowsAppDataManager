#pragma once

#include "../../models/ApplicationInfo.h"

namespace wam::core {

[[nodiscard]] RiskLevel applicationRisk(const ApplicationInfo &application);

} // namespace wam::core
