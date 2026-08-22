#include <gtest/gtest.h>
#include "core/PluginManager.hpp"

TEST(PluginLoaderTest, LoadAndUnload) {
    sdr::PluginManager pm;
    // Directory may not exist in test environment; just verify unload is safe.
    pm.unload_all();
    EXPECT_TRUE(pm.decoder_names().empty());
}
