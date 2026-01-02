#pragma once

#include <string>
#include "nebulastore/common/types.h"
#include "nebulastore/raft/raft_types.h"

namespace nebulastore {
namespace raft {

// ================================
// Apply Result
// ================================
struct ApplyResult {
    Status status;
    std::string response;
};

// ================================
// State Machine Interface
// ================================
class StateMachine {
public:
    virtual ~StateMachine() = default;

    // Apply a log entry to the state machine
    virtual ApplyResult Apply(const LogEntry& entry) = 0;

    // Create a snapshot of the current state
    virtual Status Snapshot(std::string* data) = 0;

    // Restore state from a snapshot
    virtual Status Restore(const std::string& data) = 0;

    // Get the last applied log index
    virtual LogIndex LastAppliedIndex() const = 0;
};

} // namespace raft
} // namespace nebulastore
