#include "app.h"
#include "OrderBook.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>

static int testsRun = 0;
static int testsFailed = 0;

#define ASSERT_TRUE(condition) \
    do { \
        ++testsRun; \
        if (!(condition)) { \
            std::cerr << "Assertion failed: " << #condition << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            ++testsFailed; \
            return false; \
        } \
    } while (0)

#define ASSERT_EQ(actual, expected) \
    do { \
        ++testsRun; \
        auto _actual = (actual); \
        auto _expected = (expected); \
        if (!(_actual == _expected)) { \
            std::cerr << "Assertion failed: " << #actual << " == " << #expected \
                      << " (" << _actual << " != " << _expected << ") " \
                      << "(" << __FILE__ << ":" << __LINE__ << ")\n"; \
            ++testsFailed; \
            return false; \
        } \
    } while (0)

static bool runTest(const std::string& name, bool (*testFunc)()) {
    std::cout << "Running: " << name << "... ";
    bool result = testFunc();
    std::cout << (result ? "PASSED" : "FAILED") << "\n";
    return result;
}

static bool testOrderBookCalculateIAP() {
    OrderBook book;
    book.addOrder(1, 'B', 100, 5);
    book.addOrder(2, 'B', 95, 10);
    book.addOrder(3, 'S', 97, 8);
    book.addOrder(4, 'S', 99, 7);

    auto [price, volume] = book.calculateIAP();
    ASSERT_EQ(price, static_cast<uint64_t>(100));
    ASSERT_EQ(volume, static_cast<uint64_t>(5));
    return true;
}

static bool testOrderBookModifyAndReduce() {
    OrderBook book;
    book.addOrder(10, 'B', 100, 10);
    book.addOrder(20, 'S', 105, 20);
    book.modifyOrder(10, 15);
    book.reduceOrder(20, 5);

    auto [price, volume] = book.calculateIAP();
    ASSERT_EQ(price, static_cast<uint64_t>(105));
    ASSERT_EQ(volume, static_cast<uint64_t>(0));
    return true;
}

static bool testParseArgumentsSuccess() {
    std::vector<std::string> args = {
        "prog",
        "--json",
        "venue.json",
        "--pcaps",
        "file1.pcap",
        "file2.pcap",
        "--out",
        "report.csv"
    };
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }

    AppConfig config;
    bool result = parseArguments(static_cast<int>(argv.size()), argv.data(), config);
    ASSERT_TRUE(result);
    ASSERT_EQ(config.jsonPath, "venue.json");
    ASSERT_EQ(config.outPath, "report.csv");
    ASSERT_EQ(config.pcapFiles.size(), 2u);
    ASSERT_EQ(config.pcapFiles[0], "file1.pcap");
    ASSERT_EQ(config.pcapFiles[1], "file2.pcap");
    return true;
}

static bool testParseArgumentsMissingJson() {
    std::vector<std::string> args = {
        "prog",
        "--pcaps",
        "file1.pcap"
    };
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }

    AppConfig config;
    bool result = parseArguments(static_cast<int>(argv.size()), argv.data(), config);
    ASSERT_TRUE(!result);
    return true;
}

static bool testLoadVenueMetadata() {
    const char* metadataFile = "venue_metadata_test.json";
    std::ofstream out(metadataFile);
    ASSERT_TRUE(out.is_open());
    out << R"({
        "LoadTypes": [
            {
                "TseFullInstrument": {
                    "exchSymbol": "TEST",
                    "securityType": "01",
                    "tickSizeTable": 10,
                    "unitOfTrading": 100
                }
            },
            {
                "TseFullInstrument": {
                    "exchSymbol": "BAD",
                    "securityType": "99",
                    "tickSizeTable": 10,
                    "unitOfTrading": 100
                }
            }
        ]
    })";
    out.close();

    AppState state;
    ASSERT_TRUE(loadVenueMetadata(metadataFile, state));
    ASSERT_EQ(state.targetInstruments.size(), 1u);
    ASSERT_EQ(state.orderBooks.size(), 1u);
    ASSERT_TRUE(state.targetInstruments.find("TEST") != state.targetInstruments.end());
    ASSERT_TRUE(state.orderBooks.find("TEST") != state.orderBooks.end());

    std::remove(metadataFile);
    return true;
}

static bool testGenerateCsvReport() {
    const char* csvPath = "report_test.csv";
    AppState state;
    state.orderBooks["TEST"] = OrderBook();
    state.orderBooks["TEST"].addOrder(1, 'B', 100, 5);
    state.orderBooks["TEST"].addOrder(2, 'S', 105, 5);

    ASSERT_TRUE(generateCsvReport(csvPath, state));

    std::ifstream in(csvPath);
    ASSERT_TRUE(in.is_open());

    std::string header;
    std::getline(in, header);
    ASSERT_EQ(header, "symbol,iap,iav");

    std::string line;
    std::getline(in, line);
    ASSERT_EQ(line, "TEST,105,0");
    in.close();
    std::remove(csvPath);
    return true;
}

static bool testParseTSEFlexMessage() {
    AppState state;
    state.orderBooks["12345"] = OrderBook();

    std::vector<u_char> payload(27 + 2 * 16, 0);
    payload[0] = 0x33;
    payload[5] = '1';
    payload[6] = '2';
    payload[7] = '3';
    payload[8] = '4';
    payload[9] = '5';
    payload[25] = 2; // two event blocks

    auto writeUint32 = [&](int offset, uint32_t value) {
        payload[offset + 0] = static_cast<u_char>((value >> 24) & 0xFF);
        payload[offset + 1] = static_cast<u_char>((value >> 16) & 0xFF);
        payload[offset + 2] = static_cast<u_char>((value >> 8) & 0xFF);
        payload[offset + 3] = static_cast<u_char>(value & 0xFF);
    };

    auto writeUint24 = [&](int offset, uint32_t value) {
        payload[offset + 0] = static_cast<u_char>((value >> 16) & 0xFF);
        payload[offset + 1] = static_cast<u_char>((value >> 8) & 0xFF);
        payload[offset + 2] = static_cast<u_char>(value & 0xFF);
    };

    int block1 = 27;
    payload[block1 + 0] = 0x54;
    payload[block1 + 1] = 0x67;
    payload[block1 + 2] = 0x29;
    payload[block1 + 3] = 0x52;
    writeUint32(block1 + 4, 1);
    writeUint32(block1 + 8, 1000);
    writeUint24(block1 + 12, 10);
    payload[block1 + 15] = 'B';

    int block2 = 27 + 16;
    payload[block2 + 0] = 0x54;
    payload[block2 + 1] = 0x67;
    payload[block2 + 2] = 0x29;
    payload[block2 + 3] = 0x52;
    writeUint32(block2 + 4, 2);
    writeUint32(block2 + 8, 1000);
    writeUint24(block2 + 12, 5);
    payload[block2 + 15] = 'S';

    parseTSEFlexMessage(payload.data(), static_cast<int>(payload.size()), state);

    auto [iap, iav] = state.orderBooks["12345"].calculateIAP();
    ASSERT_EQ(iap, static_cast<uint64_t>(1000));
    ASSERT_EQ(iav, static_cast<uint64_t>(5));
    return true;
}

int main() {
    struct TestCase { const char* name; bool (*func)(); } tests[] = {
        {"OrderBook calculateIAP", testOrderBookCalculateIAP},
        {"OrderBook modify and reduce", testOrderBookModifyAndReduce},
        {"parseArguments success", testParseArgumentsSuccess},
        {"parseArguments missing json", testParseArgumentsMissingJson},
        {"loadVenueMetadata", testLoadVenueMetadata},
        {"generateCsvReport", testGenerateCsvReport}
        ,{"parseTSEFlexMessage", testParseTSEFlexMessage}
    };

    for (const auto& test : tests) {
        runTest(test.name, test.func);
    }

    std::cout << "\nSummary: " << (testsRun - testsFailed) << " passed, " << testsFailed << " failed, " << testsRun << " assertions run.\n";
    return testsFailed != 0 ? 1 : 0;
}
