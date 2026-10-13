#pragma once

#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <string>

namespace boss {

// chibi-scheme's sexp_read is a strict R7RS reader: it accepts \xXXXX; for Unicode escapes
// but not JSON/JavaScript-style \uXXXX. JSON clients always emit \uXXXX, so translate them
// before the expression string reaches sexp_read. The translation is purely syntactic
// (\uXXXX -> \xXXXX;) and chibi performs the actual UTF-8 encoding from there.
//
// JSON spells a character outside the BMP as a UTF-16 surrogate pair (U+1F600 is
// \uD83D\uDE00). chibi would encode two separate \x escapes as two surrogate code points,
// which is invalid UTF-8, so a valid pair is combined into one \x escape. An unpaired
// surrogate has no Unicode scalar value either, and chibi would read its \u as a plain u, so
// an unpaired surrogate becomes U+FFFD REPLACEMENT CHARACTER, which is also what
// JavaScript's TextEncoder produces for one.
//
// Escape sequences are consumed whole, so a backslash that is itself escaped never starts a
// \uXXXX match: "\\u0041" keeps its literal backslash followed by a plain u0041.
inline std::string preprocessUnicodeEscapes(std::string input) {
  constexpr std::size_t hexDigitCount = 4;
  constexpr std::size_t escapeLength = 2 + hexDigitCount; // "\uXXXX"
  constexpr std::size_t lastHexOffset = 1 + hexDigitCount;
  constexpr std::size_t maximumScalarHexDigits = 6; // U+10FFFF
  constexpr int hexBase = 16;
  constexpr unsigned int highSurrogateFirst = 0xD800;
  constexpr unsigned int lowSurrogateFirst = 0xDC00;
  constexpr unsigned int lowSurrogateLast = 0xDFFF;
  constexpr unsigned int supplementaryPlaneFirst = 0x10000;
  constexpr unsigned int surrogatePayloadBits = 10;
  constexpr unsigned int replacementCharacter = 0xFFFD;
  auto isHex = [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; };
  auto startsUnicodeEscape = [&](std::size_t index) {
    return index + escapeLength <= input.size() && input[index] == '\\' &&
           input[index + 1] == 'u' && isHex(input[index + 2]) && isHex(input[index + 3]) &&
           isHex(input[index + 4]) && isHex(input[index + lastHexOffset]);
  };
  auto codeUnitAt = [&](std::size_t index) {
    return static_cast<unsigned int>(
        std::stoul(input.substr(index + 2, hexDigitCount), nullptr, hexBase));
  };
  auto isHighSurrogate = [&](unsigned int codeUnit) {
    return codeUnit >= highSurrogateFirst && codeUnit < lowSurrogateFirst;
  };
  auto isLowSurrogate = [&](unsigned int codeUnit) {
    return codeUnit >= lowSurrogateFirst && codeUnit <= lowSurrogateLast;
  };
  // With no backslash anywhere there is nothing to translate, so return the input unchanged
  // without building an output string. Returning the by-value parameter moves it.
  if(input.find('\\') == std::string::npos) {
    return input;
  }
  std::string output;
  output.reserve(input.size());
  auto appendSchemeEscape = [&output](unsigned int scalarValue) {
    std::array<char, maximumScalarHexDigits> digits {};
    auto const formatted =
        std::to_chars(digits.data(), digits.data() + digits.size(), scalarValue, hexBase);
    output += "\\x";
    output.append(digits.data(), formatted.ptr);
    output += ';';
  };
  for(std::size_t index = 0; index < input.size();) {
    if(startsUnicodeEscape(index)) {
      auto const codeUnit = codeUnitAt(index);
      auto const nextIndex = index + escapeLength;
      if(isHighSurrogate(codeUnit) && startsUnicodeEscape(nextIndex) &&
         isLowSurrogate(codeUnitAt(nextIndex))) {
        appendSchemeEscape(supplementaryPlaneFirst +
                           ((codeUnit - highSurrogateFirst) << surrogatePayloadBits) +
                           (codeUnitAt(nextIndex) - lowSurrogateFirst));
        index = nextIndex + escapeLength;
      } else if(isHighSurrogate(codeUnit) || isLowSurrogate(codeUnit)) {
        appendSchemeEscape(replacementCharacter);
        index = nextIndex;
      } else {
        output += "\\x";
        output.append(input, index + 2, hexDigitCount);
        output += ';';
        index = nextIndex;
      }
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

// sexp_read reads \x0; (with any number of zeros) as a NUL character.
inline bool containsNulEscape(std::string const& schemeSource) {
  for(std::size_t index = 0; index + 1 < schemeSource.size();) {
    if(schemeSource[index] != '\\') {
      ++index;
    } else if(schemeSource[index + 1] != 'x') {
      index += 2;
    } else {
      auto const digitsStart = index + 2;
      auto const digitsEnd = schemeSource.find_first_not_of("0123456789abcdefABCDEF", digitsStart);
      if(digitsEnd != std::string::npos && digitsEnd > digitsStart &&
         schemeSource[digitsEnd] == ';' &&
         schemeSource.find_first_not_of('0', digitsStart) == digitsEnd) {
        return true;
      }
      index = digitsStart;
    }
  }
  return false;
}

} // namespace boss
