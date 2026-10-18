// SPDX-License-Identifier: AGPL-3.0-only
#include "Ixml.h"

#include "Metadata.h"
#include "MiniXml.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>

namespace ixml
{
namespace
{
    // Elements that repeat, numbered even when there is only one.
    const std::set<std::string> repeating { "TRACK", "SYNC_POINT", "FILE", "EVENT", "ASWG_ITEM" };

    void flatten (const minixml::Node& n, const std::string& path, std::vector<std::pair<std::string, std::string>>& out)
    {
        std::map<std::string, int> count, seen;
        for (const auto& c : n.children)
            ++count[c.name];

        for (const auto& c : n.children)
        {
            auto name = c.name;
            if (count[c.name] > 1 || repeating.count (c.name) > 0)
                name += "[" + std::to_string (++seen[c.name]) + "]";
            const auto childPath = path.empty() ? name : path + "/" + name;
            if (c.children.empty())
                out.emplace_back (childPath, c.text);
            else
                flatten (c, childPath, out);
        }
    }

    struct Element
    {
        std::string name, text;
        std::vector<std::unique_ptr<Element>> children;

        Element* child (const std::string& n, int index)
        {
            int k = 0;
            for (auto& c : children)
                if (c->name == n && ++k == index)
                    return c.get();
            Element* last = nullptr;
            while (k < index)
            {
                children.push_back (std::make_unique<Element>());
                children.back()->name = n;
                last = children.back().get();
                ++k;
            }
            return last;
        }

        void write (std::string& out, int depth) const
        {
            const std::string indent ((size_t) depth * 2, ' ');
            out += indent + "<" + name + ">";
            if (children.empty())
                out += minixml::escape (text);
            else
            {
                out += "\n";
                for (const auto& c : children)
                    c->write (out, depth + 1);
                out += indent;
            }
            out += "</" + name + ">\n";
        }
    };
}

bool read (const std::string& xml, Tags& tags, std::string& error)
{
    minixml::Node root;
    if (! minixml::parse (xml, root, error))
        return false;
    if (root.name != "BWFXML")
    {
        error = "Not iXML (no BWFXML element)";
        return false;
    }
    std::vector<std::pair<std::string, std::string>> values;
    flatten (root, {}, values);
    std::erase_if (values, [] (const auto& v) { return v.first == "IXML_VERSION"; });
    meta::absorb (tags, meta::Schema::ixml, values);
    return true;
}

std::string write (const Tags& tags)
{
    const auto values = meta::valuesFor (tags, meta::Schema::ixml);
    if (values.empty())
        return {};

    Element root;
    root.name = "BWFXML";
    root.child ("IXML_VERSION", 1)->text = "2.10";
    for (const auto& [path, value] : values)
    {
        Element* e = &root;
        size_t start = 0;
        while (start <= path.size())
        {
            const auto slash = path.find ('/', start);
            auto part = path.substr (start, slash == std::string::npos ? std::string::npos : slash - start);
            int index = 1;
            if (const auto open = part.find ('['); open != std::string::npos)
            {
                index = std::max (1, std::atoi (part.c_str() + open + 1));
                part.resize (open);
            }
            e = e->child (part, index);
            if (slash == std::string::npos)
                break;
            start = slash + 1;
        }
        e->text = value;
    }

    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    root.write (out, 0);
    return out;
}
}
