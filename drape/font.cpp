#include "drape/font.hpp"
#include "drape/font_constants.hpp"

#include "3party/sdf_image/sdf_image.h"

#include <ft2build.h>
#include FT_TYPES_H
#include FT_STROKER_H

#undef __FTERRORS_H__
#define FT_ERRORDEF(e, v, s) {e, s},
#define FT_ERROR_START_LIST  {
#define FT_ERROR_END_LIST    {0, 0}};
struct FreetypeError
{
  int m_code;
  char const * const m_message;
};

FreetypeError constexpr g_FT_Errors[] =
#include FT_ERRORS_H

#ifdef DEBUG
#define FREETYPE_CHECK(x) \
    do \
    { \
      FT_Error const err = (x); \
      if (err) \
        LOG(LERROR, ("Freetype:", g_FT_Errors[err].m_code, g_FT_Errors[err].m_message)); \
    } while (false)
#else
#define FREETYPE_CHECK(x) x
#endif


namespace dp
{
Font::Font(uint32_t sdfScale, ReaderPtr<Reader> fontReader, FT_Library lib)
  : m_fontReader(std::move(fontReader)), m_fontFace(nullptr), m_sdfScale(sdfScale)
{
  std::memset(&m_stream, 0, sizeof(m_stream));
  m_stream.size = static_cast<unsigned long>(m_fontReader.Size());
  m_stream.descriptor.pointer = &m_fontReader;
  m_stream.read = &Font::Read;
  m_stream.close = &Font::Close;

  FT_Open_Args args;
  std::memset(&args, 0, sizeof(args));
  args.flags = FT_OPEN_STREAM;
  args.stream = &m_stream;

  FT_Error const err = FT_Open_Face(lib, &args, 0, &m_fontFace);
  if (err || !IsValid())
    MYTHROW(InvalidFontException, (g_FT_Errors[err].m_code, g_FT_Errors[err].m_message));
}

Font::~Font()
{
  ASSERT(m_fontFace, ());
  FREETYPE_CHECK(FT_Done_Face(m_fontFace));
  m_fontFace = nullptr;
}

bool Font::IsValid() const { return m_fontFace && m_fontFace->num_glyphs > 0; }

bool Font::HasGlyph(strings::UniChar unicodePoint) const { return FT_Get_Char_Index(m_fontFace, unicodePoint) != 0; }

Glyph Font::GetGlyph(strings::UniChar unicodePoint, uint32_t baseHeight, bool isSdf) const
{
  uint32_t const glyphHeight = isSdf ? baseHeight * m_sdfScale : baseHeight;

  FREETYPE_CHECK(FT_Set_Pixel_Sizes(m_fontFace, glyphHeight, glyphHeight));
  FREETYPE_CHECK(FT_Load_Glyph(m_fontFace, FT_Get_Char_Index(m_fontFace, unicodePoint), FT_LOAD_RENDER));

  FT_Glyph glyph;
  FREETYPE_CHECK(FT_Get_Glyph(m_fontFace->glyph, &glyph));

  FT_BBox bbox;
  FT_Glyph_Get_CBox(glyph, FT_GLYPH_BBOX_PIXELS, &bbox);

  FT_Bitmap const bitmap = m_fontFace->glyph->bitmap;

  float const scale = isSdf ? 1.0f / m_sdfScale : 1.0f;

  SharedBufferManager::shared_buffer_ptr_t data;
  uint32_t imageWidth = bitmap.width;
  uint32_t imageHeight = bitmap.rows;
  if (bitmap.buffer != nullptr)
  {
    if (isSdf)
    {
      sdf_image::SdfImage const img(bitmap.rows, bitmap.pitch, bitmap.buffer, m_sdfScale * kSdfBorder);
      imageWidth = std::round(img.GetWidth() * scale);
      imageHeight = std::round(img.GetHeight() * scale);

      data = SharedBufferManager::instance().reserveSharedBuffer(bitmap.rows * bitmap.pitch);
      std::memcpy(data->data(), bitmap.buffer, data->size());
    }
    else
    {
      imageHeight += 2 * kSdfBorder;
      imageWidth += 2 * kSdfBorder;

      data = SharedBufferManager::instance().reserveSharedBuffer(imageWidth * imageHeight);
      auto ptr = data->data();
      std::memset(ptr, 0, data->size());

      for (size_t row = kSdfBorder; row < bitmap.rows + kSdfBorder; ++row)
      {
        size_t const dstBaseIndex = row * imageWidth + kSdfBorder;
        size_t const srcBaseIndex = (row - kSdfBorder) * bitmap.pitch;
        for (int column = 0; column < bitmap.pitch; ++column)
          ptr[dstBaseIndex + column] = bitmap.buffer[srcBaseIndex + column];
      }
    }
  }

  Glyph result;
  result.m_image = GlyphImage{imageWidth, imageHeight, bitmap.rows, bitmap.pitch, data};

  result.m_metrics = GlyphMetrics{(glyph->advance.x >> 16) * scale, (glyph->advance.y >> 16) * scale,
                                                bbox.xMin * scale, bbox.yMin * scale, true};

  result.m_code = unicodePoint;
  result.m_fixedSize = isSdf ? kDynamicGlyphSize : static_cast<int>(baseHeight);
  FT_Done_Glyph(glyph);

  return result;
}

void Font::GetCharcodes(std::vector<FT_ULong> & charcodes)
{
  FT_UInt gindex;
  charcodes.push_back(FT_Get_First_Char(m_fontFace, &gindex));
  while (gindex)
    charcodes.push_back(FT_Get_Next_Char(m_fontFace, charcodes.back(), &gindex));

  std::sort(charcodes.begin(), charcodes.end());
  charcodes.erase(std::unique(charcodes.begin(), charcodes.end()), charcodes.end());
}

// static
unsigned long Font::Read(FT_Stream stream, unsigned long offset, unsigned char * buffer, unsigned long count)
{
  if (count != 0)
  {
    auto * reader = reinterpret_cast<ReaderPtr<Reader> *>(stream->descriptor.pointer);
    reader->Read(offset, buffer, count);
  }

  return count;
}

// static
void Font::Close(FT_Stream) {}

void Font::MarkGlyphReady(strings::UniChar code, int fixedHeight)
{
  m_readyGlyphs.emplace(code, fixedHeight);
}

bool Font::IsGlyphReady(strings::UniChar code, int fixedHeight) const
{
  return m_readyGlyphs.find(std::make_pair(code, fixedHeight)) != m_readyGlyphs.end();
}

std::string Font::GetName() const
{
  return std::string(m_fontFace->family_name) + ':' + m_fontFace->style_name;
}

}  // namespace dp
