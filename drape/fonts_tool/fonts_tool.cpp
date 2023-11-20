#include <iostream>

#include "base/file_name_utils.hpp"
#include "base/scope_guard.hpp"

#include "drape/font.hpp"

#include "platform/platform.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " <path to a directory with ttf files>\n";
    return -1;
  }
  std::string const kFontsDir = argv[1];
  Platform::FilesList ttfFiles;
  Platform::GetFilesByExt(kFontsDir, ".ttf", ttfFiles);

  // Initialize Freetype.
  FT_Library library;
  if (auto const err = FT_Init_FreeType(&library); err != 0)
  {
    std::cerr << "FT_Init_FreeType returned " << err << " error\n";
    return 1;
  }
  SCOPE_GUARD(doneFreetype, [&library]()
              {
                if (auto const err = FT_Done_FreeType(library); err != 0)
                  std::cerr << "FT_Done_FreeType returned " << err << " error\n";
              });

  // Scan all fonts.
  std::vector<dp::Font> fonts;
  for (auto const & ttf : ttfFiles)
  {
    std::cout << ttf << "\n";
    fonts.emplace_back(4, GetPlatform().GetReader(base::JoinPath(kFontsDir, ttf)), library);
    std::vector<FT_ULong> charcodes;
    fonts.back().GetCharcodes(charcodes);
  }

  return 0;
}
