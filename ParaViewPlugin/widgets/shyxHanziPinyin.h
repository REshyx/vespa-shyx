/**
 * Single-character toneless pinyin from mozillazg/pinyin-data (see shyxHanziPinyinTable.inc).
 * Returns nullptr when the code point has no Mandarin reading in that table.
 */
#ifndef shyxHanziPinyin_h
#define shyxHanziPinyin_h

#include <cstdint>

const char* shyxHanziPinyin(std::uint32_t codepoint);

#endif
