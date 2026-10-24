// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>
#include <utility>
#include <vector>

// A small XML reader and writer for the metadata PHI keeps as XML (XMP
// packets, iXML chunks). Elements, attributes, text, entities and CDATA;
// comments, processing instructions and DOCTYPE are skipped. Plain C++.
namespace minixml
{
    struct Node
    {
        std::string name;                                        // as written, with its prefix
        std::vector<std::pair<std::string, std::string>> attributes;
        std::vector<Node> children;
        std::string text;                                        // the text directly inside
        size_t begin = 0, end = 0;                               // where it is in the source

        const std::string* attribute (const std::string& name) const;
        const Node* child (const std::string& name) const;
        std::string localName() const;
        std::string prefix() const;
    };

    // The document's root element; false with `error` set when it is not well formed.
    bool parse (const std::string& text, Node& root, std::string& error);

    std::string escape (const std::string&, bool attribute = false);
}
