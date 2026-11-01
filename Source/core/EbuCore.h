// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioDocument.h"

#include <string>

// EBUCore (EBU Tech 3293) in a WAV "axml" chunk (EBU Tech 3285 Supplement 5),
// to and from Tags. PHI reads and writes the descriptive core: title,
// creators by role, contributors, publisher, subjects, description, dates,
// genre, ISRC, rights and language. Everything else in the document (other
// identifiers, formats, the Audio Definition Model of BS.2076...) is kept as
// it was written, and an element PHI reads is only written again when its
// value changed. Plain C++.
namespace ebucore
{
    bool read (const std::string& xml, Tags&, std::string& error);

    // The chunk's XML for the EBUCore values in tags, merged into `original`
    // (the file's axml chunk, or empty). Empty when there is nothing to write.
    std::string write (const Tags&, const std::string& original = {});
}
