#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
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

struct AppState {
    std::unordered_map<std::string, InstrumentMetadata> targetInstruments;
    std::unordered_map<std::string, OrderBook> orderBooks;
};

enum class ExitCode {
    Success = 0,
    InvalidArguments = 1,
    LoadMetadataFailed = 2,
    PcapProcessingFailed = 3,
    ReportGenerationFailed = 4
};

struct AppConfig {
    std::string jsonPath;
    std::string outPath = "output.csv";
    std::vector<std::string> pcapFiles;
};

bool loadVenueMetadata(const std::string& jsonPath, AppState& state);
void parseTSEFlexMessage(const u_char* data, int len, AppState& state);
void processTSEPayload(const u_char* payload, int length, AppState& state);
bool processPcap(const std::string& pcapFile, AppState& state);
bool generateCsvReport(const std::string& outPath, const AppState& state);
void printUsage(const char* programName);
bool parseArguments(int argc, char* argv[], AppConfig& config);
int runApp(const AppConfig& config);
