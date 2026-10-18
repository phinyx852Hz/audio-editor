// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioDocument.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

// XMP packets (RDF/XML) to and from Tags. Simple properties, language
// alternatives (x-default) and lists of plain values become "xmp:<prefix>:<name>"
// keys; structures (xmpMM:History, xmpDM:Tracks with Audition's markers...) are
// kept as their XML ("xmpraw:..."), so they are written back unchanged. Plain C++.
namespace xmp
{
    struct Packet
    {
        std::vector<std::pair<std::string, std::string>> values;   // "dc:title" -> text (lists: one item per line)
        std::map<std::string, std::string> raw;                    // "xmpDM:Tracks" -> its XML
        std::map<std::string, std::string> namespaces;             // prefix -> URI
    };

    bool parse (const std::string& xml, Packet&, std::string& error);
    // Padding leaves room to edit the packet in place (WAV, AIFF).
    std::string serialize (const Packet&, const std::string& toolkit, bool padding = true);

    // The URI of a namespace PHINYX knows, or empty.
    std::string uriOf (const std::string& prefix);

    // Reads a packet into tags (see meta::absorb). False when it is not XMP.
    bool read (const std::string& xml, Tags&, std::string& error);
    // The packet for the XMP values in tags; empty when there are none.
    std::string write (const Tags&, const std::string& toolkit, bool padding = true);
}
