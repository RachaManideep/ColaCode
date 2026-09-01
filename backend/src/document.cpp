#include "document.hpp"

Document::Document(
    std::string file_id,
    std::string initial_content,
    std::size_t initial_version)
    : file_id_(std::move(file_id)),
      content_(std::move(initial_content)),
      version_(initial_version) {
}

const std::string& Document::file_id() const {
    return file_id_;
}

std::size_t Document::version() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return version_;
}

std::string Document::content() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return content_;
}

bool Document::apply(
    const EditOperation& operation,
    AppliedEdit& result) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (operation.file_id != file_id_) return false;
    if (operation.position > content_.size()) return false;

    if (operation.delete_count >
        content_.size() - operation.position) {
        return false;
    }

    content_.replace(
        operation.position,
        operation.delete_count,
        operation.insert_text);

    ++version_;

    result.operation = operation;
    result.version = version_;
    result.content = content_;

    return true;
}
