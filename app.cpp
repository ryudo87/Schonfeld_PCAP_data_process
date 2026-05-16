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

static void processAddEvent(const u_char* value, int valueLen, OrderBook& book) {
    if (valueLen < 25) return;

    uint64_t orderId = readUint32(value);
    uint64_t price = readUint32(value + 4);
    char side = static_cast<char>(value[8]);
    uint64_t qty = readUint16(value + 13);

    if (qty == 0 || (side != 'B' && side != 'S')) {
        return;
    }

    book.addOrder(orderId, side, price, qty);
}

static void processReduceEvent(const u_char* value, int valueLen, OrderBook& book) {
    if (valueLen < 10) return;

    uint64_t orderId = readUint32(value);
    uint64_t qty = readUint32(value + 4);

    if (qty == 0) {
        book.deleteOrder(orderId);
    } else {
        book.reduceOrder(orderId, qty);
    }
}

static void processModifyEvent(const u_char* value, int valueLen, OrderBook& book) {
    if (valueLen < 10) return;
    uint64_t orderId = readUint32(value);
    uint64_t newQty = readUint32(value + 4);
    book.modifyOrder(orderId, newQty);
}

void parseTSEFlexMessage(const u_char* data, int len, AppState& state) {
    if (len < 26) return;

    std::string symbol = extractInstrumentSymbol(data, len);
    if (symbol.empty()) return;

    auto bookIt = state.orderBooks.find(symbol);
    if (bookIt == state.orderBooks.end()) return;

    // TLV parsing: use pointer iteration for safety and clarity
    const u_char* ptr = data + 26;
    const u_char* end = data + len;

    while (ptr + 1 < end) {
        uint8_t blockLen = *ptr; // total length including tag + value
        if (blockLen == 0) break;
        const u_char* next = ptr + 1 + blockLen;
        if (next > end) break; // malformed/partial

        char tag = static_cast<char>(*(ptr + 1));
        const u_char* value = ptr + 2;
        int valueLen = static_cast<int>(blockLen) - 1; // tag consumes 1 byte

        switch (tag) {
            case 'A': // Add
                processAddEvent(value, valueLen, bookIt->second);
                break;
            case 'D': // Delete/Reduce
            case 'E': // Execute/Reduce-like
                processReduceEvent(value, valueLen, bookIt->second);
                break;
            case 'C': // Modify
                processModifyEvent(value, valueLen, bookIt->second);
                break;
            default:
                // Unknown tag: ignore
                break;
        }

        ptr = next;
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
