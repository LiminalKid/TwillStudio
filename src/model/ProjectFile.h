// Project files (design §8): a small chunked binary format, written to and read from memory so each
// platform decides where the bytes go.
//
//   "TWIL" u32 format-version ("FLUO" before the rename, still read), then chunks: 4-byte tag,
//   u32 payload size, payload.
//   PROJ  project settings          TRAK  one per track, in order (with its arrangement)
//   CLIP  one per clip (with its notes)
//
// Every chunk payload starts with its own u16 version. Readers skip chunks they don't know, and
// ignore bytes past the fields they know at the end of a chunk, so newer files still open in older
// builds as far as they can, and older files always open in newer ones.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "model/Project.h"

namespace twill {

std::vector<uint8_t> save_project(const Project& p);
// On failure returns false, leaves `out` untouched and puts a reason in `error`.
bool load_project(const uint8_t* data, size_t size, Project& out, std::string& error);

}  // namespace twill
