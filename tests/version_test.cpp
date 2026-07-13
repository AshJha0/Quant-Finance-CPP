#include "qtest.hpp"
#include "quantfinlib/util/version.hpp"
#include <string>

TEST(version_is_set) {
    EXPECT_TRUE(std::string(quantfinlib::kVersion) == "0.1.0");
}

QTEST_MAIN()
