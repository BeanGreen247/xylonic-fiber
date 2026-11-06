// Metrics and icon codepoints measured from the old app's UI at 1024x768 (docs/ui-ref). All values are logical px.
#pragma once
#include <cstdint>
#include <string>

namespace skin {
constexpr float kHeaderH = 64, kNavW = 64, kPlayerH = 64, kPlayerFloat = 28, kPlayerInset = 12, kContentPad = 24;
constexpr float kCardW = 180, kCardH = 239, kCardGap = 24, kCardPad = 16, kCardCover = 148;

// Font Awesome 7 solid codepoints (the subset in assets/fonts/fa-solid-subset.ttf)
enum Fa : uint32_t {
    Music = 0xf001, Search = 0xf002, Heart = 0xf004, Check = 0xf00c, Xmark = 0xf00d, Gear = 0xf013, House = 0xf015, Download = 0xf019,
    Sync = 0xf021, Lock = 0xf023, VolumeHigh = 0xf028, List = 0xf03a, StepBack = 0xf048, Play = 0xf04b, Pause = 0xf04c, StepFwd = 0xf051,
    ChevLeft = 0xf053, ChevRight = 0xf054, CirclePlus = 0xf055, CircleXmark = 0xf057, CircleCheck = 0xf058, CircleQuestion = 0xf059,
    Ban = 0xf05e, ArrowLeft = 0xf060, Compress = 0xf066, Exclaim = 0xf06a, Fire = 0xf06d, Warning = 0xf071, CalendarDays = 0xf073,
    Shuffle = 0xf074, ChevUp = 0xf077, ChevDown = 0xf078, Folder = 0xf07b, Globe = 0xf0ac, Cloud = 0xf0c2, ListUl = 0xf0ca, Bolt = 0xf0e7,
    Spinner = 0xf110, Compass = 0xf14e, Car = 0xf1b9, History = 0xf1da, Sliders = 0xf1de, Wifi = 0xf1eb, Trash = 0xf1f8, CalendarPlus = 0xf271,
    SignOut = 0xf2f5, Repeat = 0xf363, Pen = 0xf304, BookOpen = 0xf518, CompactDisc = 0xf51f, Satellite = 0xf7c0, Grip = 0xf58e,
};

inline std::string utf8(uint32_t cp) {
    std::string s;
    if (cp < 0x80) s += static_cast<char>(cp);
    else if (cp < 0x800) { s += static_cast<char>(0xC0 | (cp >> 6)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += static_cast<char>(0xE0 | (cp >> 12)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { s += static_cast<char>(0xF0 | (cp >> 18)); s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
    return s;
}
}  // namespace skin
