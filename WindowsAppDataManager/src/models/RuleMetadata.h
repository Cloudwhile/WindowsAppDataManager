#pragma once

namespace wam {

// 规则来源用于区分内置规则与未来可能加载的外部规则。
// 来源本身不代表规则已经可信；自动清理还必须检查 trustLevel。
enum class RuleOrigin {
    BuiltIn,
    Community,
    Local,
    Unknown
};

enum class RuleTrustLevel {
    Verified,
    Unverified,
    Unknown
};

} // namespace wam
