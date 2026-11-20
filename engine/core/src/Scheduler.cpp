#include <bdex/core/Scheduler.hpp>

#include <algorithm>
#include <chrono>

namespace bdex {

std::string_view phaseName(Phase phase) {
    switch (phase) {
        case Phase::BeginFrame: return "BeginFrame";
        case Phase::Input: return "Input";
        case Phase::PreUpdate: return "PreUpdate";
        case Phase::FixedUpdate: return "FixedUpdate";
        case Phase::Update: return "Update";
        case Phase::PostUpdate: return "PostUpdate";
        case Phase::PreRender: return "PreRender";
        case Phase::Render: return "Render";
        case Phase::PostRender: return "PostRender";
        case Phase::EndFrame: return "EndFrame";
        case Phase::Count: break;
    }
    return "?";
}

TaskId Scheduler::add(Phase phase, std::string name, Task task, i32 order) {
    const TaskId id = nextId_++;
    Entry entry{id, order, std::move(name), std::make_shared<const Task>(std::move(task))};
    if (runDepth_ > 0) {
        pendingAdds_.emplace_back(phase, std::move(entry));
    } else {
        insert(phase, std::move(entry));
    }
    return id;
}

void Scheduler::insert(Phase phase, Entry entry) {
    auto& entries = phases_[static_cast<usize>(phase)];
    // After every entry with an order <= ours: stable for equal orders.
    const auto position = std::upper_bound(
        entries.begin(), entries.end(), entry.order,
        [](i32 value, const Entry& existing) { return value < existing.order; });
    entries.insert(position, std::move(entry));
}

void Scheduler::remove(TaskId id) {
    if (std::erase_if(pendingAdds_, [id](const auto& pending) { return pending.second.id == id; }) >
        0) {
        return;
    }
    for (auto& entries : phases_) {
        for (Entry& entry : entries) {
            if (entry.id == id) {
                entry.id = 0;  // tombstone, skipped by run()
                if (runDepth_ == 0) {
                    std::erase_if(entries, [](const Entry& e) { return e.id == 0; });
                } else {
                    needsCompaction_ = true;
                }
                return;
            }
        }
    }
}

void Scheduler::clear() {
    for (auto& entries : phases_) {
        entries.clear();
    }
    pendingAdds_.clear();
    needsCompaction_ = false;
}

void Scheduler::run(Phase phase, const FrameTime& time) {
    auto& entries = phases_[static_cast<usize>(phase)];
    const auto phaseStart = std::chrono::steady_clock::now();
    ++runDepth_;
    // Entries neither move nor grow while tasks run (additions are deferred,
    // removals are tombstones), so indices stay valid.
    for (usize i = 0; i < entries.size(); ++i) {
        if (entries[i].id != 0) {
            // Hold a reference: the task may remove itself.
            const std::shared_ptr<const Task> task = entries[i].task;
            const auto start = std::chrono::steady_clock::now();
            (*task)(time);
            const auto end = std::chrono::steady_clock::now();
            const f64 seconds = std::chrono::duration<f64>(end - start).count();
            Entry& entry = entries[i];
            if (trace_ != nullptr) {
                trace_->record(entry.name, phaseName(phase), start, end);
            }
            entry.lastSeconds = seconds;
            entry.totalSeconds += seconds;
            ++entry.runs;
        }
    }
    --runDepth_;
    if (trace_ != nullptr && !entries.empty()) {
        trace_->record(phaseName(phase), "phase", phaseStart, std::chrono::steady_clock::now());
    }
    if (runDepth_ == 0) {
        settle();
    }
}

void Scheduler::settle() {
    if (needsCompaction_) {
        for (auto& list : phases_) {
            std::erase_if(list, [](const Entry& e) { return e.id == 0; });
        }
        needsCompaction_ = false;
    }
    std::vector<std::pair<Phase, Entry>> pending;
    pending.swap(pendingAdds_);
    for (auto& [phase, entry] : pending) {
        insert(phase, std::move(entry));
    }
}

usize Scheduler::taskCount() const {
    usize count = pendingAdds_.size();
    for (const auto& entries : phases_) {
        count += static_cast<usize>(
            std::count_if(entries.begin(), entries.end(), [](const Entry& e) { return e.id != 0; }));
    }
    return count;
}

std::vector<TaskStats> Scheduler::taskStats() const {
    std::vector<TaskStats> stats;
    for (usize phase = 0; phase < phases_.size(); ++phase) {
        for (const Entry& entry : phases_[phase]) {
            if (entry.id == 0 || entry.runs == 0) {
                continue;
            }
            stats.push_back({entry.name, static_cast<Phase>(phase), entry.lastSeconds * 1000.0,
                             entry.totalSeconds * 1000.0 / static_cast<f64>(entry.runs),
                             entry.runs});
        }
    }
    return stats;
}

void Scheduler::resetStats() {
    for (auto& entries : phases_) {
        for (Entry& entry : entries) {
            entry.lastSeconds = 0.0;
            entry.totalSeconds = 0.0;
            entry.runs = 0;
        }
    }
}

std::vector<std::string> Scheduler::taskNames(Phase phase) const {
    // Execution order, with pending additions where they will be inserted.
    std::vector<const Entry*> ordered;
    for (const Entry& entry : phases_[static_cast<usize>(phase)]) {
        if (entry.id != 0) {
            ordered.push_back(&entry);
        }
    }
    for (const auto& [pendingPhase, entry] : pendingAdds_) {
        if (pendingPhase == phase) {
            const auto position = std::upper_bound(
                ordered.begin(), ordered.end(), entry.order,
                [](i32 value, const Entry* existing) { return value < existing->order; });
            ordered.insert(position, &entry);
        }
    }
    std::vector<std::string> names;
    names.reserve(ordered.size());
    for (const Entry* entry : ordered) {
        names.push_back(entry->name);
    }
    return names;
}

}  // namespace bdex
