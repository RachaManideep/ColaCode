#pragma once

#include <cstddef>
#include <mutex>
#include <string>

struct EditOperation {
    std::string file_id;
    std::size_t base_version = 0;
    std::size_t position = 0;
    std::size_t delete_count = 0;
    std::string insert_text;
};

struct AppliedEdit {
    EditOperation operation;
    std::size_t version = 0;
    std::string content;
};

class Document {
public:
    Document(
        std::string file_id,
        std::string initial_content = {},
        std::size_t initial_version = 0);

    const std::string& file_id() const;
    std::size_t version() const;
    std::string content() const;

    bool apply(
        const EditOperation& operation,
        AppliedEdit& result);

private:
    std::string file_id_;
    std::string content_;
    std::size_t version_ = 0;
    mutable std::mutex mutex_;
};
