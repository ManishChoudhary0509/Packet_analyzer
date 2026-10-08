# Packet Analyzer

A C++17 command-line tool for analyzing and filtering network traffic from saved PCAP files. It parses Ethernet/IPv4 TCP and UDP packets, extracts available hostnames, applies filtering rules, and reports how much of a capture its DPI pipeline can process.

**This is an educational offline analyzer.** Filtering excludes packets from an output capture; it does not block websites in your browser or enforce firewall rules on live traffic.

## Features

- Ethernet, IPv4, TCP, and UDP header parsing.
- Packet-local TLS ClientHello SNI and HTTP Host extraction.
- Heuristic application identification and source-IP, application, and domain filtering.
- DNS requested-domain reporting with query counts.
- Multithreaded processing with bounded producer-consumer queues.
- Capture coverage accounting: complete records, captured bytes, eligible packets, and explicit skip reasons.
- Accounting checks that compare capture categories and worker decisions.
- Filtered PCAP output and terminal reports.

## Requirements

- A C++17 compiler such as GCC/MinGW-w64 with threading support.
- Python 3 only if you want to regenerate the synthetic sample.
- Wireshark is optional for capturing traffic and inspecting PCAP files.

No external C++ packet-capture library is required: the program reads files directly.

## Build and run on Windows

Open the repository root in VS Code, then open a PowerShell terminal. The current directory must contain `src`, `include`, and `test_dpi.pcap`.

```powershell
g++ -std=c++17 -pthread -O2 -I include -o dpi_engine.exe src/dpi_mt.cpp src/pcap_reader.cpp src/packet_parser.cpp src/sni_extractor.cpp src/types.cpp
```

Analyze the included synthetic capture:

```powershell
.\dpi_engine.exe test_dpi.pcap output.pcap
```

Filter packets identified as YouTube:

```powershell
.\dpi_engine.exe test_dpi.pcap youtube_filtered.pcap --block-app YouTube
```

On the included sample, the YouTube demo produces **77 eligible packets, 76 forwarded, and 1 dropped**. Earlier handshake packets can remain because application identification occurs when a readable hostname becomes available.

If report borders display incorrectly, configure UTF-8 and rerun:

```powershell
chcp 65001
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
```

## Build and run on Linux or macOS

```bash
g++ -std=c++17 -pthread -O2 -I include -o dpi_engine src/dpi_mt.cpp src/pcap_reader.cpp src/packet_parser.cpp src/sni_extractor.cpp src/types.cpp
./dpi_engine test_dpi.pcap output.pcap
./dpi_engine test_dpi.pcap youtube_filtered.pcap --block-app YouTube
```

## Command-line options

```text
dpi_engine <input.pcap> <output.pcap> [options]
```

| Option | Behavior |
|---|---|
| `--block-ip <ip>` | Exclude packets from the specified IPv4 source address. |
| `--block-app <app>` | Exclude packets whose directional flow has the specified application label, such as `YouTube`. |
| `--block-domain <text>` | Match the extracted TLS/HTTP hostname using a substring. |
| `--lbs <n>` | Configure load balancer count; use a positive integer. Default: 2. |
| `--fps <n>` | Configure workers per load balancer; use a positive integer. Default: 2. |

Use different input and output filenames. Opening the output for writing can overwrite an existing file.

```powershell
.\dpi_engine.exe test_dpi.pcap filtered.pcap --block-domain youtube
.\dpi_engine.exe test_dpi.pcap filtered.pcap --block-ip 192.168.1.50
```

These options describe the working `src/dpi_mt.cpp` executable. Other entry points in the repository have different capabilities.

## Analyze your own capture

1. Capture traffic on your active Wi-Fi or Ethernet interface with Wireshark.
2. Start recording before opening new connections if you want to capture their TLS handshakes.
3. Stop recording and save as **classic PCAP**, rather than PCAPNG, with Ethernet link type.
4. Run the analyzer using that file:

```powershell
.\dpi_engine.exe my_capture.pcap real_output.pcap
```

Renaming a `.pcapng` extension does not convert its format. The analyzer does not support PCAPNG or nanosecond PCAP timestamps.

Personal captures can reveal network addresses and requested domains. The repository's `.gitignore` excludes captures except the synthetic `test_dpi.pcap` sample.

## Capture coverage and skipped-packet accounting

A capture can contain many packets that the existing DPI pipeline cannot process. The coverage component counts complete records **before** parser rejection or protocol skips, then assigns each to an exclusive category.

A locally validated Wireshark capture produced the following result; the personal capture itself is not included in this repository:

```text
[Capture Coverage]
Complete records read: 31912
Captured packet bytes: 26826815
Eligible IPv4 TCP/UDP packets: 414
Skipped IPv6: 31489
Skipped ARP: 9
Skipped other Ethernet types: 0
Skipped IPv4 other transport: 0
Parser rejected: 0
Total skipped: 31498
DPI coverage: 1.30%
Capture accounting (read = eligible + skipped): PASS
Worker decisions (processed = forwarded + dropped): PASS
Pipeline accounting (eligible = worker processed): PASS
```

| Field | Meaning |
|---|---|
| Complete records read | PCAP records successfully returned by the reader. |
| Captured packet bytes | Sum of stored packet bytes, excluding PCAP file/record headers. |
| Eligible IPv4 TCP/UDP packets | Packets accepted for dispatch into the existing DPI pipeline. |
| Skipped IPv6 / ARP | Frames recognized by EtherType but excluded from that pipeline. |
| Skipped other Ethernet types | Unsupported Ethernet types, including VLAN-tagged traffic. |
| Skipped IPv4 other transport | Parsed IPv4 traffic outside TCP/UDP, such as ICMP. |
| Parser rejected | Records for which the existing parser returned failure. |
| DPI coverage | `100 * eligible packets / complete records read`; zero for empty input. |
| DPI Input Packets / Bytes | Eligible input totals, rather than full-capture totals. |
| Forwarded / Dropped | Worker decisions to retain packets or exclude them by a blocking rule. |

The report checks these relationships:

```text
complete records read = eligible packets + all skip categories
worker processed = forwarded + dropped
eligible packets = worker processed
```

Unsupported packets are skipped separately from rule-based drops. They are absent from the output even when `Dropped` is zero. Application percentages use eligible DPI input packets. Eligibility does not mean an application was successfully identified.

The reader's Boolean API does not distinguish EOF from read failure. Coverage therefore describes complete records returned before reading stopped, rather than guaranteeing that a damaged file was read completely. Forwarded counters represent decisions, not verified disk-write success.

## DNS and hostname reporting

`[DNS Requested Domains]` lists readable DNS question names and their query counts. Repeated requests, background applications, ads, and browser preloading can all contribute; a DNS query is not proof of a website visit.

`[Detected Domains/SNIs]` lists hostnames extracted from TLS/HTTP traffic and their application labels. DNS requests are not correlated with later connections for application blocking. Seeing `youtube.com` in the DNS list alone does not cause `--block-app YouTube` to drop traffic.

## Architecture and files

```text
PCAP reader -> coverage accounting -> packet parser -> eligible IPv4 TCP/UDP
    -> load balancer queues -> worker flow tracking/classification/rules
    -> output queue -> PCAP writer
    -> terminal reports after processing
```

| File | Purpose |
|---|---|
| `src/dpi_mt.cpp` | Working multithreaded engine, CLI, worker logic, filtering, and reports. |
| `include/capture_coverage.h` | Reader-owned coverage counters and accounting calculations. |
| `src/pcap_reader.cpp` | Classic PCAP input and record-length checks. |
| `src/packet_parser.cpp` | Ethernet/IPv4/TCP/UDP parsing. |
| `src/sni_extractor.cpp` | TLS SNI, HTTP Host, and DNS question extraction. |
| `src/types.cpp`, `include/types.h` | Five-tuples, hashing, application labels, and hostname classification. |
| `src/main_working.cpp` | Separate single-threaded DPI implementation. |
| `src/main.cpp`, `src/main_simple.cpp` | Basic packet display and diagnostic entry points. |
| `src/main_dpi.cpp` and modular engine files | Separate modular implementation; not used by the build commands above. |
| `generate_test_pcap.py` | Generates synthetic IPv4 TLS, HTTP, DNS, and filtering examples. |

`CMakeLists.txt` currently builds the basic `src/main.cpp` packet display program, not the working DPI executable. Use the explicit build commands above for the DPI demo. The separate modular implementation has known build issues and is not the validated demo target.

Regenerate the sample with:

```powershell
python generate_test_pcap.py
```

The generator uses randomized fields, so regenerated samples need not be byte-identical.

## Validation

The coverage feature was compiled after each implementation step. Local acceptance checks covered:

- The included 77-record sample and the 31,912-record personal capture.
- Independent PCAP record, EtherType, and captured-byte counts.
- All three accounting relationships.
- A mixed fixture covering every skip category.
- Empty capture handling and a zero coverage denominator.
- Rejection of non-Ethernet input before output creation.
- YouTube filtering: 76 retained sample packets and one rule-based drop.
- Equivalent retained packet bytes and timestamps against a pre-change executable, allowing for output reordering.

The local verification script and baseline backups were kept in `.coverage-review`, which is excluded from this repository. These checks are documented results, not a claim of a committed CI or automated test suite.

## Known limitations

- IPv6 parsing, VLAN decoding, and IP-fragment reassembly are unsupported.
- TLS/HTTP extraction is packet-local; no TCP stream reassembly is implemented.
- Encrypted DNS, encrypted TLS names, and QUIC traffic are not reliably identified.
- Application matching uses substring heuristics and can misclassify domains.
- Flow tracking is directional; bidirectional session handling is incomplete.
- The default two-stage hash mapping can leave some configured workers unused.
- Worker completion relies on a fixed wait; the pipeline accounting check can reveal a mismatch but does not repair it.
- Output ordering may differ from capture ordering because workers operate concurrently.
- Parser validation and error propagation are limited; this is not a production security tool.

## Development note

Capture coverage accounting and DNS reporting updates were implemented and validated with AI assistance. The coverage change observes the existing processing boundaries; it does not add IPv6 support or change the classification and filtering algorithms.
