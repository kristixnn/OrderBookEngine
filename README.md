# Order Book Engine

A C++ limit order book engine supporting multiple order types, price-time priority matching, and real-time order management. Implements the core data structure used in electronic trading systems — maintaining buy and sell orders at price levels, matching them when prices cross, and tracking the state of all open orders.

---

## What this project does

The engine maintains two sorted price-level queues — bids (buy orders, highest price first) and asks (sell orders, lowest price first) — and matches them when a buy price meets or exceeds a sell price. It supports:

- **Adding orders** — GoodTillCancel, FillAndKill, FillOrKill, GoodForDay, Market
- **Cancelling orders** — O(1) removal using stored iterators
- **Modifying orders** — cancel and re-add with new parameters
- **Matching** — price-time priority, partial fills, full fills
- **Order book queries** — current size, bid/ask level info with aggregated quantities

---

## Order types

| Type | Behaviour |
|---|---|
| `GoodTillCancel` | Stays in the book until explicitly cancelled or filled |
| `FillAndKill` | Fills whatever quantity is available immediately, cancels the rest |
| `FillOrKill` | Fills completely or cancels entirely — no partial fills |
| `GoodForDay` | Active until end of trading session |
| `Market` | No price specified — matches at best available price immediately |

---

## The data structures

### Why `std::map` for price levels

```cpp
std::map<Price, OrderPointers, std::greater<Price>> bids_;
std::map<Price, OrderPointers, std::less<Price>>    asks_;
```

`std::map` keeps price levels sorted automatically. `std::greater<Price>` on bids means `begin()` always points to the highest bid — the best price to match against. `std::less<Price>` on asks means `begin()` always points to the lowest ask. Both best prices are O(1) to access, O(log n) to insert a new level.

### Why `std::list` for orders at each price level

Orders at each price level are stored in a `std::list`. The key property: `std::list` iterators remain valid after insertions and deletions elsewhere in the list. This is essential because each order stores its own iterator in `OrderEntry` for O(1) cancellation — if the container were `std::vector`, any `push_back` would invalidate all stored iterators, making stored positions dangling and unsafe.

### Why `std::unordered_map` for order lookup

```cpp
std::unordered_map<OrderId, OrderEntry> orders_;
```

O(1) average lookup by order ID. Stores both the order pointer and its iterator in the price-level list — the combination that makes cancel O(1) without searching.

### The `OrderEntry` struct

```cpp
struct OrderEntry {
    OrderPointer order_{ nullptr };
    OrderPointers::iterator location_;
};
```

This is the core of the O(1) cancel design. When a cancel arrives, look up the order in `orders_` (O(1)), get the stored iterator, erase directly from the list (O(1)), clean up empty price levels. No search through the book required.

---

## Matching algorithm

Price-time priority — standard for limit order books:

1. Take the best bid (`bids_.begin()`) and best ask (`asks_.begin()`)
2. If `bestBid >= bestAsk`, a match is possible
3. Fill `min(bid.remaining, ask.remaining)` quantity
4. Remove fully filled orders from the book
5. Remove empty price levels
6. Repeat until no match is possible or the book is empty
7. After matching, cancel any remaining `FillAndKill` or `FillOrKill` orders that weren't fully filled

```
Example:
  Bid: 100 qty 10  |  Ask: 99 qty 6
  → Match at qty 6 (ask fully filled, bid has 4 remaining)
  → Ask removed, bid stays with qty 4
```

---

## Project structure

```
OrderBookEngine/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp                 ← entry point and manual test
│   ├── Orderbook.h/cpp          ← engine: add, cancel, modify, match
│   ├── Order.h                  ← order struct with fill logic
│   ├── OrderModify.h            ← order modification request
│   ├── OrderType.h              ← enum: GTC, FAK, FOK, GFD, Market
│   ├── Side.h                   ← enum: Buy, Sell
│   ├── Trade.h                  ← matched trade result
│   ├── TradeInfo.h              ← per-side trade details
│   ├── LevelInfo.h              ← price level with aggregated quantity
│   ├── OrderbookLevelInfos.h    ← bid/ask level snapshot
│   ├── Constants.h              ← shared constants
│   └── Usings.h                 ← all standard library includes + type aliases
└── tests/
    ├── CMakeLists.txt
    ├── Test.cpp                 ← GoogleTest parameterised test suite
    ├── Match_GoodTillCancel.txt
    ├── Match_FillAndKill.txt
    ├── Match_FillOrKill_Hit.txt
    ├── Match_FillOrKill_Miss.txt
    ├── Cancel_Success.txt
    ├── Modify_Side.txt
    └── Match_Market.txt
```

---

## Dependencies

- C++20 compiler (MSVC, GCC, or Clang) — uses `std::format`, `std::from_chars`, structured bindings
- CMake 3.20+
- GoogleTest (fetched automatically via CMake FetchContent)

---

## Build instructions

```bash
git clone https://github.com/kristixnn/order-book-engine.git
cd order-book-engine
mkdir build && cd build
cmake ..
cmake --build .
```

---

## Running

```bash
.\build\Debug\orderbookengine.exe    # Windows
./build/orderbookengine              # Linux/macOS
```

Expected output from `main.cpp`:
```
1
0
```
One order added (size = 1), then cancelled (size = 0).

---

## Running the tests

```bash
.\build\tests\Debug\OrderbookTests.exe
```

Expected:
```
[==========] Running 7 tests from 1 test suite.
[  PASSED  ] 7 tests.
```

### Test format

Tests are driven by `.txt` files — each file describes a sequence of actions and the expected result:

```
A B GoodTillCancel 100 10 1    ← Add, Buy, GoodTillCancel, price=100, qty=10, id=1
A S GoodTillCancel 100 10 2    ← Add, Sell, GoodTillCancel, price=100, qty=10, id=2
R 0 0 0                        ← Result: 0 total, 0 bid levels, 0 ask levels
```

`A` = Add, `M` = Modify, `C` = Cancel, `R` = expected result. The test runner parses each file, replays the actions against a fresh `Orderbook`, then asserts the final state matches the `R` line.

| Test file | What it verifies |
|---|---|
| `Match_GoodTillCancel.txt` | Basic price-time priority matching |
| `Match_FillAndKill.txt` | Partial fill then cancel remainder |
| `Match_FillOrKill_Hit.txt` | Full fill succeeds |
| `Match_FillOrKill_Miss.txt` | Can't fill completely — cancel entire order |
| `Cancel_Success.txt` | O(1) cancel removes order correctly |
| `Modify_Side.txt` | Cancel + re-add with new parameters |
| `Match_Market.txt` | Market order matches at best available price |

---

## Key design decisions

**Why store iterators in `OrderEntry`?** Cancellation is the most frequent operation in a real order book — market makers cancel and replace orders constantly. Storing the iterator alongside the order makes cancel O(1) instead of O(n). The tradeoff is that the container holding orders must guarantee iterator stability — which is why `std::list` is used instead of `std::vector`. A `std::vector` invalidates all iterators on any `push_back`, making stored positions dangling.

**Why `std::shared_ptr<Order>` instead of raw pointers?** Multiple parts of the system reference the same order simultaneously — the price level list, the `orders_` lookup map, and trade records all point to the same order object. Shared ownership through `shared_ptr` ensures the order stays alive as long as any reference to it exists, without manual lifetime tracking.

**Why file-driven tests instead of hardcoded test cases?** Each `.txt` file is a self-contained scenario that can be read, modified, and version-controlled independently of the test code. Adding a new test case requires only a new `.txt` file — no recompilation of the test binary. The `InputHandler` parser and `INSTANTIATE_TEST_CASE_P` pattern mean the test infrastructure handles any number of scenarios with the same code.

**Why `std::unordered_map` for `orders_` but `std::map` for `bids_`/`asks_`?** Order lookup by ID needs O(1) average — hash map. Price levels need to stay sorted so the best bid/ask is always at `begin()` — tree map. Different access patterns, different containers.

---

## Known limitations

**No thread safety.** A real exchange order book handles concurrent order submissions from multiple connections. This implementation is single-threaded — adding thread safety would require lock-free data structures or fine-grained locking per price level.

**Integer prices only.** `Price` is `std::int32_t` — suitable for tick-based pricing (e.g. price in cents) but not floating-point prices. Real systems use integer ticks with a known tick size.

**No market data feed.** The engine processes orders but doesn't publish a market data feed of book state changes. A production system would stream level 2 data (top of book, full depth) to subscribers after every change.

**`std::list` vs intrusive linked list.** `std::list` allocates a heap node per order. Production order books use intrusive linked lists (node pointers stored inside the order object itself) to eliminate per-order heap allocation and improve cache locality. Libraries like Boost.Intrusive provide this.

**No persistence.** Orders exist only in memory. A production system would write every order event to a persistent log for recovery after crashes.

---

## References

- Harris, L. — *Trading and Exchanges* — Chapters 1–6 (market microstructure and order book mechanics)
- Aldridge, I. — *High-Frequency Trading* — Chapter 5–7 (order book data structures and latency)
- LMAX Disruptor paper — *Disruptor: High Performance Alternative to Bounded Queues* — the design behind lock-free ring buffers used in production trading systems
- Agner Fog — *Optimizing Software in C++* — cache locality and data structure performance
