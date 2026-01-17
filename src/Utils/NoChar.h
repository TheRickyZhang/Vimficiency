#pragma once

// Sentinel value for "no character" in boundary contexts.
// Uses DEL (0x7F) which is non-printable and unlikely in real text.
constexpr char NO_CHAR = '\x7f';
