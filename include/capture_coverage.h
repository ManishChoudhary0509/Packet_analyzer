#ifndef CAPTURE_COVERAGE_H
#define CAPTURE_COVERAGE_H

#include <cstddef>
#include <cstdint>

namespace DPI {

// Owned by the reader; inspected after input processing finishes.
struct CaptureCoverage {
    uint64_t records_read = 0;
    uint64_t captured_bytes = 0;
    uint64_t eligible_packets = 0;
    uint64_t skipped_ipv6 = 0;
    uint64_t skipped_arp = 0;
    uint64_t skipped_other_ethernet = 0;
    uint64_t skipped_ipv4_other_transport = 0;
    uint64_t parser_rejected = 0;

    void recordRead(size_t captured_length) {
        ++records_read;
        captured_bytes += captured_length;
    }

    uint64_t skippedPackets() const {
        return skipped_ipv6 + skipped_arp + skipped_other_ethernet +
               skipped_ipv4_other_transport + parser_rejected;
    }

    bool accountingMatches() const {
        return records_read == eligible_packets + skippedPackets();
    }

    double coveragePercent() const {
        return records_read == 0 ? 0.0 : 100.0 * eligible_packets / records_read;
    }
};

} // namespace DPI

#endif // CAPTURE_COVERAGE_H
