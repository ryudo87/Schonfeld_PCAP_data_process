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

// Big-endian binary reading helpers
static uint32_t readUint32BE(const u_char* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

static uint64_t readUint64BE(const u_char* data) {
    return (static_cast<uint64_t>(data[0]) << 56) |
           (static_cast<uint64_t>(data[1]) << 48) |
           (static_cast<uint64_t>(data[2]) << 40) |
           (static_cast<uint64_t>(data[3]) << 32) |
           (static_cast<uint64_t>(data[4]) << 24) |
           (static_cast<uint64_t>(data[5]) << 16) |
           (static_cast<uint64_t>(data[6]) << 8) |
           static_cast<uint64_t>(data[7]);
}



// TSE FLEX Full MBO Protocol Tag Handlers

// A tag: Add Order (26 bytes)
// Offset 0: Message Type ('A')
// Offset 1-4: Time (Microseconds)
// Offset 5-8: Order ID (4 bytes)
// Offset 9: Side ('B' or 'S')
// Offset 10-15: Quantity (6 bytes)
// Offset 16-23: Price (8 bytes, last 4 digits are decimal)
// Offset 24: Order Condition
// Offset 25: Modification Flag
static void processATag(const u_char* data, int len, const std::string& issueCode, AppState& state) {
    if (len != 26) return;

    auto bookIt = state.orderBooks.find(issueCode);
    if (bookIt == state.orderBooks.end()) return;

    uint32_t orderId = readUint32BE(data + 5);
    char side = static_cast<char>(data[9]);
    
    // Quantity is 6 bytes big-endian
    uint64_t quantity = 0;
    for (int i = 0; i < 6; ++i) {
        quantity = (quantity << 8) | data[10 + i];
    }
    
    // Price is 8 bytes, last 4 digits are decimal fractions
    uint64_t price = readUint64BE(data + 16);

    if (quantity == 0 || (side != 'B' && side != 'S')) {
        return;
    }

    // Skip market orders (max 64-bit value indicates market order)
    const uint64_t MARKET_ORDER_PRICE = 0xFFFFFFFFFFFFFFFFULL;
    if (price == MARKET_ORDER_PRICE) {
        return;
    }

    bookIt->second.addOrder(orderId, side, price, quantity);
}

// D tag: Order Delete (11 bytes)
// Offset 0: Message Type ('D')
// Offset 1-4: Time (Microseconds)
// Offset 5-8: Order ID (4 bytes)
// Offset 9: Side ('B' or 'S')
// Offset 10: Modification Flag
static void processDTag(const u_char* data, int len, const std::string& issueCode, AppState& state) {
    if (len != 11) return;

    auto bookIt = state.orderBooks.find(issueCode);
    if (bookIt == state.orderBooks.end()) return;

    uint32_t orderId = readUint32BE(data + 5);
    bookIt->second.deleteOrder(orderId);
}

// E tag: Order Executed (Zaraba) (20 bytes)
// Offset 0: Message Type ('E')
// Offset 1-4: Time (Microseconds)
// Offset 5-8: Order ID
// Offset 9: Side
// Offset 10-15: Quantity executed (6 bytes)
// ... additional fields
static void processETag(const u_char* data, int len, const std::string& issueCode, AppState& state) {
    if (len != 20) return;

    auto bookIt = state.orderBooks.find(issueCode);
    if (bookIt == state.orderBooks.end()) return;

    uint32_t orderId = readUint32BE(data + 5);
    
    // Quantity is 6 bytes starting at offset 10
    uint64_t quantity = 0;
    for (int i = 0; i < 6; ++i) {
        quantity = (quantity << 8) | data[10 + i];
    }

    if (quantity > 0) {
        bookIt->second.reduceOrder(orderId, quantity);
    }
}

// C tag: Order Executed with Price (Itayose) (29 bytes)
// Similar structure to E tag with additional pricing information
static void processCTag(const u_char* data, int len, const std::string& issueCode, AppState& state) {
    if (len != 29) return;

    auto bookIt = state.orderBooks.find(issueCode);
    if (bookIt == state.orderBooks.end()) return;

    uint32_t orderId = readUint32BE(data + 5);
    
    // Quantity is 6 bytes starting at offset 10
    uint64_t quantity = 0;
    for (int i = 0; i < 6; ++i) {
        quantity = (quantity << 8) | data[10 + i];
    }

    if (quantity > 0) {
        bookIt->second.reduceOrder(orderId, quantity);
    }
}

void parseTSEFlexMessage(const u_char* data, int len, AppState& state) {
    // TSE FLEX Full MBO packet structure:
    // Packet Header: 26 bytes
    //   Offset 0: Multicast Group Number (1 byte)
    //   Offset 1: Number of System Reboots (1 byte)
    //   Offset 2-5: Sequence Number (4 bytes)
    //   Offset 6-17: Issue Code (12 bytes, left-aligned ASCII)
    //   Offset 18-21: Update Number (4 bytes)
    //   Offset 22: Packet Number (1 byte)
    //   Offset 23: Total Number of Packets (1 byte)
    //   Offset 24: Utility Flag (1 byte)
    //   Offset 25: Message Count (1 byte) - number of tags
    // Then: Variable-length tags, each with [1-byte tag length] + [tag data]

    if (len < 26) return;

    // Extract Issue Code from packet header (offset 6-17, 12 bytes, left-aligned)
    std::string issueCode(reinterpret_cast<const char*>(data + 6), 12);
    // Trim trailing spaces and null bytes
    while (!issueCode.empty() && (issueCode.back() == ' ' || issueCode.back() == '\0')) {
        issueCode.pop_back();
    }
    
    if (issueCode.empty()) return;

    auto bookIt = state.orderBooks.find(issueCode);
    if (bookIt == state.orderBooks.end()) return;

    uint8_t messageCount = data[25];
    
    // Parse tags starting at offset 26
    int tagOffset = 26;
    for (uint8_t i = 0; i < messageCount; ++i) {
        if (tagOffset >= len) break;

        uint8_t tagLength = data[tagOffset];
        ++tagOffset;

        if (tagOffset + tagLength > len) break;

        const u_char* tagData = data + tagOffset;
        char tagType = static_cast<char>(tagData[0]);

        // Route to appropriate tag handler
        switch (tagType) {
            case 'A':
                processATag(tagData, tagLength, issueCode, state);
                break;
            case 'D':
                processDTag(tagData, tagLength, issueCode, state);
                break;
            case 'E':
                processETag(tagData, tagLength, issueCode, state);
                break;
            case 'C':
                processCTag(tagData, tagLength, issueCode, state);
                break;
            case 'T':
            case 'O':
            case 'K':
            case 'R':
            case 'L':
                // These tags don't affect the order book state for MBO
                // (Timestamp, Trading Status, Execution Summary, Reset, Communication Control)
                break;
            default:
                // Unknown tag type - skip
                break;
        }

        tagOffset += tagLength;
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
