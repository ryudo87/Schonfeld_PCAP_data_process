#include <iostream>
#include <vector>
#include <cstring>
#include <iomanip>

int main() {
    std::vector<u_char> packet(100, 0);
    int offset = 0;
    
    packet[offset++] = 0x01;  // Multicast Group Number
    packet[offset++] = 0x00;  // Number of System Reboots
    packet[offset++] = 0x00;  // Sequence Number (4 bytes)
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;
    packet[offset++] = 0x01;
    
    // Issue Code (12 bytes, left-aligned): "TEST"
    std::string issueCode = "TEST";
    std::copy(issueCode.begin(), issueCode.end(), packet.begin() + offset);
    offset += 12;  // Offset is now 18
    
    packet[offset++] = 0x00;  // Update Number (4 bytes)
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;
    packet[offset++] = 0x01;
    
    packet[offset++] = 0x00;  // Packet Number
    packet[offset++] = 0x00;  // Total Number of Packets
    packet[offset++] = 0x00;  // Utility Flag
    packet[offset++] = 0x02;  // Message Count = 2 tags

    std::cout << "After header, offset = " << offset << std::endl;
    std::cout << "Issue Code bytes 6-17:" << std::endl;
    for (int i = 6; i < 18; i++) {
        std::cout << "  [" << i << "] = " << (isprint(packet[i]) ? packet[i] : '.') 
                  << " (0x" << std::hex << std::setw(2) << std::setfill('0') << (int)packet[i] << std::dec << ")" << std::endl;
    }
    std::cout << "Message count at offset 25: " << (int)packet[25] << std::endl;
    
    // Tag 1 starts at offset 26
    packet[offset++] = 26;  // A tag length
    packet[offset++] = 'A';  // Message Type
    
    std::cout << "Tag 1 length at offset 26: " << (int)packet[26] << std::endl;
    std::cout << "Tag 1 type at offset 27: " << packet[27] << std::endl;
    
    return 0;
}
