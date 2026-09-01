#include "workspace.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace {

std::string lower_copy(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(
            std::tolower(
                static_cast<unsigned char>(c)));
    }
    return value;
}

} // namespace

Workspace::Workspace(
    std::string room_id,
    std::shared_ptr<Database> database)
    : room_id_(std::move(room_id)),
      database_(std::move(database)) {

    const auto persisted =
        database_->load_nodes(room_id_);

    for (const auto& node : persisted) {

        WorkspaceNode workspace_node;

        workspace_node.id = node.id;
        workspace_node.parent_id = node.parent_id;
        workspace_node.name = node.name;
        workspace_node.kind = node.kind;
        workspace_node.language = node.language;

        if (node.kind == "file") {
            workspace_node.document =
                std::make_shared<Document>(
                    node.id,
                    node.content,
                    node.version);
        }

        nodes_[node.id] =
            std::move(workspace_node);
    }

    if (nodes_.empty()) {

        std::string main_id;

        create(
            "",
            "main.cpp",
            "file",
            main_id);
    }
}

std::vector<WorkspaceNode>
Workspace::nodes() const {

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<WorkspaceNode> result;

    for (const auto& [_, node] : nodes_) {
        result.push_back(node);
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const WorkspaceNode& a,
           const WorkspaceNode& b) {

            if (a.parent_id != b.parent_id)
                return a.parent_id < b.parent_id;

            if (a.kind != b.kind)
                return a.kind == "folder";

            return a.name < b.name;
        });

    return result;
}

bool Workspace::valid_name(
    const std::string& name) const {

    if (name.empty() ||
        name.size() > 128 ||
        name == "." ||
        name == "..") {
        return false;
    }

    if (name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos) {
        return false;
    }

    return true;
}

bool Workspace::parent_exists(
    const std::string& parent_id) const {

    if (parent_id.empty())
        return true;

    auto it = nodes_.find(parent_id);

    return it != nodes_.end() &&
           it->second.kind == "folder";
}

bool Workspace::sibling_name_exists(
    const std::string& parent_id,
    const std::string& name,
    const std::string& ignore_id) const {

    const auto wanted = lower_copy(name);

    for (const auto& [id, node] : nodes_) {

        if (id == ignore_id)
            continue;

        if (node.parent_id == parent_id &&
            lower_copy(node.name) == wanted) {
            return true;
        }
    }

    return false;
}

bool Workspace::is_descendant(
    const std::string& node_id,
    const std::string& possible_ancestor) const {

    std::string current = node_id;

    while (!current.empty()) {

        auto it = nodes_.find(current);

        if (it == nodes_.end())
            return false;

        if (it->second.parent_id ==
            possible_ancestor) {
            return true;
        }

        current = it->second.parent_id;
    }

    return false;
}

std::string Workspace::language_for(
    const std::string& name) const {

    const auto dot =
        name.find_last_of('.');

    if (dot == std::string::npos)
        return "";

    const auto ext =
        lower_copy(
            name.substr(dot + 1));

    if (ext == "cpp" ||
        ext == "cc" ||
        ext == "c" ||
        ext == "h" ||
        ext == "hpp")
        return "cpp";

    if (ext == "js")
        return "javascript";

    if (ext == "ts" ||
        ext == "tsx")
        return "typescript";

    if (ext == "py")
        return "python";

    if (ext == "java")
        return "java";

    if (ext == "json")
        return "json";

    if (ext == "md")
        return "markdown";

    if (ext == "css")
        return "css";

    if (ext == "html")
        return "html";

    return "";
}

std::string Workspace::make_id() {
    // IDs must remain unique across rooms and server restarts.
    // A timestamp + random component avoids the old node-1/node-2
    // collisions when a Room object is recreated.
    static std::mt19937_64 rng(
        std::random_device{}());

    static std::mutex id_mutex;
    std::lock_guard<std::mutex> lock(id_mutex);

    const auto now =
        std::chrono::high_resolution_clock::now()
            .time_since_epoch()
            .count();

    std::ostringstream out;
    out << "node-"
        << std::hex
        << static_cast<unsigned long long>(now)
        << "-"
        << rng();

    return out.str();
}

bool Workspace::create(
    const std::string& parent_id,
    const std::string& name,
    const std::string& kind,
    std::string& created_id) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!valid_name(name))
        return false;

    if (kind != "file" &&
        kind != "folder")
        return false;

    if (!parent_exists(parent_id))
        return false;

    if (sibling_name_exists(
            parent_id,
            name))
        return false;

    const std::string id =
        make_id();

    WorkspaceNode node;

    node.id = id;
    node.parent_id = parent_id;
    node.name = name;
    node.kind = kind;
    node.language =
        kind == "file"
            ? language_for(name)
            : "";

    if (kind == "file") {
        node.document =
            std::make_shared<Document>(
                id);
    }

    PersistedNode persisted;

    persisted.id = id;
    persisted.parent_id = parent_id;
    persisted.name = name;
    persisted.kind = kind;
    persisted.language = node.language;

    database_->create_node(
        room_id_,
        persisted);

    nodes_[id] = node;
    created_id = id;

    return true;
}

bool Workspace::rename(
    const std::string& node_id,
    const std::string& new_name) {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = nodes_.find(node_id);

    if (it == nodes_.end() ||
        !valid_name(new_name)) {
        return false;
    }

    if (sibling_name_exists(
            it->second.parent_id,
            new_name,
            node_id)) {
        return false;
    }

    it->second.name =
        new_name;

    if (it->second.kind == "file") {
        it->second.language =
            language_for(new_name);
    }

    database_->rename_node(
        room_id_,
        node_id,
        new_name);

    return true;
}

bool Workspace::move(
    const std::string& node_id,
    const std::string& new_parent_id) {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = nodes_.find(node_id);

    if (it == nodes_.end())
        return false;

    if (!parent_exists(new_parent_id))
        return false;

    if (node_id == new_parent_id)
        return false;

    if (it->second.kind == "folder" &&
        is_descendant(
            new_parent_id,
            node_id)) {
        return false;
    }

    if (sibling_name_exists(
            new_parent_id,
            it->second.name,
            node_id)) {
        return false;
    }

    it->second.parent_id =
        new_parent_id;

    database_->move_node(
        room_id_,
        node_id,
        new_parent_id);

    return true;
}

bool Workspace::remove(
    const std::string& node_id) {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = nodes_.find(node_id);

    if (it == nodes_.end())
        return false;

    // Protect root main.cpp.
    if (it->second.parent_id.empty() &&
        it->second.name == "main.cpp") {
        return false;
    }

    std::vector<std::string> ids;

    for (const auto& [id, node] : nodes_) {

        std::string current =
            node.parent_id;

        while (!current.empty()) {

            if (current == node_id) {
                ids.push_back(id);
                break;
            }

            auto parent =
                nodes_.find(current);

            if (parent == nodes_.end())
                break;

            current =
                parent->second.parent_id;
        }
    }

    ids.push_back(node_id);

    for (const auto& id : ids) {

        database_->delete_node(
            room_id_,
            id);

        nodes_.erase(id);
    }

    return true;
}

std::shared_ptr<Document>
Workspace::get_document(
    const std::string& node_id) {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = nodes_.find(node_id);

    if (it == nodes_.end() ||
        it->second.kind != "file") {
        return nullptr;
    }

    return it->second.document;
}

bool Workspace::has(
    const std::string& node_id) const {

    std::lock_guard<std::mutex> lock(mutex_);

    return nodes_.find(node_id) !=
           nodes_.end();
}

std::string Workspace::parent_of(
    const std::string& node_id) const {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = nodes_.find(node_id);

    if (it == nodes_.end())
        return "";

    return it->second.parent_id;
}

std::string Workspace::kind_of(
    const std::string& node_id) const {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = nodes_.find(node_id);

    if (it == nodes_.end())
        return "";

    return it->second.kind;
}
