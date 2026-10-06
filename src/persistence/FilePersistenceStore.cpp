// Persistence infrastructure: file-backed store implementation.

#include "persistence/FilePersistenceStore.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace aura {

namespace {

constexpr char kSep = '\x1f';
constexpr char kLine = '\n';

std::string escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (c == '\\') out += "\\\\";
        else if (c == kSep) out += "\\u";
        else if (c == kLine) out += "\\n";
        else if (c == '\r') out += "\\r";
        else out.push_back(c);
    }
    return out;
}

std::string unescape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            const char n = in[++i];
            if (n == 'n') out.push_back(kLine);
            else if (n == 'r') out.push_back('\r');
            else if (n == 'u') out.push_back(kSep);
            else out.push_back(n);
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == kSep) {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(line[i]);
        }
    }
    fields.push_back(current);
    return fields;
}

std::string encodeRecord(const std::string& key,
                         const PersistenceRecordMetadata& metadata,
                         const std::string& payload) {
    std::ostringstream out;
    out << escape(key) << kSep
        << escape(metadata.recordType) << kSep
        << metadata.schemaVersion.toString() << kSep
        << metadata.sequence << kSep
        << metadata.createdAt.epochMillis() << kSep
        << metadata.updatedAt.epochMillis() << kSep
        << escape(metadata.idempotencyKey) << kSep
        << escape(metadata.contentHash.algorithm()) << kSep
        << escape(metadata.contentHash.hex()) << kSep
        << escape(payload);
    return out.str();
}

bool decodeRecord(const std::string& line, std::string& outKey,
                  PersistenceRecordMetadata& outMetadata,
                  std::string& outPayload) {
    const auto fields = splitFields(line);
    if (fields.size() < 10) return false;
    outKey = unescape(fields[0]);
    outMetadata.recordType = unescape(fields[1]);
    std::istringstream schema(fields[2]);
    std::string majorText;
    std::string minorText;
    if (std::getline(schema, majorText, '.') && std::getline(schema, minorText)) {
        try {
            outMetadata.schemaVersion.major = std::stoi(majorText);
            outMetadata.schemaVersion.minor = std::stoi(minorText);
        } catch (...) {
            return false;
        }
    }
    try {
        outMetadata.sequence = std::stoull(fields[3]);
        outMetadata.createdAt = Timestamp::fromEpochMillis(std::stoll(fields[4]));
        outMetadata.updatedAt = Timestamp::fromEpochMillis(std::stoll(fields[5]));
    } catch (...) {
        return false;
    }
    outMetadata.idempotencyKey = unescape(fields[6]);
    outMetadata.contentHash =
        HashDigest(unescape(fields[7]), unescape(fields[8]));
    outMetadata.recordId = EntityId(outKey);
    outPayload = unescape(fields[9]);
    return true;
}

}  // namespace

FilePersistenceStore::FilePersistenceStore(std::string rootDirectory,
                                           bool createIfMissing)
    : root_(std::move(rootDirectory)) {
    std::error_code ec;
    if (!root_.empty() && !fs::exists(root_, ec)) {
        if (!createIfMissing) {
            lastError_ = "root directory does not exist: " + root_;
            return;
        }
        fs::create_directories(root_, ec);
        if (ec) {
            lastError_ = "cannot create root directory: " + ec.message();
            return;
        }
    }
    // Probe writability by touching a marker file.
    const fs::path probe = fs::path(root_) / ".aura_store";
    std::ofstream out(probe, std::ios::app);
    if (!out.good()) {
        lastError_ = "root directory is not writable: " + root_;
        return;
    }
    out.close();
    available_ = true;
}

std::string FilePersistenceStore::kvPath(const std::string& collection) const {
    return (fs::path(root_) / (collection + ".kv")).string();
}

std::string FilePersistenceStore::streamPath(const std::string& stream) const {
    return (fs::path(root_) / (stream + ".stream")).string();
}

bool FilePersistenceStore::isAvailable() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_;
}

bool FilePersistenceStore::loadKv(
    const std::string& collection,
    std::map<std::string, StoredRecord>& out) const {
    std::ifstream in(kvPath(collection));
    if (!in.good()) return true;   // absent collection is empty, not an error
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::string key;
        PersistenceRecordMetadata metadata;
        std::string payload;
        if (!decodeRecord(line, key, metadata, payload)) {
            lastError_ = "corrupt record in collection: " + collection;
            return false;
        }
        StoredRecord record;
        record.payload = payload;
        record.metadata = metadata;
        out[key] = record;
    }
    return true;
}

bool FilePersistenceStore::saveKv(
    const std::string& collection,
    const std::map<std::string, StoredRecord>& records) {
    // Write to a temporary file then rename: a crash mid-write never leaves a
    // half-written collection in place.
    const std::string target = kvPath(collection);
    const std::string temp = target + ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out.good()) {
            lastError_ = "cannot open collection for write: " + collection;
            return false;
        }
        for (const auto& kv : records) {
            out << encodeRecord(kv.first, kv.second.metadata, kv.second.payload)
                << kLine;
        }
        out.flush();
        if (!out.good()) {
            lastError_ = "write failed for collection: " + collection;
            return false;
        }
    }
    std::error_code ec;
    fs::rename(temp, target, ec);
    if (ec) {
        lastError_ = "rename failed for collection: " + collection;
        return false;
    }
    return true;
}

PersistenceStatus FilePersistenceStore::put(
    const std::string& collection, const std::string& key,
    const std::string& payload, const PersistenceRecordMetadata& metadata) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!available_) return PersistenceStatus::UNAVAILABLE;

    std::map<std::string, StoredRecord> records;
    if (!loadKv(collection, records)) return PersistenceStatus::CORRUPT;

    auto it = records.find(key);
    if (it != records.end() && it->second.payload == payload) {
        return PersistenceStatus::IDEMPOTENT_NO_OP;
    }

    PersistenceRecordMetadata stored = metadata;
    stored.recordId = EntityId(key);
    if (stored.recordType.empty()) stored.recordType = collection;
    if (stored.createdAt.isUnknown()) stored.createdAt = Timestamp::now();
    if (stored.updatedAt.isUnknown()) stored.updatedAt = stored.createdAt;
    if (it != records.end()) {
        stored.createdAt = it->second.metadata.createdAt;
        stored.sequence = it->second.metadata.sequence;
    } else {
        stored.sequence = static_cast<std::uint64_t>(records.size()) + 1;
    }

    StoredRecord record;
    record.payload = payload;
    record.metadata = stored;
    records[key] = record;
    if (!saveKv(collection, records)) return PersistenceStatus::IO_ERROR;
    return PersistenceStatus::OK;
}

PersistenceStatus FilePersistenceStore::get(
    const std::string& collection, const std::string& key,
    std::string& outPayload, PersistenceRecordMetadata& outMetadata) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!available_) return PersistenceStatus::UNAVAILABLE;

    std::map<std::string, StoredRecord> records;
    if (!loadKv(collection, records)) return PersistenceStatus::CORRUPT;
    auto it = records.find(key);
    if (it == records.end()) return PersistenceStatus::NOT_FOUND;
    outPayload = it->second.payload;
    outMetadata = it->second.metadata;
    return PersistenceStatus::OK;
}

PersistenceStatus FilePersistenceStore::remove(const std::string& collection,
                                               const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!available_) return PersistenceStatus::UNAVAILABLE;

    std::map<std::string, StoredRecord> records;
    if (!loadKv(collection, records)) return PersistenceStatus::CORRUPT;
    auto it = records.find(key);
    if (it == records.end()) return PersistenceStatus::NOT_FOUND;
    records.erase(it);
    if (!saveKv(collection, records)) return PersistenceStatus::IO_ERROR;
    return PersistenceStatus::OK;
}

bool FilePersistenceStore::contains(const std::string& collection,
                                    const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!available_) return false;
    std::map<std::string, StoredRecord> records;
    if (!loadKv(collection, records)) return false;
    return records.count(key) > 0;
}

std::vector<std::string> FilePersistenceStore::keys(
    const std::string& collection) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    if (!available_) return result;
    std::map<std::string, StoredRecord> records;
    if (!loadKv(collection, records)) return result;
    for (const auto& kv : records) result.push_back(kv.first);
    return result;
}

PersistenceStatus FilePersistenceStore::append(
    const std::string& stream, const std::string& payload,
    const PersistenceRecordMetadata& metadata, std::uint64_t& outSequence) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!available_) return PersistenceStatus::UNAVAILABLE;

    std::uint64_t sequence = 0;
    {
        std::ifstream in(streamPath(stream));
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) ++sequence;
        }
    }
    sequence += 1;

    PersistenceRecordMetadata stored = metadata;
    stored.sequence = sequence;
    if (stored.recordType.empty()) stored.recordType = stream;

    std::ofstream out(streamPath(stream), std::ios::app);
    if (!out.good()) {
        lastError_ = "cannot append to stream: " + stream;
        return PersistenceStatus::IO_ERROR;
    }
    out << encodeRecord(std::to_string(sequence), stored, payload) << kLine;
    out.flush();
    if (!out.good()) {
        lastError_ = "append failed for stream: " + stream;
        return PersistenceStatus::IO_ERROR;
    }
    outSequence = sequence;
    return PersistenceStatus::OK;
}

std::vector<std::string> FilePersistenceStore::readStream(
    const std::string& stream) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    if (!available_) return result;
    std::ifstream in(streamPath(stream));
    if (!in.good()) return result;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::string key;
        PersistenceRecordMetadata metadata;
        std::string payload;
        if (decodeRecord(line, key, metadata, payload)) {
            result.push_back(payload);
        }
    }
    return result;
}

std::size_t FilePersistenceStore::streamSize(const std::string& stream) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!available_) return 0;
    std::ifstream in(streamPath(stream));
    if (!in.good()) return 0;
    std::size_t count = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) ++count;
    }
    return count;
}

PersistenceStatus FilePersistenceStore::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!available_) return PersistenceStatus::UNAVAILABLE;
    // Every write above flushes its stream; the durability boundary is already
    // satisfied. Report OK rather than pretending to do more.
    return PersistenceStatus::OK;
}

}  // namespace aura
