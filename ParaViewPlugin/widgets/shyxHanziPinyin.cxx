#include "shyxHanziPinyin.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "shyxHanziPinyinTable.inc"

const char* shyxHanziPinyin(std::uint32_t codepoint)
{
  const std::uint32_t* const begin = kShyxPinyinCodepoints;
  const std::uint32_t* const end = kShyxPinyinCodepoints + kShyxPinyinCount;
  const std::uint32_t* const found = std::lower_bound(begin, end, codepoint);
  if (found == end || *found != codepoint)
  {
    return nullptr;
  }
  const std::uint16_t syllable = kShyxPinyinSyllableIndex[static_cast<std::size_t>(found - begin)];
  return kShyxPinyinSyllables + kShyxPinyinSyllableOff[syllable];
}
