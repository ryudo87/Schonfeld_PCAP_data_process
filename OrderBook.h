#pragma once
#include <map>
#include <set>
#include <unordered_map>
#include <cstdint>
#include <algorithm>

struct Order {
    uint64_t orderId;
    char side; // 'B' for Buy, 'S' for Sell
    uint64_t price;
    uint64_t qty;
};

class OrderBook {
private:
    // Price -> Quantity (std::greater for Buy so highest price is first)
    std::map<uint64_t, uint64_t, std::greater<uint64_t>> buyLevels;
    // Price -> Quantity (std::less for Sell so lowest price is first)
    std::map<uint64_t, uint64_t, std::less<uint64_t>> sellLevels;
    
    // Order ID -> Order mapping for O(1) lookups on modifies/cancels
    std::unordered_map<uint64_t, Order> orders;

public:
    void addOrder(uint64_t orderId, char side, uint64_t price, uint64_t qty) {
        orders[orderId] = {orderId, side, price, qty};
        if (side == 'B') buyLevels[price] += qty;
        else if (side == 'S') sellLevels[price] += qty;
    }

    void deleteOrder(uint64_t orderId) {
        auto it = orders.find(orderId);
        if (it != orders.end()) {
            if (it->second.side == 'B') {
                buyLevels[it->second.price] -= it->second.qty;
                if (buyLevels[it->second.price] == 0) buyLevels.erase(it->second.price);
            } else {
                sellLevels[it->second.price] -= it->second.qty;
                if (sellLevels[it->second.price] == 0) sellLevels.erase(it->second.price);
            }
            orders.erase(it);
        }
    }

    void modifyOrder(uint64_t orderId, uint64_t newQty) {
        auto it = orders.find(orderId);
        if (it != orders.end()) {
            uint64_t oldQty = it->second.qty;
            if (it->second.side == 'B') {
                buyLevels[it->second.price] = (buyLevels[it->second.price] >= oldQty) ? 
                    (buyLevels[it->second.price] - oldQty + newQty) : newQty;
            } else {
                sellLevels[it->second.price] = (sellLevels[it->second.price] >= oldQty) ? 
                    (sellLevels[it->second.price] - oldQty + newQty) : newQty;
            }
            it->second.qty = newQty;
        }
    }

    // Calculates Indicative Auction Price (IAP) and Volume (IAV)
    // IAP is the price that maximizes executable volume between buy and sell sides
    std::pair<uint64_t, uint64_t> calculateIAP() const {
        if (buyLevels.empty() || sellLevels.empty()) {
            return {0, 0};
        }
        
        uint64_t bestPrice = 0;
        uint64_t maxVolume = 0;

        // Collect all price levels from both sides
        std::set<uint64_t> allPrices;
        for (const auto& [price, _] : buyLevels) allPrices.insert(price);
        for (const auto& [price, _] : sellLevels) allPrices.insert(price);

        // For each potential price, calculate cumulative volumes
        for (uint64_t price : allPrices) {
            // Cumulative buy: sum of all buy orders at or above this price
            uint64_t cumBuy = 0;
            for (auto it = buyLevels.begin(); it != buyLevels.end(); ++it) {
                if (it->first >= price) {
                    cumBuy += it->second;
                } else {
                    break;
                }
            }

            // Cumulative sell: sum of all sell orders at or below this price
            uint64_t cumSell = 0;
            for (auto it = sellLevels.begin(); it != sellLevels.end(); ++it) {
                if (it->first <= price) {
                    cumSell += it->second;
                }
            }

            uint64_t executableVol = std::min(cumBuy, cumSell);
            if (executableVol > maxVolume || (executableVol == maxVolume && price > bestPrice)) {
                maxVolume = executableVol;
                bestPrice = price;
            }
        }
        
        return {bestPrice, maxVolume};
    }
};