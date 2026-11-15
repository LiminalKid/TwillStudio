#include "app/SongNames.h"

#include <iterator>

namespace twill {

namespace {

// Words that colour the second (adjectives, and nouns that work as one: "Glass", "Pixel").
const char* const kFirst[] = {
    // Moods, light, weather and materials.
    "Amber", "Ashen", "Azure", "Bitter", "Blue", "Brass", "Bright", "Broken", "Burnt", "Calm", "Cedar",
    "Chrome", "Cinder", "Clear", "Cobalt", "Cold", "Copper", "Coral", "Crimson", "Crystal", "Dawn", "Deep",
    "Distant", "Drifting", "Dusty", "Early", "Echo", "Electric", "Ember", "Empty", "Endless", "Faded", "Fern",
    "Floating", "Foggy", "Folded", "Frozen", "Gentle", "Ghost", "Glass", "Gold", "Golden", "Granite", "Grey",
    "Hollow", "Honey", "Humming", "Hushed", "Indigo", "Iron", "Ivory", "Jade", "Late", "Lazy", "Linen",
    "Liquid", "Lost", "Low", "Lunar", "Marble", "Midnight", "Misty", "Moss", "Neon", "Night", "Northern",
    "Ochre", "Olive", "Opal", "Orange", "Pale", "Paper", "Pastel", "Pearl", "Pine", "Plastic", "Quiet",
    "Rain", "Restless", "Rusty", "Saffron", "Salt", "Scarlet", "Secret", "Silent", "Silver", "Slow", "Smoky",
    "Soft", "Solar", "Southern", "Static", "Steel", "Still", "Stone", "Summer", "Sunken", "Tangled", "Tidal",
    "Tin", "Twilight", "Velvet", "Violet", "Warm", "Wild", "Winter", "Wooden", "Woollen", "Young", "Zinc",
    // Empty buildings after hours: hum, carpet and strip lights.
    "Abandoned", "Beige", "Buzzing", "Carpeted", "Closed", "Concrete", "Damp", "Dim", "Familiar",
    "Flickering", "Forgotten", "Halogen", "Laminate", "Liminal", "Looping", "Mall", "Muted", "Overcast",
    "Padded", "Pool", "Recycled", "Sleepless", "Sodium", "Stale", "Tiled", "Unlit", "Unmarked", "Vacant",
    "Vinyl", "Waiting", "Yellowed",
    // Rainbows, sweets and the sound chips of old consoles.
    "Bouncy", "Bubble", "Bubblegum", "Candy", "Cherry", "Chroma", "Cosmic", "Cyan", "Digital", "Glitter",
    "Glowing", "Happy", "Hyper", "Laser", "Lemon", "Lilac", "Lime", "Lucky", "Magenta", "Mega", "Mint",
    "Noise", "Peach", "Pixel", "Prismatic", "Pulse", "Rainbow", "Retro", "Sawtooth", "Sherbet", "Shiny",
    "Sparkle", "Square", "Starry", "Sugar", "Super", "Tangerine", "Triangle", "Turbo", "Turquoise", "Ultra",
    "Vivid",
};

// Things and places.
const char* const kSecond[] = {
    // Places, machines and weather.
    "Alley", "Anchor", "Antenna", "Arcade", "Atlas", "Attic", "Avenue", "Balcony", "Beacon", "Bells", "Bloom",
    "Bridge", "Cabin", "Canal", "Canyon", "Carousel", "Cascade", "Cathedral", "Circuit", "Cloud", "Comet",
    "Compass", "Corridor", "Courtyard", "Current", "Delta", "Desert", "Diver", "Dune", "Dynamo", "Engine",
    "Ferry", "Field", "Fountain", "Garden", "Gate", "Glacier", "Greenhouse", "Grove", "Harbour", "Heron",
    "Highway", "Horizon", "Island", "Jetty", "Kite", "Lagoon", "Lantern", "Library", "Lighthouse", "Machine",
    "Meadow", "Mirror", "Monsoon", "Motel", "Motor", "Oasis", "Orbit", "Orchard", "Palace", "Parade",
    "Pavilion", "Pier", "Planet", "Prism", "Quarry", "Radio", "Railway", "Reef", "Ribbon", "Ritual", "River",
    "Rooftop", "Satellite", "Signal", "Skyline", "Station", "Stairway", "Summit", "Swimmer", "Telegraph",
    "Temple", "Theatre", "Thunder", "Tide", "Tower", "Tram", "Tunnel", "Valley", "Voyage", "Waltz", "Wave",
    "Window", "Wires", "Workshop",
    // Empty buildings after hours.
    "Annex", "Atrium", "Basement", "Breakroom", "Carpet", "Ceiling", "Checkpoint", "Concourse", "Cubicle",
    "Doorway", "Elevator", "Escalator", "Exit", "Fixture", "Foyer", "Garage", "Hallway", "Laundromat",
    "Lobby", "Lounge", "Mezzanine", "Office", "Overpass", "Platform", "Stairwell", "Terminal", "Threshold",
    "Turnstile", "Underpass", "Wallpaper",
    // Rainbows, sweets and the sound chips of old consoles.
    "Arpeggio", "Bleep", "Bloop", "Bonus", "Cartridge", "Chip", "Combo", "Confetti", "Console", "Disco",
    "Dungeon", "Galaxy", "Gumdrop", "Highscore", "Hologram", "Jetpack", "Joystick", "Kaleidoscope", "Level",
    "Lollipop", "Overworld", "Popsicle", "Rainbow", "Rocket", "Sprite", "Starfield", "Tracker", "Unicorn",
    "Warp",
};

}  // namespace

std::string song_name(uint32_t seed) {
  // Two independent picks from one seed (a small integer hash).
  uint32_t h = seed * 0x9E3779B1u;
  h ^= h >> 15;
  h *= 0x85EBCA77u;
  h ^= h >> 13;
  const uint32_t a = h % uint32_t(std::size(kFirst));
  h *= 0xC2B2AE3Du;
  h ^= h >> 16;
  uint32_t b = h % uint32_t(std::size(kSecond));
  std::string first = kFirst[a], second = kSecond[b];
  if (first == second) second = kSecond[(b + 1) % std::size(kSecond)];
  return first + " " + second;
}

}  // namespace twill
