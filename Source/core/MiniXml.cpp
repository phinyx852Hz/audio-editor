// SPDX-License-Identifier: AGPL-3.0-only
#include "MiniXml.h"

#include <cctype>
#include <cstdlib>

namespace minixml
{
const std::string* Node::attribute (const std::string& n) const
{
    for (const auto& [k, v] : attributes)
        if (k == n)
            return &v;
    return nullptr;
}

const Node* Node::child (const std::string& n) const
{
    for (const auto& c : children)
        if (c.name == n)
            return &c;
    return nullptr;
}

std::string Node::localName() const
{
    const auto colon = name.find (':');
    return colon == std::string::npos ? name : name.substr (colon + 1);
}

std::string Node::prefix() const
{
    const auto colon = name.find (':');
    return colon == std::string::npos ? std::string() : name.substr (0, colon);
}

namespace
{
    struct Parser
    {
        const std::string& s;
        size_t i = 0;
        std::string error;

        bool fail (const std::string& what)
        {
            if (error.empty())
                error = what + " at character " + std::to_string (i);
            return false;
        }

        bool startsAt (const char* t) const { return s.compare (i, std::char_traits<char>::length (t), t) == 0; }

        void appendUtf8 (std::string& out, unsigned long cp)
        {
            if (cp < 0x80) out += (char) cp;
            else if (cp < 0x800) { out += (char) (0xc0 | (cp >> 6)); out += (char) (0x80 | (cp & 63)); }
            else if (cp < 0x10000) { out += (char) (0xe0 | (cp >> 12)); out += (char) (0x80 | ((cp >> 6) & 63)); out += (char) (0x80 | (cp & 63)); }
            else { out += (char) (0xf0 | (cp >> 18)); out += (char) (0x80 | ((cp >> 12) & 63)); out += (char) (0x80 | ((cp >> 6) & 63)); out += (char) (0x80 | (cp & 63)); }
        }

        // Text up to `stop`, with entities decoded.
        std::string decode (size_t from, size_t to)
        {
            std::string out;
            for (size_t k = from; k < to; ++k)
            {
                if (s[k] != '&')
                {
                    out += s[k];
                    continue;
                }
                const auto semi = s.find (';', k);
                if (semi == std::string::npos || semi > to)
                {
                    out += '&';
                    continue;
                }
                const auto e = s.substr (k + 1, semi - k - 1);
                if (e == "amp") out += '&';
                else if (e == "lt") out += '<';
                else if (e == "gt") out += '>';
                else if (e == "quot") out += '"';
                else if (e == "apos") out += '\'';
                else if (! e.empty() && e[0] == '#')
                    appendUtf8 (out, e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? std::strtoul (e.c_str() + 2, nullptr, 16)
                                                                                  : std::strtoul (e.c_str() + 1, nullptr, 10));
                else
                    out += "&" + e + ";";
                k = semi;
            }
            return out;
        }

        void space() { while (i < s.size() && std::isspace ((unsigned char) s[i])) ++i; }

        // Skips comments, processing instructions and DOCTYPE; true when it skipped one.
        bool skipMisc()
        {
            size_t close = std::string::npos;
            if (startsAt ("<!--"))
                close = s.find ("-->", i), close = close == std::string::npos ? close : close + 3;
            else if (startsAt ("<?"))
                close = s.find ("?>", i), close = close == std::string::npos ? close : close + 2;
            else if (startsAt ("<!DOCTYPE"))
                close = s.find ('>', i), close = close == std::string::npos ? close : close + 1;
            else
                return false;
            i = close == std::string::npos ? s.size() : close;
            return true;
        }

        std::string name()
        {
            const auto start = i;
            while (i < s.size() && ! std::isspace ((unsigned char) s[i]) && s[i] != '>' && s[i] != '/' && s[i] != '=')
                ++i;
            return s.substr (start, i - start);
        }

        bool element (Node& node)
        {
            node.begin = i;
            if (i >= s.size() || s[i] != '<')
                return fail ("Expected an element");
            ++i;
            node.name = name();
            if (node.name.empty())
                return fail ("Expected an element name");

            for (;;)
            {
                space();
                if (i >= s.size())
                    return fail ("Unfinished element");
                if (s[i] == '/')
                {
                    if (i + 1 >= s.size() || s[i + 1] != '>')
                        return fail ("Expected '>'");
                    i += 2;
                    node.end = i;
                    return true;
                }
                if (s[i] == '>')
                {
                    ++i;
                    break;
                }
                auto key = name();
                space();
                if (i >= s.size() || s[i] != '=')
                    return fail ("Expected '=' after " + key);
                ++i;
                space();
                if (i >= s.size() || (s[i] != '"' && s[i] != '\''))
                    return fail ("Expected a quoted value");
                const auto quote = s[i++];
                const auto close = s.find (quote, i);
                if (close == std::string::npos)
                    return fail ("Unfinished attribute");
                node.attributes.emplace_back (std::move (key), decode (i, close));
                i = close + 1;
            }

            // Content
            for (;;)
            {
                const auto lt = s.find ('<', i);
                if (lt == std::string::npos)
                    return fail ("Missing </" + node.name + ">");
                node.text += decode (i, lt);
                i = lt;
                if (startsAt ("<![CDATA["))
                {
                    const auto close = s.find ("]]>", i);
                    if (close == std::string::npos)
                        return fail ("Unfinished CDATA");
                    node.text += s.substr (i + 9, close - i - 9);
                    i = close + 3;
                    continue;
                }
                if (skipMisc())
                    continue;
                if (startsAt ("</"))
                {
                    i += 2;
                    const auto closing = name();
                    space();
                    if (closing != node.name || i >= s.size() || s[i] != '>')
                        return fail ("Expected </" + node.name + ">");
                    ++i;
                    node.end = i;
                    return true;
                }
                Node child;
                if (! element (child))
                    return false;
                node.children.push_back (std::move (child));
            }
        }
    };
}

bool parse (const std::string& text, Node& root, std::string& error)
{
    Parser p { text };
    // Skip a byte order mark and anything before the root.
    if (text.compare (0, 3, "\xEF\xBB\xBF") == 0)
        p.i = 3;
    for (;;)
    {
        p.space();
        if (p.i >= text.size())
        {
            error = "No XML element found";
            return false;
        }
        if (! p.skipMisc())
            break;
    }
    if (! p.element (root))
    {
        error = p.error;
        return false;
    }
    return true;
}

std::string escape (const std::string& s, bool attribute)
{
    std::string out;
    out.reserve (s.size());
    for (const auto ch : s)
    {
        switch (ch)
        {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += attribute ? "&quot;" : "\""; break;
            case '\n': out += attribute ? "&#xA;" : "\n"; break;
            case '\r': out += "&#xD;"; break;
            default: out += ch;
        }
    }
    return out;
}
}
