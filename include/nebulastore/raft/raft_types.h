#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <chrono>

namespace nebulastore {
namespace raft {

// ================================
// Raft Node ID
// ================================
using NodeId = uint64_t;
using GroupId = uint64_t;
using Term = uint64_t;
using LogIndex = uint64_t;

// ================================
// Raft Role
// ================================
enum class RaftRole : uint8_t {
    kFollower = 0,
    kCandidate = 1,
    kLeader = 2,
};

// ================================
// Command Type
// ================================
enum class CmdType : uint32_t {
    kNoop = 0,
    kCreateInode = 1,
    kDeleteInode = 2,
    kUpdateInode = 3,
    kCreateDentry = 4,
    kDeleteDentry = 5,
    kUpdateDentry = 6,
};

// ================================
// Log Entry
// ================================
struct LogEntry {
    LogIndex index;
    Term term;
    CmdType type;
    std::string command;
};

// ================================
// Persistent State (on stable storage)
// ================================
struct PersistentState {
    Term current_term = 0;
    NodeId voted_for = 0;
};

// ================================
// Volatile State (on all servers)
// ================================
struct VolatileState {
    LogIndex commit_index = 0;
    LogIndex last_applied = 0;
};

// ================================
// Leader State (volatile, on leaders)
// ================================
struct LeaderState {
    std::map<NodeId, LogIndex> next_index;
    std::map<NodeId, LogIndex> match_index;
};

// ================================
// Raft Configuration
// ================================
struct RaftConfig {
    GroupId group_id;
    NodeId node_id;
    std::vector<NodeId> peers;

    // Timeout configuration (milliseconds)
    uint32_t election_timeout_min_ms = 150;
    uint32_t election_timeout_max_ms = 300;
    uint32_t heartbeat_interval_ms = 50;
};

} // namespace raft
} // namespace nebulastore
