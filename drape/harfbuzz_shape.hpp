#pragma once

#include "base/buffer_vector.hpp"

#include <hb.h>

struct FontParams {
  int pixelSize;
  int8_t lang;
};

struct TextRun
{
  int32_t start, end;
  hb_script_t script;
  int font;
  TextRun() = default;
  TextRun(int32_t start, int32_t end, hb_script_t script, int font) : start(start), end(end), script(script), font(font) {}
};

typedef buffer_vector<TextRun, 10> TextRuns;

TextRuns ItemizeAndShapeText(std::string_view utf8, int8_t lang, FontParams const & fontParams);
