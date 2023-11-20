#include "glyph_manager.hpp"
#include "font.hpp"
#include "font_constants.hpp"

#include "platform/platform.hpp"

#include "coding/reader.hpp"

#include "base/string_utils.hpp"
#include "base/logging.hpp"
#include "base/math.hpp"

#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace dp
{
static int constexpr kInvalidFont = -1;

template <typename ToDo>
void ParseUniBlocks(std::string const & uniBlocksFile, ToDo toDo)
{
  std::string uniBlocks;
  try
  {
    ReaderPtr<Reader>(GetPlatform().GetReader(uniBlocksFile)).ReadAsString(uniBlocks);
  }
  catch (RootException const & e)
  {
    LOG(LCRITICAL, ("Error reading uniblock description: ", e.what()));
    return;
  }

  std::istringstream fin(uniBlocks);
  while (true)
  {
    std::string name;
    strings::UniChar start;
    strings::UniChar end;
    fin >> name >> std::hex >> start >> std::hex >> end;
    if (!fin)
      break;

    toDo(name, start, end);
  }
}

template <typename ToDo>
void ParseFontList(std::string const & fontListFile, ToDo toDo)
{
  std::string fontList;
  try
  {
    ReaderPtr<Reader>(GetPlatform().GetReader(fontListFile)).ReadAsString(fontList);
  }
  catch(RootException const & e)
  {
    LOG(LWARNING, ("Error reading font list ", fontListFile, " : ", e.what()));
    return;
  }

  std::istringstream fin(fontList);
  while (true)
  {
    std::string ubName;
    std::string fontName;
    fin >> ubName >> fontName;
    if (!fin)
      break;

    toDo(ubName, fontName);
  }
}

// Information about single unicode block.
struct UnicodeBlock
{
  std::string m_name;

  strings::UniChar m_start;
  strings::UniChar m_end;
  std::vector<int> m_fontsWeight;

  UnicodeBlock(std::string const & name, strings::UniChar start, strings::UniChar end)
    : m_name(name)
    , m_start(start)
    , m_end(end)
  {}

  int GetFontOffset(int idx) const
  {
    if (m_fontsWeight.empty())
      return kInvalidFont;

    int maxWeight = 0;
    int upperBoundWeight = std::numeric_limits<int>::max();
    if (idx != kInvalidFont)
      upperBoundWeight = m_fontsWeight[idx];

    int index = kInvalidFont;
    ASSERT_LESS(m_fontsWeight.size(), static_cast<size_t>(std::numeric_limits<int>::max()), ());
    for (size_t i = 0; i < m_fontsWeight.size(); ++i)
    {
      int const w = m_fontsWeight[i];
      if (w < upperBoundWeight && w > maxWeight)
      {
        maxWeight = w;
        index = static_cast<int>(i);
      }
    }

    return index;
  }

  bool HasSymbol(strings::UniChar sym) const
  {
    return (m_start <= sym) && (m_end >= sym);
  }
};

using TUniBlocks = std::vector<UnicodeBlock>;
using TUniBlockIter = TUniBlocks::const_iterator;

struct GlyphManager::Impl
{
  Impl() = default;

  ~Impl() {
    m_fonts.clear();
    if (m_library)
      FREETYPE_CHECK(FT_Done_FreeType(m_library));
  }

  Impl(Impl const &) = delete;
  Impl(Impl &&) = delete;
  Impl & operator=(Impl const &) = delete;
  Impl & operator=(Impl &&) = delete;

  FT_Library m_library;
  TUniBlocks m_blocks;
  TUniBlockIter m_lastUsedBlock;
  std::vector<std::unique_ptr<Font>> m_fonts;

  uint32_t m_baseGlyphHeight;
  uint32_t m_sdfScale;
};

// Destructor is defined where pimpl's destructor is already known.
GlyphManager::~GlyphManager() = default;

GlyphManager::GlyphManager(Params const & params)
  : m_impl(std::make_unique<Impl>())
{
  m_impl->m_baseGlyphHeight = params.m_baseGlyphHeight;
  m_impl->m_sdfScale = params.m_sdfScale;

  using TFontAndBlockName = std::pair<std::string, std::string>;
  using TFontLst = buffer_vector<TFontAndBlockName, 64>;

  TFontLst whitelst;
  TFontLst blacklst;

  m_impl->m_blocks.reserve(160);
  ParseUniBlocks(params.m_uniBlocks, [this](std::string const & name,
                                            strings::UniChar start, strings::UniChar end)
  {
    m_impl->m_blocks.emplace_back(name, start, end);
  });

  ParseFontList(params.m_whitelist, [&whitelst](std::string const & ubName, std::string const & fontName)
  {
    whitelst.emplace_back(fontName, ubName);
  });

  ParseFontList(params.m_blacklist, [&blacklst](std::string const & ubName, std::string const & fontName)
  {
    blacklst.emplace_back(fontName, ubName);
  });

  m_impl->m_fonts.reserve(params.m_fonts.size());

  FREETYPE_CHECK(FT_Init_FreeType(&m_impl->m_library));

  for (auto const & fontName : params.m_fonts)
  {
    bool ignoreFont = false;
    std::for_each(blacklst.begin(), blacklst.end(), [&ignoreFont, &fontName](TFontAndBlockName const & p)
    {
      if (p.first == fontName && p.second == "*")
        ignoreFont = true;
    });

    if (ignoreFont)
      continue;

    std::vector<FT_ULong> charCodes;
    try
    {
      m_impl->m_fonts.emplace_back(std::make_unique<Font>(params.m_sdfScale, GetPlatform().GetReader(fontName),
                                                          m_impl->m_library));
      m_impl->m_fonts.back()->GetCharcodes(charCodes);
    }
    catch(RootException const & e)
    {
      LOG(LWARNING, ("Error reading font file =", fontName, "; Reason =", e.what()));
      continue;
    }

    using BlockIndex = size_t;
    using CharCounter = int;
    using CoverNode = std::pair<BlockIndex, CharCounter>;
    using CoverInfo = std::vector<CoverNode>;

    size_t currentUniBlock = 0;
    CoverInfo coverInfo;
    for (auto const charCode : charCodes)
    {
      size_t block = currentUniBlock;
      while (block < m_impl->m_blocks.size())
      {
        if (m_impl->m_blocks[block].HasSymbol(static_cast<strings::UniChar>(charCode)))
          break;
        ++block;
      }

      if (block < m_impl->m_blocks.size())
      {
        if (coverInfo.empty() || coverInfo.back().first != block)
          coverInfo.emplace_back(block, 1);
        else
          ++coverInfo.back().second;

        currentUniBlock = block;
      }
    }

    using TUpdateCoverInfoFn = std::function<void(UnicodeBlock const & uniBlock, CoverNode & node)>;
    auto const enumerateFn = [this, &coverInfo, &fontName] (TFontLst const & lst, TUpdateCoverInfoFn const & fn)
    {
      for (auto const & b : lst)
      {
        if (b.first != fontName)
          continue;

        for (CoverNode & node : coverInfo)
        {
          auto const & uniBlock = m_impl->m_blocks[node.first];
          if (uniBlock.m_name == b.second)
          {
            fn(uniBlock, node);
            break;
          }
          else if (b.second == "*")
          {
            fn(uniBlock, node);
          }
        }
      }
    };

    enumerateFn(blacklst, [](UnicodeBlock const &, CoverNode & node)
    {
      node.second = 0;
    });

    enumerateFn(whitelst, [this](UnicodeBlock const & uniBlock, CoverNode & node)
    {
      node.second = static_cast<int>(uniBlock.m_end + 1 - uniBlock.m_start + m_impl->m_fonts.size());
    });

    for (CoverNode const & node : coverInfo)
    {
      UnicodeBlock & uniBlock = m_impl->m_blocks[node.first];
      uniBlock.m_fontsWeight.resize(m_impl->m_fonts.size(), 0);
      uniBlock.m_fontsWeight.back() = node.second;
    }
  }

  m_impl->m_lastUsedBlock = m_impl->m_blocks.end();

  LOG(LDEBUG, ("How unicode blocks are mapped on font files:"));

  // We don't have black list for now.
  ASSERT_EQUAL(m_impl->m_fonts.size(), params.m_fonts.size(), ());

  for (auto const & b : m_impl->m_blocks)
  {
    auto const & weights = b.m_fontsWeight;
    ASSERT_LESS_OR_EQUAL(weights.size(), m_impl->m_fonts.size(), ());
    if (weights.empty())
    {
      LOG_SHORT(LDEBUG, (b.m_name, "is unsupported"));
    }
    else
    {
      size_t const ind = std::distance(weights.begin(), std::max_element(weights.begin(), weights.end()));
      LOG_SHORT(LDEBUG, (b.m_name, "is in", params.m_fonts[ind]));
    }
  }


  // tmp
  std::ifstream names("/Users/alex/Developer/omim/rust-extract/sorted_unique.txt");
  std::string line;
  while (names.good()) {
    std::getline(names, line);
    
  }
}

uint32_t GlyphManager::GetBaseGlyphHeight() const
{
  return m_impl->m_baseGlyphHeight;
}

uint32_t GlyphManager::GetSdfScale() const
{
  return m_impl->m_sdfScale;
}

int GlyphManager::GetFontIndex(strings::UniChar unicodePoint)
{
  auto iter = m_impl->m_blocks.cend();
  if (m_impl->m_lastUsedBlock != m_impl->m_blocks.end() &&
      m_impl->m_lastUsedBlock->HasSymbol(unicodePoint))
  {
    iter = m_impl->m_lastUsedBlock;
  }
  else
  {
    if (iter == m_impl->m_blocks.end() || !iter->HasSymbol(unicodePoint))
    {
      iter = std::lower_bound(m_impl->m_blocks.begin(), m_impl->m_blocks.end(), unicodePoint,
                              [](UnicodeBlock const & block, strings::UniChar const & v)
      {
        return block.m_end < v;
      });
    }
  }

  if (iter == m_impl->m_blocks.end() || !iter->HasSymbol(unicodePoint))
    return kInvalidFont;

  m_impl->m_lastUsedBlock = iter;

  return FindFontIndexInBlock(*m_impl->m_lastUsedBlock, unicodePoint);
}

int GlyphManager::GetFontIndexImmutable(strings::UniChar unicodePoint) const
{
  TUniBlockIter iter = std::lower_bound(m_impl->m_blocks.begin(), m_impl->m_blocks.end(), unicodePoint,
                                        [](UnicodeBlock const & block, strings::UniChar const & v)
  {
    return block.m_end < v;
  });

  if (iter == m_impl->m_blocks.end() || !iter->HasSymbol(unicodePoint))
    return kInvalidFont;

  return FindFontIndexInBlock(*iter, unicodePoint);
}

int GlyphManager::FindFontIndexInBlock(UnicodeBlock const & block, strings::UniChar unicodePoint) const
{
  ASSERT(block.HasSymbol(unicodePoint), ());
  for (int fontIndex = block.GetFontOffset(kInvalidFont); fontIndex != kInvalidFont;
       fontIndex = block.GetFontOffset(fontIndex))
  {
    ASSERT_LESS(fontIndex, static_cast<int>(m_impl->m_fonts.size()), ());
    auto const & f = m_impl->m_fonts[fontIndex];
    if (f->HasGlyph(unicodePoint))
      return fontIndex;
  }

  return kInvalidFont;
}

Glyph GlyphManager::GetGlyph(strings::UniChar unicodePoint, int fixedHeight)
{
  int const fontIndex = GetFontIndex(unicodePoint);
  if (fontIndex == kInvalidFont)
    return GetInvalidGlyph(fixedHeight);

  auto const & f = m_impl->m_fonts[fontIndex];
  bool const isSdf = fixedHeight < 0;
  Glyph glyph = f->GetGlyph(unicodePoint, isSdf ? m_impl->m_baseGlyphHeight : fixedHeight, isSdf);
  glyph.m_fontIndex = fontIndex;
  return glyph;
}

// static
Glyph GlyphManager::GenerateGlyph(Glyph const & glyph, uint32_t sdfScale)
{
  if (glyph.m_image.m_data != nullptr)
  {
    Glyph resultGlyph;
    resultGlyph.m_metrics = glyph.m_metrics;
    resultGlyph.m_fontIndex = glyph.m_fontIndex;
    resultGlyph.m_code = glyph.m_code;
    resultGlyph.m_fixedSize = glyph.m_fixedSize;

    if (glyph.m_fixedSize < 0)
    {
      sdf_image::SdfImage img(glyph.m_image.m_bitmapRows, glyph.m_image.m_bitmapPitch,
                              glyph.m_image.m_data->data(), sdfScale * kSdfBorder);

      img.GenerateSDF(1.0f / static_cast<float>(sdfScale));

      ASSERT_EQUAL(img.GetWidth(), glyph.m_image.m_width, ());
      ASSERT_EQUAL(img.GetHeight(), glyph.m_image.m_height, ());

      size_t const bufferSize = base::NextPowOf2(glyph.m_image.m_width * glyph.m_image.m_height);
      resultGlyph.m_image.m_data = SharedBufferManager::instance().reserveSharedBuffer(bufferSize);

      img.GetData(*resultGlyph.m_image.m_data);
    }
    else
    {
      size_t const bufferSize = base::NextPowOf2(glyph.m_image.m_width * glyph.m_image.m_height);
      resultGlyph.m_image.m_data = SharedBufferManager::instance().reserveSharedBuffer(bufferSize);
      resultGlyph.m_image.m_data->assign(glyph.m_image.m_data->begin(), glyph.m_image.m_data->end());
    }

    resultGlyph.m_image.m_width = glyph.m_image.m_width;
    resultGlyph.m_image.m_height = glyph.m_image.m_height;
    resultGlyph.m_image.m_bitmapRows = 0;
    resultGlyph.m_image.m_bitmapPitch = 0;

    return resultGlyph;
  }
  return glyph;
}

void GlyphManager::MarkGlyphReady(Glyph const & glyph)
{
  ASSERT_GREATER_OR_EQUAL(glyph.m_fontIndex, 0, ());
  ASSERT_LESS(glyph.m_fontIndex, static_cast<int>(m_impl->m_fonts.size()), ());
  m_impl->m_fonts[glyph.m_fontIndex]->MarkGlyphReady(glyph.m_code, glyph.m_fixedSize);
}

bool GlyphManager::AreGlyphsReady(strings::UniString const & str, int fixedSize) const
{
  for (auto const & code : str)
  {
    int const fontIndex = GetFontIndexImmutable(code);
    if (fontIndex == kInvalidFont)
      return false;

    if (!m_impl->m_fonts[fontIndex]->IsGlyphReady(code, fixedSize))
      return false;
  }

  return true;
}

Glyph GlyphManager::GetInvalidGlyph(int fixedSize) const
{
  strings::UniChar constexpr kInvalidGlyphCode = 0x9;
  int constexpr kFontId = 0;

  static bool s_inited = false;
  static Glyph s_glyph;

  if (!s_inited)
  {
    ASSERT(!m_impl->m_fonts.empty(), ());
    bool const isSdf = fixedSize < 0 ;
    s_glyph = m_impl->m_fonts[kFontId]->GetGlyph(kInvalidGlyphCode,
                                                 isSdf ? m_impl->m_baseGlyphHeight : fixedSize,
                                                 isSdf);
    s_glyph.m_metrics.m_isValid = false;
    s_glyph.m_fontIndex = kFontId;
    s_glyph.m_code = kInvalidGlyphCode;
    s_inited = true;
  }

  return s_glyph;
}
}  // namespace dp
