#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lvk::metrics {

struct Snapshot {
    int requestsProcessing = -1;
    std::optional<double> predictedTokensPerSecond;
    std::optional<double> tokensPredictedTotal;
};

// Parses the Prometheus text emitted by llama-server /metrics. This is kept
// separate from the network code so the server contract can be tested without
// starting a model.
std::optional<Snapshot> parsePrometheus(std::string_view body);

class LlamaMetrics {
public:
    // Returns "idle", "n/a", or a formatted generation speed.
    // The request is intentionally synchronous because callers run it on the
    // existing background worker, never on the Win32 message loop.
    std::string sample(const std::string& host, unsigned short port, unsigned long processId);

private:
    void reset();

    unsigned long processId_ = 0;
    std::optional<double> previousTokensPredicted_;
    std::chrono::steady_clock::time_point previousSample_{};
};

} // namespace lvk::metrics
