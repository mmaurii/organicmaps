#include "testing/testing.hpp"

#include "drape/bidi.hpp"
#include "drape/drape_tests/img.hpp"
#include "drape/glyph_manager.hpp"

#include "platform/platform.hpp"

#include <QtGui/QPainter>

#include "qt_tstfrm/test_main_loop.hpp"

#include "std/target_os.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <vector>


#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>

#include <hb-ft.h>

namespace glyph_mng_tests
{
class GlyphRenderer
{
  strings::UniString m_toDraw;
  std::string m_utf8;

public:
  GlyphRenderer()
  {
    dp::GlyphManager::Params args;
    args.m_uniBlocks = "unicode_blocks.txt";
    args.m_whitelist = "fonts_whitelist.txt";
    args.m_blacklist = "fonts_blacklist.txt";
    GetPlatform().GetFontNames(args.m_fonts);

    m_mng = std::make_unique<dp::GlyphManager>(args);
  }

  void SetString(std::string const & s)
  {
    m_toDraw = bidi::log2vis(strings::MakeUniString(s));
    m_utf8 = s;
  }

  void RenderGlyphs(QPaintDevice * device)
  {
    QPainter painter(device);
    painter.fillRect(QRectF(0.0, 0.0, device->width(), device->height()), Qt::white);
    float constexpr ratio = 1.0;

    FT_Library library;

    // Initialize FreeType
    if (FT_Init_FreeType(&library)) {
      std::cerr << "Can't initialize FreeType\n";
      return;
    }

    printf("HB code\n\n");

    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, m_utf8.c_str(), m_utf8.size(), 0, m_utf8.size());
    //hb_buffer_add_codepoints(buf, m_toDraw.data(), m_toDraw.size(), 0, m_toDraw.size());
    // If you know the direction, script, and language
    hb_buffer_set_direction(buf, HB_DIRECTION_RTL);
    hb_buffer_set_script(buf, HB_SCRIPT_ARABIC);
    hb_buffer_set_language(buf, hb_language_from_string("ar", -1));

    // If you don't know the direction, script, and language
//    hb_buffer_guess_segment_properties(buf);

    char const * fontFile = "/Users/alex/Developer/omim/omim/data/00_NotoNaskhArabic-Regular.ttf";

    // Create a face and a font from a font file.
//    hb_blob_t *blob = hb_blob_create_from_file(fontFile); /* or hb_blob_create_from_file_or_fail() */
//    if (blob == hb_blob_get_empty()) {
//      printf("hb_blob_create_from_file failed\n");
//      return;
//    }
//    hb_face_t *face = hb_face_create(blob, 0);
//    if (face == hb_face_get_empty()) {
//      printf("hb_face_create failed\n");
//      return;
//    }
//    hb_font_t *font = hb_font_create(face);
//    if (font == hb_font_get_empty()) {
//      printf("hb_font_create failed\n");
//      return;
//    }

    // FreeType font face handle
    FT_Face face;

    // Load font
    if (FT_New_Face(library, fontFile, 0, &face)) {
      std::cerr << "Can't load font " << fontFile << '\n';
      return;
    }

    long constexpr kFontSize = 40;

    // Set character size
//    if (FT_Set_Char_Size(face, kFontSize << 6, kFontSize << 6, 0, 0)) {
//      std::cerr << "Can't set character size\n";
//      return;
//    }
    FT_Set_Pixel_Sizes(
        face,   /* handle to face object */
        0,      /* pixel_width           */
        kFontSize );   /* pixel_height          */


    // Set no transform (identity)
    //FT_Set_Transform(face, nullptr, nullptr);

    // Load font into HarfBuzz
    hb_font_t *font = hb_ft_font_create(face, nullptr);

    // Shape!
    hb_shape(font, buf, nullptr, 0);

    // Get the glyph and position information.
    unsigned int glyph_count;
    hb_glyph_info_t *glyph_info    = hb_buffer_get_glyph_infos(buf, &glyph_count);
    hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

    // Iterate over each glyph.
//    hb_position_t cursor_x = 0;
//    hb_position_t cursor_y = 0;

    QPoint hbPen(10, 100);

    for (unsigned int i = 0; i < glyph_count; i++) {
      hb_codepoint_t const glyphid = glyph_info[i].codepoint;

      printf("Glyph ID: %X\n", glyphid);

      FT_Int32 const flags =  FT_LOAD_RENDER;
      FT_Load_Glyph(face, glyphid, flags);

      FT_GlyphSlot slot = face->glyph;
//      FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);

      FT_Bitmap const ftBitmap = slot->bitmap;

      auto buffer = ftBitmap.buffer;
      auto width = ftBitmap.width;
      auto height = ftBitmap.rows;
      auto bearing_x = slot->metrics.horiBearingX;//slot->bitmap_left;
      auto bearing_y = slot->metrics.horiBearingY;//slot->bitmap_top;

      hb_position_t const x_offset  = (glyph_pos[i].x_offset + bearing_x) >> 6;
      hb_position_t const y_offset  = (glyph_pos[i].y_offset + bearing_y) >> 6;
      hb_position_t const x_advance = glyph_pos[i].x_advance >> 6;
      hb_position_t const y_advance = glyph_pos[i].y_advance >> 6;

      QPoint currentPen = hbPen;
      currentPen.rx() += x_offset * ratio;
      currentPen.ry() -= y_offset * ratio;
      painter.drawImage(currentPen, CreateImage(width, height, buffer),
                        QRect(0, 0, width, height));
      hbPen.rx() += x_advance * ratio;
      hbPen.ry() += y_advance * ratio;


      //      std::printf("%X xoff: %d, yoff: %d, xadv: %d, yadv: %d\n", glyphid, x_offset, y_offset, x_advance, y_advance);
      //      std::printf("cursorx: %d, cursory: %d\n", cursor_x, cursor_y);

//      cursor_x += x_advance;
//      cursor_y += y_advance;
    }

    // Tidy up.
    hb_buffer_destroy(buf);
    hb_font_destroy(font);
    // Destroy FreeType font
    FT_Done_Face(face);
    // Destroy FreeType
    FT_Done_FreeType(library);

    //hb_face_destroy(face);
    //hb_blob_destroy(blob);


    //////////////////////////////////////////////////////////////
    printf("Old drape code\n\n");

    std::vector<dp::Glyph> glyphs;
    auto generateGlyph = [this, &glyphs](strings::UniChar c)
    {
      dp::Glyph g = m_mng->GetGlyph(c, kFontSize);
      glyphs.push_back(dp::GlyphManager::GenerateGlyph(g, m_mng->GetSdfScale()));
      g.m_image.Destroy();
    };

    for (auto const & ucp : m_toDraw)
      generateGlyph(ucp);

    QPoint pen(10, 200);
    //float const ratio = 2.0;
    for (auto & g : glyphs)
    {
      if (!g.m_image.m_data)
        continue;

      printf("%X\n", g.m_code);

      uint8_t * d = SharedBufferManager::GetRawPointer(g.m_image.m_data);

      QPoint currentPen = pen;
      currentPen.rx() += g.m_metrics.m_xOffset * ratio;
      currentPen.ry() -= g.m_metrics.m_yOffset * ratio;
      painter.drawImage(currentPen, CreateImage(g.m_image.m_width, g.m_image.m_height, d),
                        QRect(0, 0, g.m_image.m_width, g.m_image.m_height));
      pen.rx() += g.m_metrics.m_xAdvance * ratio;
      pen.ry() += g.m_metrics.m_yAdvance * ratio;

      g.m_image.Destroy();
    }
  }

private:
  std::unique_ptr<dp::GlyphManager> m_mng;
};

UNIT_TEST(GlyphLoadingTest)
{
  // This unit test creates window so can't be run in GUI-less Linux machine.
#ifndef OMIM_OS_LINUX
  GlyphRenderer renderer;

  using namespace std::placeholders;

//  renderer.SetString("ØŒÆ");
//  RunTestLoop("Test1", std::bind(&GlyphRenderer::RenderGlyphs, &renderer, _1));

  //renderer.SetString("الحلّة گلها");
  renderer.SetString("الحلّة گلها"" كسول الزنجبيل القط"" اَلْعَرَبِيَّةُ");
  RunTestLoop("Test2", std::bind(&GlyphRenderer::RenderGlyphs, &renderer, _1));

//  renderer.SetString("گُلها");
//  RunTestLoop("Test3", std::bind(&GlyphRenderer::RenderGlyphs, &renderer, _1));
//
//  renderer.SetString("മനക്കലപ്പടി");
//  RunTestLoop("Test4", std::bind(&GlyphRenderer::RenderGlyphs, &renderer, _1));
#endif
}

}  // namespace glyph_mng_tests
