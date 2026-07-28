#pragma once

#include <cctype>
#include <cstddef>
#include <string>

namespace boss {

// chibi-scheme's sexp_read is a strict R7RS reader: it accepts \xXXXX; for Unicode escapes
// but not JSON/JavaScript-style \uXXXX. JSON clients always emit \uXXXX, so translate them
// before the expression string reaches sexp_read. This covers the full BMP (U+0000-U+FFFF);
// the translation is purely syntactic (\uXXXX -> \xXXXX;) and chibi performs the actual
// UTF-8 encoding from there.
//
// Escape sequences are consumed whole, so a backslash that is itself escaped never starts a
// \uXXXX match: "\\u0041" keeps its literal backslash followed by a plain u0041.
inline std::string preprocessUnicodeEscapes(std::string input) {
  constexpr std::size_t hexDigitCount = 4;
  constexpr std::size_t escapeLength = 2 + hexDigitCount; // "\uXXXX"
  constexpr std::size_t lastHexOffset = 1 + hexDigitCount;
  auto isHex = [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; };
  auto startsUnicodeEscape = [&](std::size_t index) {
    return index + escapeLength <= input.size() && input[index] == '\\' &&
           input[index + 1] == 'u' && isHex(input[index + 2]) && isHex(input[index + 3]) &&
           isHex(input[index + 4]) && isHex(input[index + lastHexOffset]);
  };
  // With no backslash anywhere there is nothing to translate, and the output string need not
  // be built at all. (`input` is already owned by this function, so no copy is avoided here.)
  if(input.find('\\') == std::string::npos) {
    return input;
  }
  std::string output;
  output.reserve(input.size());
  for(std::size_t index = 0; index < input.size();) {
    if(startsUnicodeEscape(index)) {
      output += "\\x";
      output.append(input, index + 2, hexDigitCount);
      output += ';';
      index += escapeLength;
    } else if(input[index] == '\\' && index + 1 < input.size()) {
      // Copy every other escape whole, so the character it escapes cannot be re-read as the
      // start of a \uXXXX sequence.
      output += input[index];
      output += input[index + 1];
      index += 2;
    } else {
      output += input[index++];
    }
  }
  return output;
}

} // namespace boss
