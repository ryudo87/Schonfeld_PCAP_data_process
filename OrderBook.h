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
        if (it == orders.end()) {
            return;
        }

        uint64_t oldQty = it->second.qty;
        if (oldQty == newQty) {
            return;
        }

        uint64_t price = it->second.price;
        if (it->second.side == 'B') {
            if (buyLevels.count(price)) {
                uint64_t currentLevelQty = buyLevels[price];
                if (currentLevelQty >= oldQty) {
                    buyLevels[price] = currentLevelQty - oldQty + newQty;
                } else {
                    buyLevels[price] = newQty;
                }
                if (buyLevels[price] == 0) {
                    buyLevels.erase(price);
                }
            }
        } else {
            if (sellLevels.count(price)) {
                uint64_t currentLevelQty = sellLevels[price];
                if (currentLevelQty >= oldQty) {
                    sellLevels[price] = currentLevelQty - oldQty + newQty;
                } else {
                    sellLevels[price] = newQty;
                }
                if (sellLevels[price] == 0) {
                    sellLevels.erase(price);
                }
            }
        }

        if (newQty == 0) {
            orders.erase(it);
        } else {
            it->second.qty = newQty;
        }
    }

    void reduceOrder(uint64_t orderId, uint64_t reduceQty) {
        auto it = orders.find(orderId);
        if (it == orders.end()) {
            return;
        }

        if (reduceQty >= it->second.qty) {
            deleteOrder(orderId);
        } else {
            modifyOrder(orderId, it->second.qty - reduceQty);
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
        // Collect all unique price levels in ascending order
        std::vector<uint64_t> prices;
        prices.reserve(buyLevels.size() + sellLevels.size());
        for (const auto& [p, _] : buyLevels) prices.push_back(p);
        for (const auto& [p, _] : sellLevels) prices.push_back(p);
        std::sort(prices.begin(), prices.end());
        prices.erase(std::unique(prices.begin(), prices.end()), prices.end());

        size_t n = prices.size();
        if (n == 0) return {0, 0};

        // Map price -> index
        std::unordered_map<uint64_t, size_t> idxMap;
        idxMap.reserve(n * 2);
        for (size_t i = 0; i < n; ++i) idxMap[prices[i]] = i;

        // Build arrays of quantities at each price index
        std::vector<uint64_t> buyQty(n, 0), sellQty(n, 0);
        for (const auto& [p, q] : buyLevels) {
            auto it = idxMap.find(p);
            if (it != idxMap.end()) buyQty[it->second] = q;
        }
        for (const auto& [p, q] : sellLevels) {
            auto it = idxMap.find(p);
            if (it != idxMap.end()) sellQty[it->second] = q;
        }

        // Compute suffix sums for buys (>= price) and prefix sums for sells (<= price)
        std::vector<uint64_t> buyCum(n, 0), sellCum(n, 0);
        for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
            buyCum[i] = buyQty[i] + (i + 1 < static_cast<int>(n) ? buyCum[i + 1] : 0);
        }
        for (size_t i = 0; i < n; ++i) {
            sellCum[i] = sellQty[i] + (i > 0 ? sellCum[i - 1] : 0);
        }

        // Evaluate executable volume at each candidate price
        for (size_t i = 0; i < n; ++i) {
            uint64_t executableVol = std::min(buyCum[i], sellCum[i]);
            uint64_t price = prices[i];
            if (executableVol > maxVolume || (executableVol == maxVolume && price > bestPrice)) {
                maxVolume = executableVol;
                bestPrice = price;
            }
        }
        
        return {bestPrice, maxVolume};
    }
};