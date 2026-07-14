// Port of the Java SymbolRegistry behavior pins: interning returns a
// stable dense id, re-registering the same symbol is idempotent, id() on
// an unknown symbol throws, and a concurrent register from two threads
// still hands out distinct dense ids to distinct symbols (a stand-in for
// Java's ConcurrentHashMap + `synchronized register` guarantee).
#include "quantfinlib/marketdata/symbol_registry.hpp"

#include <set>
#include <string>
#include <thread>
#include <vector>

#include "qtest.hpp"

using quantfinlib::SymbolRegistry;

TEST(registerInternsToStableDenseIds) {
    SymbolRegistry reg;
    int a = reg.registerSymbol("EURUSD");
    int b = reg.registerSymbol("USDJPY");
    EXPECT_TRUE(a == 0);
    EXPECT_TRUE(b == 1);
    // Idempotent: re-registering returns the SAME id, does not grow size().
    EXPECT_TRUE(reg.registerSymbol("EURUSD") == a);
    EXPECT_TRUE(reg.size() == 2);
    EXPECT_TRUE(reg.symbol(a) == "EURUSD");
    EXPECT_TRUE(reg.symbol(b) == "USDJPY");
    EXPECT_TRUE(reg.id("EURUSD") == a);
    EXPECT_TRUE(reg.id("USDJPY") == b);
}

TEST(idOnUnregisteredSymbolThrows) {
    SymbolRegistry reg;
    reg.registerSymbol("EURUSD");
    EXPECT_THROWS(reg.id("GBPUSD"));
}

TEST(concurrentRegisterFromTwoThreadsAssignsDistinctIds) {
    SymbolRegistry reg;
    constexpr int N = 500;
    std::vector<int> idsA(N), idsB(N);

    std::thread t1([&] {
        for (int i = 0; i < N; i++) {
            idsA[i] = reg.registerSymbol("A" + std::to_string(i));
        }
    });
    std::thread t2([&] {
        for (int i = 0; i < N; i++) {
            idsB[i] = reg.registerSymbol("B" + std::to_string(i));
        }
    });
    t1.join();
    t2.join();

    std::set<int> allIds;
    for (int id : idsA) allIds.insert(id);
    for (int id : idsB) allIds.insert(id);
    // 2N distinct symbols must get 2N distinct ids -- no collisions, no
    // duplicate hand-outs, despite two threads racing register().
    EXPECT_TRUE(static_cast<int>(allIds.size()) == 2 * N);
    EXPECT_TRUE(reg.size() == 2 * N);
    for (int i = 0; i < N; i++) {
        EXPECT_TRUE(reg.id("A" + std::to_string(i)) == idsA[i]);
        EXPECT_TRUE(reg.id("B" + std::to_string(i)) == idsB[i]);
    }
}

QTEST_MAIN()
