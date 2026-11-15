// Names for new songs: two words, one from each of two lists (our own), like "Velvet Harbour".
#pragma once
#include <cstdint>
#include <string>

namespace twill {

// The same seed gives the same name. Fits the project-name editor: letters and a space only.
std::string song_name(uint32_t seed);

}  // namespace twill
