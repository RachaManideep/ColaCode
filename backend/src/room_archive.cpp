#include "room_archive.hpp"

#include <cstdint>
#include <map>
#include <sstream>
#include <vector>

namespace {

void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}

std::uint32_t crc32(const std::string& data) {
    std::uint32_t crc = 0xffffffffu;
    for (unsigned char c : data) {
        crc ^= c;
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xedb88320u & (-(static_cast<int>(crc & 1))));
    }
    return ~crc;
}

std::string base64(const std::vector<std::uint8_t>& data) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const std::uint32_t a = data[i];
        const std::uint32_t b = i + 1 < data.size() ? data[i + 1] : 0;
        const std::uint32_t c = i + 2 < data.size() ? data[i + 2] : 0;
        const std::uint32_t n = (a << 16) | (b << 8) | c;
        out.push_back(table[(n >> 18) & 63]);
        out.push_back(table[(n >> 12) & 63]);
        out.push_back(i + 1 < data.size() ? table[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < data.size() ? table[n & 63] : '=');
    }
    return out;
}

std::string path_for(const std::map<std::string, WorkspaceNode>& by_id,
                     const WorkspaceNode& node) {
    std::vector<std::string> parts;
    WorkspaceNode current = node;
    while (true) {
        parts.push_back(current.name);
        if (current.parent_id.empty()) break;
        auto it = by_id.find(current.parent_id);
        if (it == by_id.end()) break;
        current = it->second;
    }
    std::string path;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!path.empty()) path += '/';
        path += *it;
    }
    return path;
}

} // namespace

std::string make_room_zip_base64(const Workspace& workspace) {
    const auto nodes = workspace.nodes();
    std::map<std::string, WorkspaceNode> by_id;
    for (const auto& node : nodes) by_id[node.id] = node;

    struct Entry { std::string name; std::string data; bool directory; std::uint32_t crc; std::uint32_t offset; };
    std::vector<Entry> entries;

    for (const auto& node : nodes) {
        std::string name = path_for(by_id, node);
        if (node.kind == "folder") {
            if (name.back() != '/') name += '/';
            entries.push_back({name, {}, true, 0, 0});
        } else {
            std::string content;
            if (node.document) content = node.document->content();
            entries.push_back({name, content, false, crc32(content), 0});
        }
    }

    std::vector<std::uint8_t> zip;
    for (auto& e : entries) {
        e.offset = static_cast<std::uint32_t>(zip.size());
        put32(zip, 0x04034b50); // local file header
        put16(zip, 20);          // version
        put16(zip, 0);           // flags
        put16(zip, 0);           // stored, no compression
        put16(zip, 0); put16(zip, 0);
        put32(zip, e.crc);
        put32(zip, static_cast<std::uint32_t>(e.data.size()));
        put32(zip, static_cast<std::uint32_t>(e.data.size()));
        put16(zip, static_cast<std::uint16_t>(e.name.size()));
        put16(zip, 0);
        zip.insert(zip.end(), e.name.begin(), e.name.end());
        zip.insert(zip.end(), e.data.begin(), e.data.end());
    }

    const auto central_offset = static_cast<std::uint32_t>(zip.size());
    for (const auto& e : entries) {
        put32(zip, 0x02014b50);
        put16(zip, 20); put16(zip, 20);
        put16(zip, 0); put16(zip, 0); put16(zip, 0); put16(zip, 0);
        put32(zip, e.crc);
        put32(zip, static_cast<std::uint32_t>(e.data.size()));
        put32(zip, static_cast<std::uint32_t>(e.data.size()));
        put16(zip, static_cast<std::uint16_t>(e.name.size()));
        put16(zip, 0); put16(zip, 0); put16(zip, 0); put16(zip, 0);
        put32(zip, e.directory ? 0x10u : 0u);
        put32(zip, e.offset);
        zip.insert(zip.end(), e.name.begin(), e.name.end());
    }

    const auto central_size = static_cast<std::uint32_t>(zip.size()) - central_offset;
    put32(zip, 0x06054b50);
    put16(zip, 0); put16(zip, 0);
    put16(zip, static_cast<std::uint16_t>(entries.size()));
    put16(zip, static_cast<std::uint16_t>(entries.size()));
    put32(zip, central_size);
    put32(zip, central_offset);
    put16(zip, 0);

    return base64(zip);
}
