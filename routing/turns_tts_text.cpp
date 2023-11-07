#include "routing/turns_tts_text.hpp"

#include "base/string_utils.hpp"
#include "indexer/road_shields_parser.hpp"
#include "routing/turns_sound_settings.hpp"

#include <string>
#include <regex>

namespace routing
{
namespace turns
{
namespace sound
{

namespace
{

template <class TIter> std::string DistToTextId(TIter begin, TIter end, uint32_t dist)
{
  TIter const it = std::lower_bound(begin, end, dist, [](auto const & p1, uint32_t p2) { return p1.first < p2; });
  if (it == end)
  {
    ASSERT(false, ("notification.m_distanceUnits is not correct."));
    return {};
  }

  if (it != begin)
  {
    // Rounding like 130 -> 100; 135 -> 200 is better than upper bound, IMO.
    auto iPrev = it;
    --iPrev;
    if ((dist - iPrev->first) * 2 < (it->first - dist))
      return iPrev->second;
  }

  return it->second;
}
}  //  namespace

void GetTtsText::SetLocale(std::string const & locale)
{
  m_getCurLang = platform::GetTextByIdFactory(platform::TextSource::TtsSound, locale);
}

void GetTtsText::ForTestingSetLocaleWithJson(std::string const & jsonBuffer, std::string const & locale)
{
  m_getCurLang = platform::ForTestingGetTextByIdFactory(jsonBuffer, locale);
}

/** Returns -1 for not found, positive for which item in the haystack was found
 * Finds the needle in an array haystack, and then returns the index of the item found
 * @tparam N - an arbitrary array size
 * @param haystack - array of string_views
 * @param needle - a string to find in the array
 * @return long - the index of the needle in the haystack
 */
template <std::size_t N>
long FindInStrArray(const std::array<std::string_view, N>& haystack, std::string& needle)
{
  auto begin = haystack.begin();
  for(auto i = begin; i != haystack.end(); ++i) {
    const std::string_view item = *i;
    std::string::size_type idx = needle.find(item);
    if (idx != std::string::npos) {
      long item_i = std::distance( begin, i );
      return item_i;
    }
  }
  return -1;
}

/**
 * @brief Modifies a string's last character to harmonize its vowel with a -ra/-re suffix
 * @param std::string & myString - string to harmonize and modify
 */
void HungarianBaseWordTransform(std::string& myString) {
  constexpr std::array<std::string_view, 4> baseStr = {"e", "a", "ö", "ü"};
  constexpr std::array<std::string_view, 4> harmonyStr = {"é", "á", "ő", "ű"};

  for (size_t i = 0; i < std::size(baseStr); ++i)
  {
    if (strings::EndsWith(myString, baseStr[i]))
      myString.replace(myString.size() - baseStr[i].size(), baseStr[i].size(), harmonyStr[i]);
  }
}

/**
 * @brief Decides if an uppercase/numeric string has a "front" or "back" ending.
 *
 * If the last two characters in an acronym or number (i.e. we won't say ABC or 123 as if they were
 * words, we will spell it out like ay bee see or one hundred twenty three) then in Hungarian we
 * start by looking at the last two characters. If the last two characters are 10, 40, 50, 70, 90
 * then we have a "-re" ending because of how it's pronounced. If they're 20, 30, 60, 80 then
 * they'll have a "-ra" ending.
 * A phrase ending in "-hundred" is a special case, so if the last three letters are "100" then that
 * has a "-ra" ending.
 * If none of the above are true, then we can simply look at the last character in the string for
 * the appropriate suffix. If the last character is one of AÁHIÍKOÓUŰ0368 then it gets a "-re"
 * ending. All other cases will get a "-ra" ending however we can't simply stop there because if
 * there is some unknown character like whitespace or punctuation we have to keep looking further
 * backwards into the string until we find a match or we run off the end of the word (" ").
 *
 * @arg std::string const & myString - numbers or acronyms to be spoken in Hungarian
 * @return 1 = front = -re, 2 = back = -ra
 */
uint8_t CategorizeHungarianAcronymsAndNumbers(std::string const & myString)
{
  constexpr std::array<std::string_view, 14> backNames = {
      "A", // a
      "Á", // á
      "H", // há
      "I", // i
      "Í", // í
      "K", // ká
      "O", // o
      "Ó", // ó
      "U", // u
      "Ű", // ú
      "0", // nulla or zéró
      "3", // három
      "6", // hat
      "8", // nyolc
  };

  constexpr std::array<std::string_view, 31> frontNames = {
      // all other letters besides H and K
      "B", "C", "D", "E", "É", "F", "G", "J", "L", "M", "N", "Ö", "Ő", "P", "Q", "R", "S", "T", "Ú", "Ü", "V", "W", "X", "Y", "Z",
      "1", // egy
      "2", // kettő
      "4", // négy
      "5", // öt
      "7", // hét
      "9", // kilenc
  };

  constexpr std::array<std::string_view, 5> specialCaseFront = {
      "10", // tíz special case front
      "40", // negyven front
      "50", // ötven front
      "70", // hetven front
      "90", // kilencven front
  };

  constexpr std::array<std::string_view, 4> specialCaseBack = {
      "20", // húsz back
      "30", // harminc back
      "60", // hatvan back
      "80", // nyolcvan back
  }; //TODO: consider regex

  //'100', // száz back

  for (int i = myString.size()-1; i >= 0; i--) {
    strings::UniString original = strings::MakeUniString(myString);
    // special is 2 char, so check last 2
    std::string oneBuf = strings::ToUtf8(strings::Substring(original, i, 1));
    std::string twoBuf = strings::ToUtf8(strings::Substring(original, i-1, 2));
    std::string threeBuf = strings::ToUtf8(strings::Substring(original, i-2, 3));
    if (FindInStrArray(specialCaseFront, twoBuf) != -1)
      return 1;
    if (FindInStrArray(specialCaseBack, twoBuf) != -1)
      return 2;
    if (threeBuf == "100")
      return 2;

    if (FindInStrArray(frontNames, oneBuf) != -1)
      return 1;
    if (FindInStrArray(backNames, oneBuf) != -1)
      return 2;
    if (oneBuf == " ") // if we've somehow hit a space, just say it's back
      return 2;
  }

  LOG(LWARNING, ("Unable to find Hungarian front/back for", myString));
  return 2;
}

/**
 * @brief Decides if a string (possibly Hungarian) has a "front" or "back" ending.
 *
 * Much like the Acronym/Number algorithm above, we start from the back of the word and
 * keep trying to match a front or back vowel until we find one. Indeterminate vowels are
 * "back" by default but only if we find nothing else. And if we truly find nothing, it
 * may be an acronym after all. (The word "acerbic" has a different ending sound than ABC.)
 *
 * @arg std::string const & myString - words, numbers or acronyms to be spoken in Hungarian
 * @return 1 = front = -re, 2 = back = -ra
 */
uint8_t CategorizeHungarianLastWordVowels(std::string const & myString)
{
  constexpr std::array<std::string_view, 6> front = {"e","é","ö","ő","ü","ű"};
  constexpr std::array<std::string_view, 6> back = {"a","á","o","ó","u","ú"};
  constexpr std::array<std::string_view, 2> indeterminate = {"i","í"};

  strings::UniString myUniStr = strings::MakeUniString(myString);

  // scan for acronyms and numbers first (i.e. characters spoken differently than words)
  // if the last word is an acronym/number like M5, check those instead
  if (EndsInAcronymOrNum(myUniStr)) {
    return CategorizeHungarianAcronymsAndNumbers(myString);
  }

  bool foundIndeterminate = false;

  // find last vowel in last word, since it discriminates in all cases
  // note we can't use single chars like myString[i] because of multi-byte characters
  for (int i = myString.size()-1; i >= 0; i--) {
    std::string oneBuf = strings::ToUtf8(strings::Substring(myUniStr, i, 1));

    std::string lowerC = strings::MakeLowerCase(oneBuf);
    if (FindInStrArray(front, lowerC) != -1)
      return 1;
    if (FindInStrArray(back, lowerC) != -1)
      return 2;
    if (FindInStrArray(indeterminate, lowerC) != -1)
      foundIndeterminate = true;
    if (!oneBuf.compare(" ") && foundIndeterminate == true) // if we've hit a space with only indeterminates, it's back
      return 2;
    if (!oneBuf.compare(" ") && foundIndeterminate == false) // if we've hit a space with no vowels at all, check for numbers and acronyms
      return CategorizeHungarianAcronymsAndNumbers(myString);
  }
  // if we got here, are we even reading Hungarian words?
  LOG(LWARNING, ("Hungarian word not found:", myString));
  return 2; // default
}

/**
 * @brief Modified version of GetFullRoadName in routing_session.cpp.
 * For next street returns "ref; name" .
 * For highway exits (or main roads with exit info) returns "junction:ref; target:ref; target".
 * If no |target| - it will be replaced by |name| of next street.
 * If no |target:ref| - it will be replaced by |ref| of next road.
 * So if link has no info at all, "ref; name" of next will be returned (as for next street).
 *
 * @arg RouteSegment::RoadNameInfo & road - structured info input about the next street
 * @arg std::string & name - semicolon-delimited string output for the TTS engine
 */
void FormatFullRoadName(RouteSegment::RoadNameInfo & road, std::string & name)
{
  name.clear();

  if (auto const & sh = ftypes::GetRoadShields(road.m_ref); !sh.empty())
    road.m_ref = sh[0].m_name;
  if (auto const & sh = ftypes::GetRoadShields(road.m_destination_ref); !sh.empty())
    road.m_destination_ref = sh[0].m_name;

  std::vector<std::string> outArr;

  if (road.HasExitInfo())
  {
    if (!road.m_junction_ref.empty())
      outArr.emplace_back(road.m_junction_ref);

    if (!road.m_destination_ref.empty())
      outArr.emplace_back(road.m_destination_ref);

    if (!road.m_destination.empty())
      outArr.emplace_back(road.m_destination);
    else if (!road.m_name.empty())
      outArr.emplace_back(road.m_name);
  }
  else
  {
    if (!road.m_ref.empty())
      outArr.emplace_back(road.m_ref);
    if (!road.m_name.empty())
      outArr.emplace_back(road.m_name);
  }

  if (outArr.size() > 0) {
    // append strings with delimiter and no trailing
    for (size_t i = 0; i < outArr.size()-1; i++) {
      name.append(outArr[i]+"; ");
    }
    name.append(outArr[outArr.size()-1]);
  }
}

std::string GetTtsText::GetTurnNotification(Notification const & notification) const
{
  const std::string localeKey = GetLocale();
  const std::string dirKey = GetDirectionTextId(notification);
  std::string dirStr = GetTextById(dirKey);

  if (notification.m_distanceUnits == 0 && !notification.m_useThenInsteadOfDistance && notification.m_nextStreetInfo.empty())
    return dirStr;

  if (notification.IsPedestrianNotification())
  {
    if (notification.m_useThenInsteadOfDistance &&
        notification.m_turnDirPedestrian == PedestrianDirection::None)
      return {};
  }

  if (notification.m_useThenInsteadOfDistance && notification.m_turnDir == CarDirection::None)
    return {};

  if (dirStr.empty())
    return {};

  std::string thenStr;
  if (notification.m_useThenInsteadOfDistance) {
    // add "then" and space only if needed, for appropriate languages
    if(localeKey != "ja")
      thenStr = GetTextById("then") + " ";
    else
      thenStr = GetTextById("then");
  }

  std::string distStr;
  if (notification.m_distanceUnits > 0)
    distStr = GetTextById(GetDistanceTextId(notification));

  // Get a string like 245; CA 123; Highway 99; San Francisco
  // In the future we could use the full RoadNameInfo struct to do some nice formatting.
  std::string streetOut;
  RouteSegment::RoadNameInfo nsi = notification.m_nextStreetInfo; // extract non-const
  FormatFullRoadName(nsi, streetOut);

  if (!streetOut.empty()) {
    // We're going to pronounce the street name.

    // Replace any full-stop characters (in between sub-instructions) to make TTS flow better.
    // Full stops are: . (Period) or 。 (East Asian) or । (Hindi)
    strings::ReplaceLast(distStr, ".", "");
    strings::ReplaceLast(distStr, "。", "");
    strings::ReplaceLast(distStr, "।", "");

    // If the turn direction with the key +_street exists for this locale, use it (like make_a_right_turn_street)
    std::string dirStreetStr = GetTextById(dirKey+"_street");
    if (!dirStreetStr.empty())
      dirStr = std::move(dirStreetStr);

    // Normally use "onto" for "turn right onto Main St"
    std::string ontoStr = GetTextById("onto");

    // If the nextStreetInfo has an exit number, we'll announce it
    if (!notification.m_nextStreetInfo.m_junction_ref.empty()) {
      // Try to get a specific "take exit #" phrase and its associated "onto" phrase (if any)
      std::string dirExitStr = GetTextById("take_exit_number");
      if (!dirExitStr.empty()) {
        dirStr = std::move(dirExitStr);
        ontoStr = ""; // take_exit_number overwrites "onto"
      }
    }

    // Same as above but for dirStr instead of distStr
    strings::ReplaceLast(dirStr, ".", "");
    strings::ReplaceLast(dirStr, "。", "");
    strings::ReplaceLast(dirStr, "।", "");

    std::string distDirOntoStreetStr = GetTextById("dist_direction_onto_street");
    // TODO: we may want to only load _street_verb if _street exists; may also need to handle
    //   a lack of a $5 position in the formatter string
    std::string dirVerb = GetTextById(dirKey+"_street_verb");

    if (localeKey == "hu") {
      HungarianBaseWordTransform(streetOut); // modify streetOut's last letter if it's a vowel

      // adjust the -re suffix in the formatter string based on last-word vowels
      uint8_t hungarianism = CategorizeHungarianLastWordVowels(streetOut);
      if (hungarianism == 1) {
        strings::ReplaceLast(distDirOntoStreetStr, "-re", "re"); // just remove hyphenation
      } else if (hungarianism == 2) {
        strings::ReplaceLast(distDirOntoStreetStr, "-re", "ra"); // change re to ra without hyphen
      } else {
        strings::ReplaceLast(distDirOntoStreetStr, "-re", ""); // clear it
      }

      // if the first pronounceable character of the street is a vowel, use "az" instead of "a"
      // 1, 5, and 1000 start with vowels but not 10 or 100 (including 5*, 5**, 1*, 1**, 1***, etc)
      static const std::regex rHun("^[5aeiouyáéíóúöüőű]|^1$|^1[^\\d]|^1\\d\\d\\d[^\\d]", std::regex_constants::icase);
      std::smatch ma;
      if (std::regex_search(streetOut, ma, rHun) && ma.size() > 0) {
        if (ontoStr == "a")
          ontoStr = "az";
        if (dirStr == "Hajtson ki a")
          dirStr = "Hajtson ki az";
      }
    }

    char ttsOut[1024];
    snprintf(ttsOut, sizeof(ttsOut)/sizeof(ttsOut[0]),
      distDirOntoStreetStr.c_str(),
      distStr.c_str(), // in 100 feet
      dirStr.c_str(), // turn right / take exit
      ontoStr.c_str(), // onto / null
      streetOut.c_str(), // Main Street / 543:: M4: Queens Parkway, London
      dirVerb.c_str() // (optional "turn right" verb)
    );

    // remove floating punctuation
    static const std::regex rP(" [,\\.:;]+ ");
    std::string cleanOut = std::regex_replace(ttsOut, rP, " ");
    // remove repetitious spaces or colons
    static const std::regex rS("[ :]{2,99}");
    cleanOut = std::regex_replace(cleanOut, rS, " ");
    // trim leading spaces
    static const std::regex rL("^ +");
    cleanOut = std::regex_replace(cleanOut, rL, "");

    LOG(LINFO, ("TTSn", thenStr + cleanOut));

    return thenStr + cleanOut;
  }

  std::string out;
  if (!distStr.empty()) {
    // add distance and/or space only if needed, for appropriate languages
    if(localeKey != "ja")
      out = thenStr + distStr + " " + dirStr;
    else
      out = thenStr + distStr + dirStr;
  } else {
    out = thenStr + dirStr;
  }
  LOG(LINFO, ("TTS", out));
  return out;
}

bool EndsInAcronymOrNum(strings::UniString const & myUniStr)
{
  bool allUppercaseNum = true;
  for (int i = myUniStr.size()-1; i >= 0; i--) {
    std::string oneBuf = strings::ToUtf8(strings::Substring(myUniStr, i, 1));
    std::string lowerOneBuf = oneBuf;
    lowerOneBuf = strings::MakeLowerCase(lowerOneBuf);

    static const std::regex rNum("^[0-9]$");
    std::smatch nM;
    std::regex_search(lowerOneBuf, nM, rNum);

    // if we've reached a space, we're done here
    if (oneBuf == " ")
      break;
    else if (oneBuf == lowerOneBuf && nM.size() == 0) {
      allUppercaseNum = false;
      break;
    }
  }
  return allUppercaseNum;
}

std::string GetTtsText::GetSpeedCameraNotification() const
{
  return GetTextById("unknown_camera");
}

std::string GetTtsText::GetLocale() const
{
  if (m_getCurLang == nullptr)
  {
    ASSERT(false, ());
    return {};
  }
  return m_getCurLang->GetLocale();
}

std::string GetTtsText::GetTextById(std::string const & textId) const
{
  ASSERT(!textId.empty(), ());

  if (m_getCurLang == nullptr)
  {
    ASSERT(false, ());
    return {};
  }
  return (*m_getCurLang)(textId);
}

std::string GetDistanceTextId(Notification const & notification)
{
//  if (notification.m_useThenInsteadOfDistance)
//    return "then";

  switch (notification.m_lengthUnits)
  {
  case measurement_utils::Units::Metric:
    return DistToTextId(GetAllSoundedDistMeters().cbegin(), GetAllSoundedDistMeters().cend(),
                        notification.m_distanceUnits);
  case measurement_utils::Units::Imperial:
    return DistToTextId(GetAllSoundedDistFeet().cbegin(), GetAllSoundedDistFeet().cend(),
                        notification.m_distanceUnits);
  }
  ASSERT(false, ());
  return {};
}

std::string GetRoundaboutTextId(Notification const & notification)
{
  if (notification.m_turnDir != CarDirection::LeaveRoundAbout)
  {
    ASSERT(false, ());
    return {};
  }
  if (!notification.m_useThenInsteadOfDistance)
    return "leave_the_roundabout"; // Notification just before leaving a roundabout.

  static constexpr uint8_t kMaxSoundedExit = 11;
  if (notification.m_exitNum == 0 || notification.m_exitNum > kMaxSoundedExit)
    return "leave_the_roundabout";

  return "take_the_" + strings::to_string(static_cast<int>(notification.m_exitNum)) + "_exit";
}

std::string GetYouArriveTextId(Notification const & notification)
{
  if (!notification.IsPedestrianNotification() &&
      notification.m_turnDir != CarDirection::ReachedYourDestination)
  {
    ASSERT(false, ());
    return {};
  }

  if (notification.IsPedestrianNotification() &&
      notification.m_turnDirPedestrian != PedestrianDirection::ReachedYourDestination)
  {
    ASSERT(false, ());
    return {};
  }

  if (notification.m_distanceUnits != 0 || notification.m_useThenInsteadOfDistance)
    return "destination";
  return "you_have_reached_the_destination";
}

std::string GetDirectionTextId(Notification const & notification)
{
  if (notification.IsPedestrianNotification())
  {
    switch (notification.m_turnDirPedestrian)
    {
    case PedestrianDirection::GoStraight: return "go_straight";
    case PedestrianDirection::TurnRight: return "make_a_right_turn";
    case PedestrianDirection::TurnLeft: return "make_a_left_turn";
    case PedestrianDirection::ReachedYourDestination: return GetYouArriveTextId(notification);
    case PedestrianDirection::None:
    case PedestrianDirection::Count: ASSERT(false, (notification)); return {};
    }
  }

  switch (notification.m_turnDir)
  {
    case CarDirection::GoStraight:
      return "go_straight";
    case CarDirection::TurnRight:
      return "make_a_right_turn";
    case CarDirection::TurnSharpRight:
      return "make_a_sharp_right_turn";
    case CarDirection::TurnSlightRight:
      return "make_a_slight_right_turn";
    case CarDirection::TurnLeft:
      return "make_a_left_turn";
    case CarDirection::TurnSharpLeft:
      return "make_a_sharp_left_turn";
    case CarDirection::TurnSlightLeft:
      return "make_a_slight_left_turn";
    case CarDirection::UTurnLeft:
    case CarDirection::UTurnRight:
      return "make_a_u_turn";
    case CarDirection::EnterRoundAbout:
      return "enter_the_roundabout";
    case CarDirection::LeaveRoundAbout:
      return GetRoundaboutTextId(notification);
    case CarDirection::ReachedYourDestination:
      return GetYouArriveTextId(notification);
    case CarDirection::ExitHighwayToLeft:
    case CarDirection::ExitHighwayToRight:
      return "exit";
    case CarDirection::StayOnRoundAbout:
    case CarDirection::StartAtEndOfStreet:
    case CarDirection::None:
    case CarDirection::Count:
      ASSERT(false, ());
      return {};
  }
  ASSERT(false, ());
  return {};
}
}  // namespace sound
}  // namespace turns
}  // namespace routing
