#pragma once

// Internal helpers shared by the XML (.xlsx) and HTML readers. Not installed.

#include <string>
#include <string_view>

namespace egxq::detail {

/// Decode XML/HTML character references: &amp; &lt; &gt; &quot; &apos; &nbsp;
/// (as a plain space) and numeric &#NN; / &#xHH; (UTF-8 encoded). Unknown
/// entities are kept as written.
std::string decode_entities(std::string_view s);

/// Value of attribute `name` inside an opening tag's text, or "" if absent.
/// Matches whole attribute names only ("r" does not match "xr:uid"), with
/// single or double quotes. The value is entity-decoded.
std::string tag_attr(std::string_view tag, std::string_view name);

}  // namespace egxq::detail
