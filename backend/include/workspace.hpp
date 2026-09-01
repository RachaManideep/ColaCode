#pragma once

#include "database.hpp"
#include "document.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct WorkspaceNode {
    std::string id;
    std::string parent_id;
    std::string name;
    std::string kind;
    std::string language;
    std::shared_ptr<Document> document;
};

class Workspace {
public:
    Workspace(
        std::string room_id,
        std::shared_ptr<Database> database);

    std::vector<WorkspaceNode> nodes() const;

    bool create(
        const std::string& parent_id,
        const std::string& name,
        const std::string& kind,
        std::string& created_id);

    bool rename(
        const std::string& node_id,
        const std::string& new_name);

    bool move(
        const std::string& node_id,
        const std::string& new_parent_id);

    bool remove(
        const std::string& node_id);

    std::shared_ptr<Document> get_document(
        const std::string& node_id);

    bool has(
        const std::string& node_id) const;

    std::string parent_of(
        const std::string& node_id) const;

    std::string kind_of(
        const std::string& node_id) const;

private:
    bool valid_name(
        const std::string& name) const;

    bool parent_exists(
        const std::string& parent_id) const;

    bool sibling_name_exists(
        const std::string& parent_id,
        const std::string& name,
        const std::string& ignore_id = {}) const;

    bool is_descendant(
        const std::string& node_id,
        const std::string& possible_ancestor) const;

    std::string language_for(
        const std::string& name) const;

    std::string make_id();

    std::string room_id_;
    std::shared_ptr<Database> database_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, WorkspaceNode> nodes_;

};
