# TSE FLEX Full MBO PCAP Processor

## AI Model Usage Disclosure
This project was completed with assistance from **GitHub Copilot (Claude Haiku 4.5)**. The AI model was used to:
- Implement the TSE FLEX protocol parser
- Design and optimize the order book data structure
- Create the IAP (Indicative Auction Price) calculation algorithm
- Generate build configurations and project structure
- Debug compilation and runtime issues

## 1. Operating System and Version
* **OS:** Ubuntu 24.04.4 LTS (Noble Numbat)
* **Tested on:** Linux/Unix distributions with POSIX compliance
* **Note:** The code uses standard POSIX networking libraries and `libpcap`, making it portable across Linux/Unix systems

## 2. Compiler and Version
* **Compiler:** GCC (g++) 13.3.0 (Ubuntu 13.3.0-6ubuntu2)
* **C++ Standard:** C++17 (Required for `std::optional`, structured bindings, and other modern C++ features)
* **Alternative:** Clang 14.0 or higher should also work

## 3. Dependencies
Required libraries for compilation:
- `libpcap-dev` - for PCAP file processing
- `nlohmann-json3-dev` - for JSON metadata parsing
- `build-essential` and `cmake` - for building

Install dependencies:
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libpcap-dev nlohmann-json3-dev
```

## 4. How to Compile

```bash
mkdir build
cd build
cmake ..
make -j4
```

The compiled executable will be at `./build/tse_mbo_processor`

## 5. How to Run the Program

```bash
./build/tse_mbo_processor \
    --json TseVenue.20241105.json \
    --pcaps 20241105_051.test.pcap 20241105_052.test.pcap \
    --out output.csv
```

### Command-line Arguments:
- `--json <file>`: Path to the venue metadata JSON file (required)
- `--pcaps <file1> [file2] ...`: One or more PCAP files to process (required)
- `--out <file>`: Output CSV file path (default: `output.csv`)

### Output Format:
The program generates a CSV file with the following format:
```
symbol,iap,iav
9226,1234,5000
9169,1567,3500
...
```

Where:
- `symbol`: TSE exchange symbol (numeric string)
- `iap`: Indicative Auction Price (in base units)
- `iav`: Indicative Auction Volume (total shares)

## 6. Project Architecture

### Main Components

#### OrderBook.h
- `OrderBook` class: Maintains buy and sell order levels
  - Uses `std::map<uint64_t, uint64_t>` for price levels (ordered)
  - Uses `std::unordered_map<uint64_t, Order>` for individual order tracking
  - Methods: `addOrder()`, `deleteOrder()`, `modifyOrder()`, `calculateIAP()`

#### main.cpp
- **loadVenueMetadata()**: Parses JSON to load valid stock instruments (security types 01-04)
- **processTSEPayload()**: Routes UDP payload data to the TSE FLEX protocol parser
- **parseTSEFlexMessage()**: Extracts order information from binary TSE FLEX messages
- **processPcap()**: Reads PCAP files and extracts UDP packets
- **generateCsvReport()**: Writes final auction prices and volumes to CSV

### Protocol Handling
The TSE FLEX Full MBO protocol uses a binary message format over UDP multicast. The parser:
1. Extracts UDP payload from Ethernet/IP/UDP packet stack
2. Parses binary-encoded order messages
3. Maps messages to order book operations (add, delete, modify, execute)
4. Tracks order state by instrument

### Order Book Design
- **Price Levels**: `std::map` with `std::greater<>` for buys (highest first) and `std::less<>` for sells (lowest first)
- **Order Tracking**: `std::unordered_map` for O(1) order lookups during modifications/cancellations
- **IAP Calculation**: Iterates through all price levels to find the price that maximizes executable volume

### IAP Calculation Algorithm
The Indicative Auction Price is determined by:
1. Collecting all unique price levels from both buy and sell sides
2. For each price level, calculating:
   - Cumulative buy volume: sum of all buy orders at or above that price
   - Cumulative sell volume: sum of all sell orders at or below that price
3. Finding the price level where `min(cumulative_buy, cumulative_sell)` is maximized
4. In case of ties, selecting the highest price level

## 7. Important Notes

### PCAP File Handling
- The test data files are provided in gzipped format (`.pcap.gz`)
- They must be decompressed before processing: `gunzip *.pcap.gz`
- Alternatively, the program can be extended to handle gzip streams

### Metadata JSON Structure
The `TseVenue.20241105.json` file contains:
- `LoadTypes[].TseFullInstrument`: Instrument definitions
- Filters applied: Only security types "01", "02", "03", "04" (stocks) are processed
- Each instrument has:
  - `exchSymbol`: The exchange symbol (TSE stock code)
  - `securityType`: Type classification
  - `tickSizeTable`: Minimum price increment
  - `unitOfTrading`: Minimum order quantity (lot size)

### Performance Considerations
- Memory: ~1.3GB for typical PCAP processing (3956+ instruments)
- Processing speed: Depends on PCAP file size and packet volume
- Supports sequential processing of multiple PCAP files

## 8. Testing

Run with provided test files:
```bash
./build/tse_mbo_processor \
    --json TseVenue.20241105.json \
    --pcaps 20241105_051.test.pcap 20241105_052.test.pcap \
    --out output.csv
```

Expected output:
- CSV file with 3957 rows (header + 3956 instruments)
- Each row contains symbol, indicative auction price, and auction volume

## 9. Troubleshooting

### Build Errors
- Ensure all dependencies are installed: `libpcap-dev`, `nlohmann-json3-dev`
- Verify CMake version >= 3.10: `cmake --version`
- Check compiler supports C++17: `g++ --version`

### Runtime Issues
- Verify PCAP files exist and are decompressed
- Check JSON file path and format
- Ensure write permissions for output CSV file
- Monitor memory usage for large PCAP files

## 10. References

- **TSE Official**: https://www.jpx.co.jp/english/
- **libpcap Documentation**: https://www.tcpdump.org/papers/sniffing-faq.html
- **nlohmann/json**: https://github.com/nlohmann/json
- **C++17 Standard**: ISO/IEC 14882:2017