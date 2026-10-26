#pragma once
#include <array>
#include <optional>
#include <string_view>

namespace imago {
// Only stable versions are distributed. Reject suffixes, signs, leading zeroes and overflow.
inline std::optional<std::array<unsigned,3>> ParseVersion(std::wstring_view text) {
    if(text.starts_with(L"v"))text.remove_prefix(1);
    std::array<unsigned,3> parts{};
    for(size_t i=0;i<parts.size();++i) {
        const auto end=text.find(L'.');
        const auto part=text.substr(0,end);
        if(part.empty() || part.size()>5 || (part.size()>1 && part.front()==L'0'))return {};
        unsigned value{};
        for(wchar_t c:part){if(c<L'0' || c>L'9')return {};value=value*10+static_cast<unsigned>(c-L'0');}
        if(value>65535)return {};
        parts[i]=value;
        if(i==2){if(end!=std::wstring_view::npos)return {};}
        else {if(end==std::wstring_view::npos)return {};text.remove_prefix(end+1);}
    }
    return parts;
}
}
