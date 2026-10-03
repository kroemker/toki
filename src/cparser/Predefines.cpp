#include "Predefines.h"

#include "cparser/Types.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace cparser {

namespace {

[[nodiscard]] std::optional<std::string_view> standardVersion(std::string_view standard) {
    if (standard.starts_with("gnu")) {
        standard.remove_prefix(3);
    } else if (standard.starts_with('c')) {
        standard.remove_prefix(1);
    } else {
        return std::nullopt;
    }

    if (standard == "99" || standard == "9x") {
        return "199901L";
    }
    if (standard == "11" || standard == "1x") {
        return "201112L";
    }
    if (standard == "17" || standard == "18") {
        return "201710L";
    }
    if (standard == "23" || standard == "2x") {
        return "202311L";
    }
    return std::nullopt;
}

void appendDefine(std::string& source, std::string_view definition) {
    const usize equals = definition.find('=');
    source += "#define ";
    if (equals == std::string_view::npos) {
        source += definition;
        source += " 1\n";
        return;
    }
    source += definition.substr(0, equals);
    source += ' ';
    source += definition.substr(equals + 1);
    source += '\n';
}

} // namespace

std::string predefineSource(std::span<const std::string> arguments) {
    std::string source;
    for (const std::string_view argument : arguments) {
        if (argument.starts_with("-D") && argument.size() > 2) {
            appendDefine(source, argument.substr(2));
        } else if (argument.starts_with("-U") && argument.size() > 2) {
            source += "#undef ";
            source += argument.substr(2);
            source += '\n';
        } else if (argument.starts_with("-std=")) {
            if (const std::optional<std::string_view> version =
                    standardVersion(argument.substr(5))) {
                source += "#define __STDC_VERSION__ ";
                source += *version;
                source += '\n';
            }
        }
    }
    return source;
}

} // namespace cparser
