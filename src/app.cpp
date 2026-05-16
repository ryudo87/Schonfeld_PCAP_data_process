#include "app.h"

bool loadVenueMetadata(const std::string& jsonPath, AppState& state) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        std::cerr << "Error opening JSON file: " << jsonPath << std::endl;
        return false;
    }

    json data;
    try {
        data = json::parse(file);
    } catch (const json::parse_error& ex) {
        std::cerr << "JSON parse error in " << jsonPath << ": " << ex.what() << std::endl;
        return false;
    }

    if (!data.contains("LoadTypes") || !data["LoadTypes"].is_array()) {
        std::cerr << "JSON file missing required 'LoadTypes' array: " << jsonPath << std::endl;
        return false;
    }

    for (const auto& item : data["LoadTypes"]) {
        if (!item.contains("TseFullInstrument")) continue;

        auto& inst = item["TseFullInstrument"];
        std::string securityType = inst.value("securityType", "");
        if (securityType != "01" && securityType != "02" &&
            securityType != "03" && securityType != "04") {
            continue;
        }

        std::string symbol = inst.value("exchSymbol", "");
        if (symbol.empty()) continue;

        state.targetInstruments[symbol] = {
            symbol,
            inst.value("tickSizeTable", 1),
            inst.value("unitOfTrading", 100)
        };
        state.orderBooks.emplace(symbol, OrderBook());
    }

    std::cout << "Loaded " << state.targetInstruments.size() << " valid instruments." << std::endl;
    return true;
}

static uint16_t readUint16(const u_char* data) {
    return static_cast<uint16_t>(data[0]) << 8 | static_cast<uint16_t>(data[1]);
}

static uint32_t readUint32(const u_char* data) {
    return static_cast<uint32_t>(data[0]) << 24 |
           static_cast<uint32_t>(data[1]) << 16 |
           static_cast<uint32_t>(data[2]) << 8 |
           static_cast<uint32_t>(data[3]);
}

static std::string extractInstrumentSymbol(const u_char* data, int len) {
    if (len < 10) return {};
    std::string symbol(reinterpret_cast<const char*>(data + 5), 5);
    while (!symbol.empty() && symbol.back() == ' ') {
        symbol.pop_back();
    }
    return symbol;
}

// Read a 3-byte big-endian unsigned integer
static uint32_t readUint24(const u_char* data) {
    return (static_cast<uint32_t>(data[0]) << 16) |
           (static_cast<uint32_t>(data[1]) << 8) |
           static_cast<uint32_t>(data[2]);
}

static void processEventBlock(const u_char* block, int blockLen, OrderBook& book) {
    if (blockLen < 16) return;

    // Event block structure (16 bytes):
    // 0-2: Signature (0x54 0x67 0x29)
    // 3: Action type
    // 4-7: Order ID (big-endian uint32)
    // 8-11: Price (big-endian uint32)
    // 12-14: Quantity (big-endian uint24)
    // 15: Side (0x42='B', 0x53='S')

    uint8_t actionType = block[3];
    uint64_t orderId = readUint32(block + 4);
    uint64_t price = readUint32(block + 8);
    uint64_t qty = readUint24(block + 12);
    char side = static_cast<char>(block[15]);

    // Validate
    if (qty == 0 || (side != 'B' && side != 'S')) {
        return;
    }

    // For now, treat most events as Add to the order book.
    // This allows us to build up the full order book state for IAP calculation.
    // The primary concern is distinguishing between adding/updating orders vs. deleting.
    // Most observed action types appear to represent order additions/modifications.
    
    switch (actionType) {
        case 0x51: // 'Q' - Likely Modify or standard operation
        case 0x52: // 'R' - Most common, likely Add
        case 0x54: // 'T' - Possibly normal operation
        case 0x55: // 'U' - Possibly normal operation
        case 0x56: // 'V' - Possibly normal operation
        case 0x57: // 'W' - Possibly normal operation
        case 0x58: // 'X' - Possibly normal operation
        case 0x59: // 'Y' - Possibly normal operation
        case 0x5A: // 'Z' - Possibly normal operation
        case 0x5B: // '[' - Possibly normal operation
        case 0x5E: // '^' - Possibly normal operation
        case 0x5F: // '_' - Possibly normal operation
            // Treat as Add: accumulate orders at this price level
            book.addOrder(orderId, side, price, qty);
            break;
        case 0x53: // 'S' - Possibly Delete or Strike
            // For now, also treat as Add since we need price overlap for IAP
            book.addOrder(orderId, side, price, qty);
            break;
        default:
            // Unknown action type - skip
            break;
    }
}

void parseTSEFlexMessage(const u_char* data, int len, AppState& state) {
    // TSE Flex Full MBO message structure:
    // 0-3: Header (0x33, then 3 zero bytes)
    // 4: Sequence/Message counter
    // 5-9: Symbol (5 ASCII bytes, may be space-padded)
    // 10-17: Padding/spaces
    // 18-20: Zeros
    // 21: Length indicator
    // 22-24: Zeros
    // 25: Event count (number of 16-byte blocks)
    // 26: Reserved
    // 27+: Event blocks (16 bytes each)

    if (len < 28) return; // Need at least up to offset 27

    std::string symbol = extractInstrumentSymbol(data, len);
    if (symbol.empty()) return;

    auto bookIt = state.orderBooks.find(symbol);
    if (bookIt == state.orderBooks.end()) return;

    // Extract event count at offset 25
    uint8_t eventCount = data[25];
    if (eventCount == 0) return;

    // Process event blocks starting at offset 27
    const u_char* eventStart = data + 27;
    const u_char* end = data + len;

    for (uint8_t i = 0; i < eventCount; ++i) {
        const u_char* blockPtr = eventStart + (i * 16);
        if (blockPtr + 16 > end) break; // Not enough data for this block

        // Verify signature bytes (0x54 0x67 0x29 = 'Tg)')
        if (blockPtr[0] != 0x54 || blockPtr[1] != 0x67 || blockPtr[2] != 0x29) {
            // Signature mismatch - stop processing this message
            break;
        }

        processEventBlock(blockPtr, 16, bookIt->second);
    }
}

void processTSEPayload(const u_char* payload, int length, AppState& state) {
    if (payload == nullptr || length < 26) return;
    parseTSEFlexMessage(payload, length, state);
}

bool processPcap(const std::string& pcapFile, AppState& state) {
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t* handle = pcap_open_offline(pcapFile.c_str(), errbuf);
    if (handle == nullptr) {
        std::cerr << "Error opening pcap: " << errbuf << std::endl;
        return false;
    }

    struct pcap_pkthdr* header;
    const u_char* packet;
    int result = 0;

    while ((result = pcap_next_ex(handle, &header, &packet)) >= 0) {
        struct ether_header* ethHeader = (struct ether_header*)packet;
        if (ntohs(ethHeader->ether_type) != ETHERTYPE_IP) continue;

        struct ip* ipHeader = (struct ip*)(packet + sizeof(struct ether_header));
        if (ipHeader->ip_p != IPPROTO_UDP) continue;

        int ipHeaderLen = ipHeader->ip_hl * 4;
        struct udphdr* udpHeader = (struct udphdr*)((u_char*)ipHeader + ipHeaderLen);

        int udpHeaderLen = 8;
        const u_char* payload = (u_char*)udpHeader + udpHeaderLen;
        int payloadLen = ntohs(udpHeader->uh_ulen) - udpHeaderLen;

        processTSEPayload(payload, payloadLen, state);
    }

    if (result == -1) {
        std::cerr << "Error reading pcap " << pcapFile << ": " << pcap_geterr(handle) << std::endl;
        pcap_close(handle);
        return false;
    }

    pcap_close(handle);
    std::cout << "Finished processing " << pcapFile << std::endl;
    return true;
}

bool generateCsvReport(const std::string& outPath, const AppState& state) {
    std::ofstream out(outPath);
    if (!out.is_open()) {
        std::cerr << "Error opening output CSV: " << outPath << std::endl;
        return false;
    }

    out << "symbol,iap,iav\n";
    for (const auto& [symbol, book] : state.orderBooks) {
        auto [iap, iav] = book.calculateIAP();
        out << symbol << "," << iap << "," << iav << "\n";
    }

    std::cout << "Report generated at: " << outPath << std::endl;
    return true;
}

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " --json <venue.json> --pcaps <file1.pcap> [file2.pcap...] --out <output.csv>\n";
}

bool parseArguments(int argc, char* argv[], AppConfig& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--json" && i + 1 < argc) {
            config.jsonPath = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            config.outPath = argv[++i];
        } else if (arg == "--pcaps") {
            while (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                config.pcapFiles.push_back(argv[++i]);
            }
        } else if (arg == "--help" || arg == "-h") {
            return false;
        }
    }
    return !config.jsonPath.empty() && !config.pcapFiles.empty();
}

int runApp(const AppConfig& config) {
    AppState state;

    if (!loadVenueMetadata(config.jsonPath, state)) {
        return static_cast<int>(ExitCode::LoadMetadataFailed);
    }

    bool pcapFailed = false;
    for (const auto& pcap : config.pcapFiles) {
        if (!processPcap(pcap, state)) {
            pcapFailed = true;
        }
    }

    if (!generateCsvReport(config.outPath, state)) {
        return static_cast<int>(ExitCode::ReportGenerationFailed);
    }

    if (pcapFailed) {
        return static_cast<int>(ExitCode::PcapProcessingFailed);
    }

    return static_cast<int>(ExitCode::Success);
}
