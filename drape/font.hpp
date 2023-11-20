#pragma once

#include "base/exception.hpp"
#include "base/string_utils.hpp"

#include "coding/reader.hpp"

#include "drape/glyph.hpp"

#include <string>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYSTEM_H

namespace dp
{
class Font
{
public:
  DECLARE_EXCEPTION(InvalidFontException, RootException);

  Font(uint32_t sdfScale, ReaderPtr<Reader> fontReader, FT_Library lib);
  ~Font();

  bool IsValid() const;

  bool HasGlyph(strings::UniChar unicodePoint) const;
  Glyph GetGlyph(strings::UniChar unicodePoint, uint32_t baseHeight, bool isSdf) const;
  void GetCharcodes(std::vector<FT_ULong> & charcodes) const;

  static unsigned long Read(FT_Stream stream, unsigned long offset, unsigned char * buffer, unsigned long count);
  static void Close(FT_Stream);

  void MarkGlyphReady(strings::UniChar code, int fixedHeight);
  bool IsGlyphReady(strings::UniChar code, int fixedHeight) const;
  std::string GetName() const;

private:
  ReaderPtr<Reader> m_fontReader;
  FT_StreamRec_ m_stream;
  FT_Face m_fontFace;
  uint32_t m_sdfScale;

  std::set<std::pair<strings::UniChar, int>> m_readyGlyphs;
};

}  // namespace dp
