// SPDX-License-Identifier: AGPL-3.0-only
#include "Xmp.h"

#include "Metadata.h"
#include "MiniXml.h"

#include <set>

namespace xmp
{
namespace
{
    const std::pair<const char*, const char*> known[] {
        { "dc", "http://purl.org/dc/elements/1.1/" },
        { "xmp", "http://ns.adobe.com/xap/1.0/" },
        { "xmpRights", "http://ns.adobe.com/xap/1.0/rights/" },
        { "xmpMM", "http://ns.adobe.com/xap/1.0/mm/" },
        { "xmpBJ", "http://ns.adobe.com/xap/1.0/bj/" },
        { "xmpTPg", "http://ns.adobe.com/xap/1.0/t/pg/" },
        { "xmpDM", "http://ns.adobe.com/xmp/1.0/DynamicMedia/" },
        { "xmpScript", "http://ns.adobe.com/xmp/1.0/Script/" },
        { "pdf", "http://ns.adobe.com/pdf/1.3/" },
        { "photoshop", "http://ns.adobe.com/photoshop/1.0/" },
        { "crs", "http://ns.adobe.com/camera-raw-settings/1.0/" },
        { "tiff", "http://ns.adobe.com/tiff/1.0/" },
        { "exif", "http://ns.adobe.com/exif/1.0/" },
        { "aux", "http://ns.adobe.com/exif/1.0/aux/" },
        { "Iptc4xmpCore", "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/" },
        { "Iptc4xmpExt", "http://iptc.org/std/Iptc4xmpExt/2008-02-29/" },
        { "stEvt", "http://ns.adobe.com/xap/1.0/sType/ResourceEvent#" },
        { "stRef", "http://ns.adobe.com/xap/1.0/sType/ResourceRef#" },
        { "stDim", "http://ns.adobe.com/xap/1.0/sType/Dimensions#" },
        { "stJob", "http://ns.adobe.com/xap/1.0/sType/Job#" },
        { "xmpG", "http://ns.adobe.com/xap/1.0/g/" },
        { "xmpGImg", "http://ns.adobe.com/xap/1.0/g/img/" },
    };

    const std::string rdfUri = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";

    std::string prefixForUri (const std::string& uri)
    {
        for (const auto& [p, u] : known)
            if (uri == u)
                return p;
        return {};
    }

    void collectNamespaces (const minixml::Node& n, std::map<std::string, std::string>& out)
    {
        for (const auto& [k, v] : n.attributes)
            if (k.compare (0, 6, "xmlns:") == 0)
                out.emplace (k.substr (6), v);
        for (const auto& c : n.children)
            collectNamespaces (c, out);
    }

    const minixml::Node* findRdf (const minixml::Node& n)
    {
        if (n.localName() == "RDF")
            return &n;
        for (const auto& c : n.children)
            if (const auto* r = findRdf (c))
                return r;
        return nullptr;
    }

    // The text of a property, or false when it is a structure.
    bool plainValue (const minixml::Node& prop, std::string& out)
    {
        if (const auto* res = prop.attribute ("rdf:resource"))
            return out = *res, true;
        if (const auto* type = prop.attribute ("rdf:parseType"); type != nullptr && *type == "Resource")
            return false;
        for (const auto& [k, v] : prop.attributes)
            if (k.find (':') != std::string::npos && k.compare (0, 4, "rdf:") != 0 && k.compare (0, 4, "xml:") != 0
                && k.compare (0, 6, "xmlns:") != 0)
                return false;   // shorthand structure
        if (prop.children.empty())
            return out = prop.text, true;
        if (prop.children.size() != 1)
            return false;

        const auto& container = prop.children[0];
        const auto kind = container.name;
        if (kind != "rdf:Alt" && kind != "rdf:Bag" && kind != "rdf:Seq")
            return false;

        std::vector<std::string> items;
        std::string fallback, chosen;
        bool haveDefault = false;
        for (const auto& li : container.children)
        {
            if (li.name != "rdf:li" || ! li.children.empty())
                return false;
            for (const auto& [k, v] : li.attributes)
                if (k != "xml:lang" && k.compare (0, 6, "xmlns:") != 0)
                    return false;
            if (kind == "rdf:Alt")
            {
                const auto* lang = li.attribute ("xml:lang");
                if (lang != nullptr && *lang == "x-default")
                    chosen = li.text, haveDefault = true;
                else if (fallback.empty())
                    fallback = li.text;
                if (container.children.size() > 1 && (lang == nullptr || *lang != "x-default"))
                    return false;   // other languages: keep the whole alternative
            }
            else
                items.push_back (li.text);
        }

        if (kind == "rdf:Alt")
            out = haveDefault ? chosen : fallback;
        else
        {
            out.clear();
            for (size_t k = 0; k < items.size(); ++k)
                out += (k > 0 ? "\n" : "") + items[k];
        }
        return true;
    }
}

std::string uriOf (const std::string& prefix)
{
    for (const auto& [p, u] : known)
        if (prefix == p)
            return u;
    return {};
}

bool parse (const std::string& xml, Packet& packet, std::string& error)
{
    minixml::Node root;
    if (! minixml::parse (xml, root, error))
        return false;
    const auto* rdf = findRdf (root);
    if (rdf == nullptr)
    {
        error = "No rdf:RDF element: not an XMP packet";
        return false;
    }

    std::map<std::string, std::string> declared;
    collectNamespaces (root, declared);

    // A property's prefix as PHINYX names it: the known prefix for its URI, else the file's.
    const auto canonical = [&] (const std::string& filePrefix)
    {
        const auto it = declared.find (filePrefix);
        if (it == declared.end())
            return filePrefix;
        const auto p = prefixForUri (it->second);
        return p.empty() ? filePrefix : p;
    };

    for (const auto& [p, uri] : declared)
        if (uri != rdfUri && p != "x" && p != "rdf")
            packet.namespaces[p] = uri;

    for (const auto& description : rdf->children)
    {
        if (description.localName() != "Description")
            continue;

        for (const auto& [k, v] : description.attributes)
        {
            const auto colon = k.find (':');
            if (colon == std::string::npos || k.compare (0, 6, "xmlns:") == 0 || k.compare (0, 4, "rdf:") == 0
                || k.compare (0, 4, "xml:") == 0)
                continue;
            packet.values.emplace_back (canonical (k.substr (0, colon)) + k.substr (colon), v);
        }

        for (const auto& prop : description.children)
        {
            const auto id = canonical (prop.prefix()) + ":" + prop.localName();
            std::string value;
            if (plainValue (prop, value))
                packet.values.emplace_back (id, value);
            else
            {
                // Kept as written, with the file's own prefix in it.
                auto fragment = xml.substr (prop.begin, prop.end - prop.begin);
                packet.raw[prop.prefix() + ":" + prop.localName()] = std::move (fragment);
            }
        }
    }
    return true;
}

std::string serialize (const Packet& packet, const std::string& toolkit, bool padding)
{
    std::set<std::string> used;
    for (const auto& [id, v] : packet.values)
        used.insert (id.substr (0, id.find (':')));
    for (const auto& [id, v] : packet.raw)
        used.insert (id.substr (0, id.find (':')));

    std::map<std::string, std::string> declare;
    for (const auto& p : used)
    {
        auto uri = uriOf (p);
        if (const auto it = packet.namespaces.find (p); uri.empty() && it != packet.namespaces.end())
            uri = it->second;
        if (! uri.empty())
            declare[p] = uri;
    }
    // Structures use further prefixes (stEvt, xmpDM...): declare every one the file had.
    if (! packet.raw.empty())
    {
        for (const auto& [p, uri] : packet.namespaces)
            declare.emplace (p, uri);
        for (const auto& [p, uri] : known)
            for (const auto& [id, fragment] : packet.raw)
                if (fragment.find (std::string ("<") + p + ":") != std::string::npos
                    || fragment.find (std::string (" ") + p + ":") != std::string::npos)
                    declare.emplace (p, uri);
    }

    std::string out = "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
                      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"" + minixml::escape (toolkit, true) + "\">\n"
                      " <rdf:RDF xmlns:rdf=\"" + rdfUri + "\">\n"
                      "  <rdf:Description rdf:about=\"\"";
    for (const auto& [p, uri] : declare)
        out += "\n    xmlns:" + p + "=\"" + minixml::escape (uri, true) + "\"";
    out += ">\n";

    for (const auto& [id, value] : packet.values)
    {
        const auto* f = meta::findId (meta::Schema::xmp, id);
        const auto type = f != nullptr ? f->xmpType : meta::XmpType::simple;
        out += "   <" + id + ">";
        switch (type)
        {
            case meta::XmpType::simple:
                out += minixml::escape (value);
                break;
            case meta::XmpType::langAlt:
                out += "\n    <rdf:Alt>\n     <rdf:li xml:lang=\"x-default\">" + minixml::escape (value) + "</rdf:li>\n    </rdf:Alt>\n   ";
                break;
            case meta::XmpType::bag:
            case meta::XmpType::seq:
            {
                const auto container = type == meta::XmpType::bag ? "rdf:Bag" : "rdf:Seq";
                out += std::string ("\n    <") + container + ">\n";
                size_t start = 0;
                for (;;)
                {
                    const auto nl = value.find ('\n', start);
                    const auto item = value.substr (start, nl == std::string::npos ? std::string::npos : nl - start);
                    if (! item.empty())
                        out += "     <rdf:li>" + minixml::escape (item) + "</rdf:li>\n";
                    if (nl == std::string::npos)
                        break;
                    start = nl + 1;
                }
                out += std::string ("    </") + container + ">\n   ";
                break;
            }
        }
        out += "</" + id + ">\n";
    }
    for (const auto& [id, fragment] : packet.raw)
        out += "   " + fragment + "\n";

    out += "  </rdf:Description>\n </rdf:RDF>\n</x:xmpmeta>\n";
    // Room to edit the packet in place, as the XMP specification suggests.
    for (int line = 0; padding && line < 20; ++line)
        out += std::string (99, ' ') + "\n";
    out += "<?xpacket end=\"w\"?>";
    return out;
}

bool read (const std::string& xml, Tags& tags, std::string& error)
{
    Packet packet;
    if (! parse (xml, packet, error))
        return false;
    meta::absorb (tags, meta::Schema::xmp, packet.values);
    for (const auto& [id, fragment] : packet.raw)
        tags["xmpraw:" + id] = fragment;
    for (const auto& [p, uri] : packet.namespaces)
        if (uriOf (p) != uri)
            tags["xmpns:" + p] = uri;
    return true;
}

std::string write (const Tags& tags, const std::string& toolkit, bool padding)
{
    Packet packet;
    packet.values = meta::valuesFor (tags, meta::Schema::xmp);
    for (auto it = tags.lower_bound ("xmpraw:"); it != tags.end() && it->first.compare (0, 7, "xmpraw:") == 0; ++it)
        packet.raw[it->first.substr (7)] = it->second;
    for (auto it = tags.lower_bound ("xmpns:"); it != tags.end() && it->first.compare (0, 6, "xmpns:") == 0; ++it)
        packet.namespaces[it->first.substr (6)] = it->second;
    if (packet.values.empty() && packet.raw.empty())
        return {};
    return serialize (packet, toolkit, padding);
}
}
