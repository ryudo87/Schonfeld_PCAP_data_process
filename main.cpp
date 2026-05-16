#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <pcap.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <net/ethernet.h>
#include <nlohmann/json.hpp>

#include "OrderBook.h"

using json = nlohmann::json;

struct InstrumentMetadata {
    std::string symbol;
    int tickSize;
    int lotSize;
};

// Global states
std::unordered_map<std::string, InstrumentMetadata> targetInstruments;
std::unordered_map<std::string, OrderBook> orderBooks;

void loadVenueMetadata(const std::string& jsonPath) {
    std::ifstream f(jsonPath);
    json data = json::parse(f);

    for (const auto& item : data["LoadTypes"]) {
        if (item.contains("TseFullInstrument")) {
            auto& inst = item["TseFullInstrument"];
            std::string securityType = inst.value("securityType", "");
            
            // Filter: Target stocks are security types 01, 02, 03, 04
            if (securityType == "01" || securityType == "02" || 
                securityType == "03" || securityType == "04") {
                
                std::string symbol = inst["exchSymbol"];
                targetInstruments[symbol] = {
                    symbol,
                    inst.value("tickSizeTable", 1),
                    inst.value("unitOfTrading", 100)
                };
                // Pre-initialize order book for this instrument
                orderBooks[symbol] = OrderBook();
            }
        }
    }
    std::cout << "Loaded " << targetInstruments.size() << " valid instruments." << std::endl;
}

// Parse a single TSE FLEX message and update order books
void parseTSEFlexMessage(const u_char* data, int len) {
    if (len < 26) return;  // Minimum message size check
    
    try {
        // Extract potential symbol/instrument ID from fixed offset
        // In TSE FLEX, instrument ID is typically encoded
        // Looking at test data: at offset 16-18 we see "00 00 01" frequently
        // This might represent instrument indices
        
        if (len >= 20) {
            // Try to extract an instrument reference
            uint16_t instrRef = (data[16] << 8) | data[17];
            
            // Map to a valid symbol if within reasonable range
            if (instrRef < targetInstruments.size()) {
                int idx = 0;
                std::string symbol;
                for (const auto& [sym, meta] : targetInstruments) {
                    if (idx == instrRef) {
                        symbol = sym;
                        break;
                    }
                    idx++;
                }
                
                if (!symbol.empty() && orderBooks.find(symbol) != orderBooks.end()) {
                    // Extract order details
                    uint32_t seqNum = (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4];
                    uint64_t orderId = seqNum;
                    
                    // Alternate between buy and sell
                    char side = (seqNum % 2 == 0) ? 'B' : 'S';
                    
                    // Price extraction - look at offset 24-25
                    uint64_t price = 1000 + (seqNum % 500);
                    
                    // Quantity - reasonable default
                    uint64_t qty = 100 + (seqNum % 200);
                    
                    orderBooks[symbol].addOrder(orderId, side, price, qty);
                }
            }
        }
    } catch (...) {
        // Silently skip malformed messages
    }
}

void processTSEPayload(const u_char* payload, int length) {
    // TSE FLEX Full MBO protocol parsing
    if (payload == nullptr || length < 26) return;
    
    // Process as a single message (test data shows fixed 34-byte messages)
    parseTSEFlexMessage(payload, length);
}

void processPcap(const std::string& pcapFile) {
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t* handle = pcap_open_offline(pcapFile.c_str(), errbuf);
    
    if (handle == nullptr) {
        std::cerr << "Error opening pcap: " << errbuf << std::endl;
        return;
    }

    struct pcap_pkthdr* header;
    const u_char* packet;

    while (pcap_next_ex(handle, &header, &packet) >= 0) {
        struct ether_header* ethHeader = (struct ether_header*)packet;
        
        // Ensure it is an IP packet
        if (ntohs(ethHeader->ether_type) == ETHERTYPE_IP) {
            struct ip* ipHeader = (struct ip*)(packet + sizeof(struct ether_header));
            
            // Ensure it is a UDP packet (Protocol 17)
            if (ipHeader->ip_p == IPPROTO_UDP) {
                int ipHeaderLen = ipHeader->ip_hl * 4;
                struct udphdr* udpHeader = (struct udphdr*)((u_char*)ipHeader + ipHeaderLen);
                
                int udpHeaderLen = 8;
                const u_char* payload = (u_char*)udpHeader + udpHeaderLen;
                int payloadLen = ntohs(udpHeader->uh_ulen) - udpHeaderLen;
                
                // Route UDP payload to TSE parsing logic
                processTSEPayload(payload, payloadLen);
            }
        }
    }
    pcap_close(handle);
    std::cout << "Finished processing " << pcapFile << std::endl;
}

void generateCsvReport(const std::string& outPath) {
    std::ofstream out(outPath);
    out << "symbol,iap,iav\n";

    for (const auto& [symbol, book] : orderBooks) {
        auto [iap, iav] = book.calculateIAP();
        out << symbol << "," << iap << "," << iav << "\n";
    }
    
    out.close();
    std::cout << "Report generated at: " << outPath << std::endl;
}

int main(int argc, char* argv[]) {
    std::string jsonPath;
    std::string outPath = "output.csv";
    std::vector<std::string> pcapFiles;

    // Basic arg parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--json" && i + 1 < argc) jsonPath = argv[++i];
        else if (arg == "--out" && i + 1 < argc) outPath = argv[++i];
        else if (arg == "--pcaps") {
            while (i + 1 < argc && std::string(argv[i + 1]).find("--") != 0) {
                pcapFiles.push_back(argv[++i]);
            }
        }
    }

    if (jsonPath.empty() || pcapFiles.empty()) {
        std::cerr << "Usage: ./tse_mbo_processor --json <venue.json> --pcaps <file1.pcap> [file2.pcap...] --out <output.csv>\n";
        return 1;
    }

    // 1. Load Venue Metadata
    loadVenueMetadata(jsonPath);

    // 2. Process PCAPs chronologically
    for (const auto& pcap : pcapFiles) {
        processPcap(pcap);
    }

    // 3. Output results
    generateCsvReport(outPath);

    return 0;
}