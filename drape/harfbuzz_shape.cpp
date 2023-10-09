#include "base/assert.hpp"

#include <string>

#include <hb.h>
#include <unicode/uscript.h>  // UScriptCode
#include <unicode/utf16.h>  // U16_NEXT

namespace
{
// The maximum number of scripts a Unicode character can belong to. This value
// is arbitrarily chosen to be a good limit because it is unlikely for a single
// character to belong to more scripts.
constexpr size_t kMaxScripts = 32;

//UBiDiLevel GetParagraphLevelForGivenText(const std::u16string& text) {
//  const char16_t* string = text.c_str();
//  size_t const length = text.length();
//  size_t position = 0;
//  while (position < length) {
//    UChar32 character;
//    size_t next_position = position;
//    U16_NEXT(string, next_position, length, character);
//
//    int32_t const property = u_getIntPropertyValue(character, UCHAR_BIDI_CLASS);
//    switch (property) {
//    case U_RIGHT_TO_LEFT:
//    case U_RIGHT_TO_LEFT_ARABIC:
//    case U_RIGHT_TO_LEFT_EMBEDDING:
//    case U_RIGHT_TO_LEFT_OVERRIDE:
//      return 1;  // Highest RTL level.
//
//    case U_LEFT_TO_RIGHT:
//    case U_LEFT_TO_RIGHT_EMBEDDING:
//    case U_LEFT_TO_RIGHT_OVERRIDE:
//      return 0;  // Highest LTR level.
//
//    default: position = next_position;
//    }
//  }
//  return 0;  // Highest LTR level.
//}

// Writes the script and the script extensions of the Unicode |codepoint|.
// Returns the number of written scripts.
size_t GetScriptExtensions(UChar32 codepoint, UScriptCode* scripts) {
  // Fill |scripts| with the script extensions.
  UErrorCode icu_error = U_ZERO_ERROR;
  size_t const count =
      uscript_getScriptExtensions(codepoint, scripts, kMaxScripts, &icu_error);
  if (U_FAILURE(icu_error))
    return 0;

  return count;
}

// Intersects the script extensions set of |codepoint| with |result| and writes
// to |result|, reading and updating |result_size|. The output |result| will be
// a subset of the input |result| (thus |result_size| can only be smaller).
void ScriptSetIntersect(UChar32 codepoint, UScriptCode* result, size_t* result_size) {
  // Each codepoint has a Script property and a Script Extensions (Scx)
  // property.
  //
  // The implicit Script property values 'Common' and 'Inherited' indicate that
  // a codepoint is widely used in many scripts, rather than being associated
  // to a specific script.
  //
  // However, some codepoints that are assigned a value of 'Common' or
  // 'Inherited' are not commonly used with all scripts, but rather only with a
  // limited set of scripts. The Script Extension property is used to specify
  // the set of script which borrow the codepoint.
  //
  // Calls to GetScriptExtensions(...) return the set of scripts where the
  // codepoints can be used.
  // (see table 7 from http://www.unicode.org/reports/tr24/tr24-29.html)
  //
  //     Script       Script Extensions ->  Results
  //  1) Common       {Common}          ->  {Common}
  //     Inherited    {Inherited}       ->  {Inherited}
  //  2) Latin        {Latn}            ->  {Latn}
  //     Inherited    {Latn}            ->  {Latn}
  //  3) Common       {Hira Kana}       ->  {Hira Kana}
  //     Inherited    {Hira Kana}       ->  {Hira Kana}
  //  4) Devanagari   {Deva Dogr Kthi Mahj}  ->  {Deva Dogr Kthi Mahj}
  //     Myanmar      {Cakm Mymr Tale}  ->  {Cakm Mymr Tale}
  //
  // For most of the codepoints, the script extensions set contains only one
  // element. For CJK codepoints, it's common to see 3-4 scripts. For really
  // rare cases, the set can go above 20 scripts.
  UScriptCode scripts[kMaxScripts] = { USCRIPT_INVALID_CODE };
  size_t const count = GetScriptExtensions(codepoint, scripts);

  // Implicit script 'inherited' is inheriting scripts from preceding codepoint.
  if (count == 1 && scripts[0] == USCRIPT_INHERITED)
    return;

  auto const contains = [&scripts, count](UScriptCode code)
  {
    for (size_t i = 0; i < count; ++i)
      if (scripts[i] == code)
        return true;

    return false;
  };

  // Perform the intersection of both script set.
  ASSERT(!contains(USCRIPT_INHERITED), ());
  size_t out_size = 0;
  for (size_t i = 0; i < *result_size; ++i) {
    auto const current = result[i];
    if (contains(current))
      result[out_size++] = current;
  }

  *result_size = out_size;
}

// The CharIterator classes iterate through the characters in UTF8 and
// UTF16 strings.  Example usage:
//
//   for (UTF8CharIterator iter(str); !iter.end(); iter.Advance()) {
//     VLOG(1) << iter.get();
//   }
class UTF16CharIterator {
public:
  // Requires |str| to live as long as the UTF16CharIterator does.
  explicit UTF16CharIterator(std::u16string_view str) : str_(str),
        array_pos_{0},
        next_pos_{0},
        char_{0} {
    // This has the side-effect of advancing |next_pos_|.
    if (array_pos_ < str_.length())
      ReadChar();
  }

  UTF16CharIterator(UTF16CharIterator&& to_move) = default;
  UTF16CharIterator& operator=(UTF16CharIterator&& to_move) = default;

  UTF16CharIterator(const UTF16CharIterator&) = delete;
  UTF16CharIterator operator=(const UTF16CharIterator&) = delete;

  // Return the starting array index of the current character within the
  // string.
  size_t array_pos() const { return array_pos_; }

  // Returns the code point at the current position.
  int32_t get() const { return char_; }

  // Returns true if we're at the end of the string.
  bool end() const { return array_pos_ == str_.length(); }

  // Advances to the next actual character.  Returns false if we're at the
  // end of the string.
  bool Advance() {
    if (array_pos_ >= str_.length())
      return false;

    array_pos_ = next_pos_;
    if (next_pos_ < str_.length())
      ReadChar();

    return true;
  }

private:

  // Fills in the current character we found and advances to the next
  // character, updating all flags as necessary.
  void ReadChar()
  {
    // This is actually a huge macro, so is worth having in a separate function.
    U16_NEXT(str_.data(), next_pos_, str_.length(), char_);
  }
  // The string we're iterating over.
  std::u16string_view str_;
  // Array index.
  size_t array_pos_{0};
  // The next array index.
  size_t next_pos_;
  // The current character.
  int32_t char_;
};
}  // namespace

// Find the longest sequence of characters from 0 and up to |length| that have
// at least one common UScriptCode value. Writes the common script value to
// |script| and returns the length of the sequence. Takes the characters' script
// extensions into account. http://www.unicode.org/reports/tr24/#ScriptX
//
// Consider 3 characters with the script values {Kana}, {Hira, Kana}, {Kana}.
// Without script extensions only the first script in each set would be taken
// into account, resulting in 3 runs where 1 would be enough.
size_t ScriptInterval(const std::u16string& text,
                      size_t start,
                      size_t length,
                      UScriptCode* script) {
  ASSERT_GREATER(length, 0U, ());

  UScriptCode scripts[kMaxScripts] = { USCRIPT_INVALID_CODE };

  UTF16CharIterator char_iterator{std::u16string_view{text.c_str() + start, length}};
  size_t scripts_size = GetScriptExtensions(char_iterator.get(), scripts);
  *script = scripts[0];

  while (char_iterator.Advance()) {
    ScriptSetIntersect(char_iterator.get(), scripts, &scripts_size);
    if (scripts_size == 0U)
      return char_iterator.array_pos();
    *script = scripts[0];
  }

  return length;
}

struct FontParams {
  int pixelSize;
  int8_t lang;
};

struct Runs
{
  int32_t start, end;
  Font * font;
};

Runs ItemizeTextToRuns(std::u16string const & text)
{
  ASSERT(!text.empty(), ());
  auto const textLength = static_cast<int32_t>(text.length());

  // Deliberately not checking for nullptr.
  thread_local static UBiDi * bidi = ubidi_open();
  UErrorCode error = U_ZERO_ERROR;
  ubidi_setPara(bidi, text.data(), textLength, UBIDI_DEFAULT_LTR, nullptr, &error);
  if (U_FAILURE(error))
  {
    LOG(LERROR, ("ubidi_setPara failed with code", error));
    auto font = nullptr; // default font
    return {0, textLength, font};
  }

  // Iterator to split ranged styles and baselines. The color attributes don't
  // break text runs to keep ligature between graphemes (e.g. Arabic word).
  //internal::StyleIterator style = GetLayoutTextStyleIterator();

  // Split the original text by logical runs, then each logical run by common
  // script and each sequence at special characters and style boundaries. This
  // invariant holds: bidi_run_start <= script_run_start <= breaking_run_start
  // <= breaking_run_end <= script_run_end <= bidi_run_end
  for (int32_t bidi_run_start = 0; bidi_run_start < textLength;) {
    // Determine the longest logical run (e.g. same bidi direction) from this point.
    int32_t bidi_run_break = 0;
    UBiDiLevel bidi_level = 0;
    ubidi_getLogicalRun(bidi, bidi_run_start, &bidi_run_break, &bidi_level);
    int32_t const bidi_run_end = bidi_run_break;
    ASSERT_LESS(bidi_run_start, bidi_run_end, ());

    for (int32_t script_run_start = bidi_run_start; script_run_start < bidi_run_end;) {
      // Find the longest sequence of characters that have at least one common UScriptCode value.
      UScriptCode script = USCRIPT_INVALID_CODE;
      size_t const script_run_end = ScriptInterval(text, script_run_start,
                         bidi_run_end - script_run_start, &script) + script_run_start;
      ASSERT_LESS(script_run_start, script_run_end, ());

//      for (size_t breaking_run_start = script_run_start; breaking_run_start < script_run_end;) {
//        // Find the break boundary for style. The style won't break a grapheme
//        // since the style of the first character is applied to the whole
//        // grapheme.
//        style.IncrementToPosition(breaking_run_start);
//        size_t text_style_end = style.GetTextBreakingRange().end();

        // Break runs at certain characters that need to be rendered separately
        // to prevent an unusual character from forcing a fallback font on the
        // entire run. After script intersection, many codepoints end up in the
        // script COMMON but can't be rendered together.
//        size_t breaking_run_end = FindRunBreakingCharacter(
//            text, script, breaking_run_start, text_style_end, script_run_end);
//
//        DCHECK_LT(breaking_run_start, breaking_run_end);
//        DCHECK(IsValidCodePointIndex(text, breaking_run_end));

        // Set the font params for the current run for the current run break.
        internal::TextRunHarfBuzz::FontParams font_params =
            CreateFontParams(primary_font, bidi_level, script);

        // Create the current run from [breaking_run_start, breaking_run_end[.
        auto run = std::make_unique<internal::TextRunHarfBuzz>(primary_font);
        //run->range = Range(breaking_run_start, breaking_run_end);
        run->range = Range(script_run_start, script_run_end);

        // Add the created run to the set of runs.
        (*out_commonized_run_map)[font_params].push_back(run.get());
        //out_run_list->Add(std::move(run));

//        // Move to the next run.
//        breaking_run_start = breaking_run_end;
//      }

      // Move to the next script sequence.
      script_run_start = script_run_end;
    }

    // Move to the next direction sequence.
    bidi_run_start = bidi_run_end;
  }
}

// A copy of hb_icu_script_to_script to avoid direct ICU dependency.
hb_script_t ICUScriptToHarfbuzzScript(UScriptCode script) {
    if (script == USCRIPT_INVALID_CODE)
      return HB_SCRIPT_INVALID;
    return hb_script_from_string(uscript_getShortName (script), -1);
}

hb_language_t OrganicMapsLanguageToHarfbuzzLanguage(int8_t lang) {
    // TODO(AB): can langs be converted faster?
    auto const langsv = StringUtf8Multilang::GetLangByCode(lang);
    auto const hbLanguage = hb_language_from_string(sv.data(), sv.size());
    if (hbLanguage == HB_LANGUAGE_INVALID)
      return hb_language_get_default();
    return hbLanguage;
}

// We treat HarfBuzz ints as 16.16 fixed-point.
static const int kHbUnit1 = 1 << 16;

int SkiaScalarToHarfBuzzUnits(SkScalar value) {
    return base::saturated_cast<int>(value * kHbUnit1);
}

SkScalar HarfBuzzUnitsToSkiaScalar(int value) {
    static const SkScalar kSkToHbRatio = SK_Scalar1 / kHbUnit1;
    return kSkToHbRatio * value;
}

float HarfBuzzUnitsToFloat(int value) {
    static const float kFloatToHbRatio = 1.0f / kHbUnit1;
    return kFloatToHbRatio * value;
}

hb_font_t* CreateHarfbuzzFont(Font const & font,
                               int text_size,
                               const FontRenderParams& params,
                               bool subpixel_rendering_suppressed) {
    // A cache from Skia font to harfbuzz typeface information.
    using TypefaceCache = base::LRUCache<SkFontID, TypefaceData>;

    constexpr int kTypefaceCacheSize = 64;
    static base::NoDestructor<TypefaceCache> face_caches(kTypefaceCacheSize);

    TypefaceCache* typeface_cache = face_caches.get();
    TypefaceCache::iterator typeface_data =
        typeface_cache->Get(skia_face->uniqueID());
    if (typeface_data == typeface_cache->end()) {
      TypefaceData new_typeface_data(skia_face);
      typeface_data = typeface_cache->Put(skia_face->uniqueID(),
                                          std::move(new_typeface_data));
    }

    DCHECK(typeface_data->second.face());
    hb_font_t* harfbuzz_font = hb_font_create(typeface_data->second.face());

    const int scale = SkiaScalarToHarfBuzzUnits(text_size);
    hb_font_set_scale(harfbuzz_font, scale, scale);
    FontData* hb_font_data = new FontData(typeface_data->second.glyphs());
    hb_font_data->font_.setTypeface(std::move(skia_face));
    hb_font_data->font_.setSize(text_size);
    // TODO(ckocagil): Do we need to update these params later?
    internal::ApplyRenderParams(params, subpixel_rendering_suppressed,
                                &hb_font_data->font_);
    hb_font_set_funcs(harfbuzz_font, g_font_funcs.Get().get(), hb_font_data,
                      DeleteByType<FontData>);
    hb_font_make_immutable(harfbuzz_font);
    return harfbuzz_font;
}

void ShapeRunWithFont(std::u16string_view const & text, int runOffset, int runLength, UScriptCode script, bool isRtl, int8_t lang,
                      TextRunHarfBuzz::ShapeOutput* out) {
  hb_font_t* harfbuzz_font = CreateHarfBuzzFont(in.skia_face, SkIntToScalar(in.font_size),
                         in.render_params, in.subpixel_rendering_suppressed);

  // Create a HarfBuzz buffer and add the string to be shaped. The HarfBuzz
  // buffer holds our text, run information to be used by the shaping engine,
  // and the resulting glyph data.
  hb_buffer_t * buffer = hb_buffer_create();
  // Note that the value of the |item_offset| argument (here specified as
  // |in.range.start()|) does affect the result, so we will have to adjust
  // the computed offsets.
  hb_buffer_add_utf16(buffer, reinterpret_cast<uint16_t const *>(text.data()), static_cast<int>(text.size()), runOffset, runLength);
  hb_buffer_set_script(buffer, ICUScriptToHarfbuzzScript(script));
  hb_buffer_set_direction(buffer,isRtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);

  hb_buffer_set_language(buffer, OrganicMapsLanguageToHarfbuzzLanguage(lang));

  // Shape the text.
  hb_shape(harfbuzz_font, buffer, nullptr, 0);

  // Populate the run fields with the resulting glyph data in the buffer.
  unsigned int glyph_count = 0;
  hb_glyph_info_t * infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
  out->glyph_count = glyph_count;
  hb_glyph_position_t * hb_positions = hb_buffer_get_glyph_positions(buffer, nullptr);
  out->glyphs.resize(out->glyph_count);
  out->glyph_to_char.resize(out->glyph_count);
  out->positions.resize(out->glyph_count);
  out->width = 0.0f;

  // Font on MAC like ".SF NS Text" may have a negative x_offset. Positive
  // x_offset are also found on Windows (e.g. "Segoe UI"). It requires tests
  // relying on the behavior of |glyph_width_for_test_| to also be given a zero
  // x_offset, otherwise expectations get thrown off
  // (see: http://crbug.com/1056220).
  const bool force_zero_offset = in.glyph_width_for_test > 0;
  constexpr uint16_t kMissingGlyphId = 0;

  out->missing_glyph_count = 0;
  for (size_t i = 0; i < out->glyph_count; ++i) {
    // Max 65535 glyphs in font.
    DCHECK_LE(infos[i].codepoint, std::numeric_limits<uint16_t>::max());
    uint16_t glyph = static_cast<uint16_t>(infos[i].codepoint);
    out->glyphs[i] = glyph;
    if (glyph == kMissingGlyphId)
      out->missing_glyph_count += 1;
    //DCHECK_GE(infos[i].cluster, in.range.start());
    //out->glyph_to_char[i] = infos[i].cluster - in.range.start();
    const SkScalar x_offset =
        force_zero_offset ? 0
                          : HarfBuzzUnitsToSkiaScalar(hb_positions[i].x_offset);
    const SkScalar y_offset =
        HarfBuzzUnitsToSkiaScalar(hb_positions[i].y_offset);
    out->positions[i].set(out->width + x_offset, -y_offset);

    if (in.glyph_width_for_test == 0)
      out->width += HarfBuzzUnitsToFloat(hb_positions[i].x_advance);
    else if (hb_positions[i].x_advance)  // Leave zero-width glyphs alone.
      out->width += in.glyph_width_for_test;

    if (in.obscured)
      out->width += in.obscured_glyph_spacing;

    // When subpixel positioning is not enabled, glyph width is rounded to avoid
    // fractional width. Disable this conversion when a glyph width is provided
    // for testing. Using an integral glyph width has the same behavior as
    // disabling the subpixel positioning.
    const bool force_subpixel_for_test = in.glyph_width_for_test != 0;

    // Round run widths if subpixel positioning is off to match native behavior.
    if (!in.render_params.subpixel_positioning && !force_subpixel_for_test)
      out->width = std::round(out->width);
  }

  hb_buffer_destroy(buffer);
  hb_font_destroy(harfbuzz_font);
}











void ShapeRunsWithFont(std::u16string const & text, FontParams const & fontParams,
    std::vector<internal::TextRunHarfBuzz*>* in_out_runs) {
  // ShapeRunWithFont can be extremely slow, so use cached results if possible.
  // Only do this on the UI thread, to avoid synchronization overhead (and
  // because almost all calls are on the UI thread. Also avoid caching long
  // strings, to avoid blowing up the cache size.
  constexpr size_t kMaxRunLengthToCache = 25;
  static base::NoDestructor<internal::ShapeRunCache> cache;

  std::vector<internal::TextRunHarfBuzz*> runs_with_missing_glyphs;
  for (internal::TextRunHarfBuzz*& run : *in_out_runs) {
    // First do a cache lookup.
//    bool can_use_cache = base::CurrentUIThread::IsSet() &&
//                         run->range.length() <= kMaxRunLengthToCache;
//    bool found_in_cache = false;
//    const internal::ShapeRunWithFontInput cache_key(
//        text, font_params, run->range, obscured(), glyph_width_for_test_,
//        obscured_glyph_spacing(), subpixel_rendering_suppressed());
//    if (can_use_cache) {
//      auto found = cache.get()->Get(cache_key);
//      if (found != cache.get()->end()) {
//        run->UpdateFontParamsAndShape(font_params, found->second);
//        found_in_cache = true;
//      }
//    }

    // If that fails, compute the shape of the run, and add the result to the cache.
//    if (!found_in_cache) {
      internal::TextRunHarfBuzz::ShapeOutput output;
      ShapeRunWithFont(cache_key, &output);
      run->UpdateFontParamsAndShape(font_params, output);
      if (can_use_cache)
        cache.get()->Put(cache_key, output);
//    }

    // Check to see if we still have missing glyphs.
    if (run->shape.missing_glyph_count)
      runs_with_missing_glyphs.push_back(run);
  }
  in_out_runs->swap(runs_with_missing_glyphs);
}












void ShapeRuns(const std::u16string& text, int8_t lang, FontParams const & fontParams,
               std::vector<internal::TextRunHarfBuzz*> runs) {
  // Runs with a single newline character should be skipped since they can't be
  // rendered (see http://crbug/680430). The following code sets the runs
  // shaping output to report  the missing glyph and removes the runs from
  // the vector of runs to shape. The newline character doesn't have a
  // glyph, which otherwise forces this function to go through the expensive
  // font fallbacks before reporting a missing glyph (see http://crbug/972090).
//  std::vector<internal::TextRunHarfBuzz*> need_shaping_runs;
//  for (internal::TextRunHarfBuzz*& run : runs) {
//    if ((run->range.length() == 1 && (text[run->range.start()] == '\r' ||
//                                      text[run->range.start()] == '\n')) ||
//        (run->range.length() == 2 && text[run->range.start()] == '\r' &&
//         text[run->range.start() + 1] == '\n')) {
//      // Newline runs can't be shaped. Shape this run as if the glyph is
//      // missing.
//      run->font_params = font_params;
//      run->shape.missing_glyph_count = 1;
//      run->shape.glyph_count = 1;
//      run->shape.glyphs.resize(run->shape.glyph_count);
//      run->shape.glyph_to_char.resize(run->shape.glyph_count);
//      run->shape.positions.resize(run->shape.glyph_count);
//      // Keep width as zero since newline character doesn't have a width.
//    } else {
//      // This run needs shaping.
//      need_shaping_runs.push_back(run);
//    }
//  }
//  runs.swap(need_shaping_runs);
//  if (runs.empty()) {
//    RecordShapeRunsFallback(ShapeRunFallback::NO_FALLBACK);
//    return;
//  }

//  // Keep a set of fonts already tried for shaping runs.
//  std::set<SkFontID> fallback_fonts_already_tried;
//  std::vector<Font> fallback_font_candidates;

  // Shaping with primary configured fonts from font_list().
  for (const Font& font : font_list().GetFonts()) {
    internal::TextRunHarfBuzz::FontParams test_font_params = font_params;
    if (test_font_params.SetRenderParamsRematchFont(font, font.GetFontRenderParams()) &&
        !FontWasAlreadyTried(test_font_params.skia_face, &fallback_fonts_already_tried)) {
      ShapeRunsWithFont(text, test_font_params, &runs);
      MarkFontAsTried(test_font_params.skia_face, &fallback_fonts_already_tried);
      fallback_font_candidates.push_back(font);
    }
    if (runs.empty()) {
      RecordShapeRunsFallback(ShapeRunFallback::NO_FALLBACK);
      return;
    }
  }

  const Font& primary_font = font_list().GetPrimaryFont();

  // Find fallback fonts for the remaining runs using a worklist algorithm. Try
  // to shape the first run by using GetFallbackFont(...) and then try shaping
  // other runs with the same font. If the first font can't be shaped, remove it
  // and continue with the remaining runs until the worklist is empty. The
  // fallback font returned by GetFallbackFont(...) depends on the text of the
  // run and the results may differ between runs.
  std::vector<internal::TextRunHarfBuzz*> remaining_unshaped_runs;
  while (!runs.empty()) {
    Font fallback_font(primary_font);
    bool fallback_found;
    internal::TextRunHarfBuzz* current_run = *runs.begin();
    {
      SCOPED_UMA_HISTOGRAM_LONG_TIMER("RenderTextHarfBuzz.GetFallbackFontTime");
      TRACE_EVENT1("ui", "RenderTextHarfBuzz::GetFallbackFont", "script",
                   TRACE_STR_COPY(uscript_getShortName(font_params.script)));
      const base::StringPiece16 run_text(&text[current_run->range.start()],
                                         current_run->range.length());
      fallback_found =
          GetFallbackFont(primary_font, locale_, run_text, &fallback_font);
    }

    if (fallback_found) {
      internal::TextRunHarfBuzz::FontParams test_font_params = font_params;
      if (test_font_params.SetRenderParamsOverrideSkiaFaceFromFont(
              fallback_font, fallback_font.GetFontRenderParams()) &&
          !FontWasAlreadyTried(test_font_params.skia_face,
                               &fallback_fonts_already_tried)) {
        ShapeRunsWithFont(text, test_font_params, &runs);
        MarkFontAsTried(test_font_params.skia_face,
                        &fallback_fonts_already_tried);
      }
    }

    // Remove the first run if not fully shaped with its associated fallback
    // font.
    if (!runs.empty() && runs[0] == current_run) {
      remaining_unshaped_runs.push_back(current_run);
      runs.erase(runs.begin());
    }
  }
  runs.swap(remaining_unshaped_runs);
  if (runs.empty()) {
    RecordShapeRunsFallback(ShapeRunFallback::FALLBACK);
    return;
  }

  std::vector<Font> fallback_font_list;
  {
    SCOPED_UMA_HISTOGRAM_LONG_TIMER("RenderTextHarfBuzz.GetFallbackFontsTime");
    TRACE_EVENT1("ui", "RenderTextHarfBuzz::GetFallbackFonts", "script",
                 TRACE_STR_COPY(uscript_getShortName(font_params.script)));
    fallback_font_list = GetFallbackFonts(primary_font);

#if defined(OS_WIN)
    // Append fonts in the fallback list of the fallback fonts.
    // TODO(tapted): Investigate whether there's a case that benefits from this
    // on Mac.
    for (const auto& fallback_font : fallback_font_candidates) {
      std::vector<Font> fallback_fonts = GetFallbackFonts(fallback_font);
      fallback_font_list.insert(fallback_font_list.end(),
                                fallback_fonts.begin(), fallback_fonts.end());
    }

    // Add Segoe UI and its associated linked fonts to the fallback font list to
    // ensure that the fallback list covers the basic cases.
    // http://crbug.com/467459. On some Windows configurations the default font
    // could be a raster font like System, which would not give us a reasonable
    // fallback font list.
    Font segoe("Segoe UI", 13);
    if (!FontWasAlreadyTried(segoe.platform_font()->GetNativeSkTypeface(),
                             &fallback_fonts_already_tried)) {
      std::vector<Font> default_fallback_families = GetFallbackFonts(segoe);
      fallback_font_list.insert(fallback_font_list.end(),
                                default_fallback_families.begin(),
                                default_fallback_families.end());
    }
#endif
  }

  // Use a set to track the fallback fonts and avoid duplicate entries.
  SCOPED_UMA_HISTOGRAM_LONG_TIMER(
      "RenderTextHarfBuzz.ShapeRunsWithFallbackFontsTime");
  TRACE_EVENT1("ui", "RenderTextHarfBuzz::ShapeRunsWithFallbackFonts",
               "fonts_count", fallback_font_list.size());

  // Try shaping with the fallback fonts.
  for (const auto& font : fallback_font_list) {
    std::string font_name = font.GetFontName();

    FontRenderParamsQuery query;
    query.families.push_back(font_name);
    query.pixel_size = font_params.font_size;
    query.style = font_params.italic ? Font::ITALIC : 0;
    FontRenderParams fallback_render_params = GetFontRenderParams(query, NULL);
    internal::TextRunHarfBuzz::FontParams test_font_params = font_params;
    if (test_font_params.SetRenderParamsOverrideSkiaFaceFromFont(
            font, fallback_render_params) &&
        !FontWasAlreadyTried(test_font_params.skia_face,
                             &fallback_fonts_already_tried)) {
      ShapeRunsWithFont(text, test_font_params, &runs);
      MarkFontAsTried(test_font_params.skia_face,
                      &fallback_fonts_already_tried);
    }
    if (runs.empty()) {
      TRACE_EVENT_INSTANT2("ui", "RenderTextHarfBuzz::FallbackFont",
                           TRACE_EVENT_SCOPE_THREAD, "font_name",
                           TRACE_STR_COPY(font_name.c_str()),
                           "primary_font_name", primary_font.GetFontName());
      RecordShapeRunsFallback(ShapeRunFallback::FALLBACKS);
      return;
    }
  }

  for (internal::TextRunHarfBuzz*& run : runs) {
    if (run->shape.missing_glyph_count == std::numeric_limits<size_t>::max()) {
      run->shape.glyph_count = 0;
      run->shape.width = 0.0f;
    }
  }

  RecordShapeRunsFallback(ShapeRunFallback::FAILED);
}

// Shapes a single line of text without newline \r or \n characters.
// Any line breaking or trimming should be done by the caller.
void ItemizeAndShapeText(std::string_view utf8, int8_t lang, FontParams const & fontParams)
{
  ASSERT(!utf8.empty(), ());
  auto const utf16 = icu::UnicodeString::fromUTF8(utf8);
  for (auto const & run : ItemizeTextToRuns(utf16))
  {
    //internal::TextRunHarfBuzz::FontParams font_params = iter->first;
    //font_params.ComputeRenderParamsFontSizeAndBaselineOffset();
    ShapeRuns(utf16, lang, fontParams, run);
  }
}
