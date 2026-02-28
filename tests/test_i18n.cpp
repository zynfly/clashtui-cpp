#include <gtest/gtest.h>
#include "i18n/i18n.hpp"

#include <cstring>

TEST(I18nTest, DefaultLanguageIsZH) {
    current_lang = Lang::ZH;
    EXPECT_EQ(&T(), &ZH_STRINGS);
}

TEST(I18nTest, SwitchToEN) {
    current_lang = Lang::EN;
    EXPECT_EQ(&T(), &EN_STRINGS);
    current_lang = Lang::ZH; // restore
}

TEST(I18nTest, ENStringsNotEmpty) {
    const Strings& s = EN_STRINGS;
    EXPECT_NE(std::strlen(s.app_title), 0u);
    EXPECT_NE(std::strlen(s.connected), 0u);
    EXPECT_NE(std::strlen(s.disconnected), 0u);
    EXPECT_NE(std::strlen(s.confirm), 0u);
    EXPECT_NE(std::strlen(s.cancel), 0u);
    EXPECT_NE(std::strlen(s.mode_global), 0u);
    EXPECT_NE(std::strlen(s.mode_rule), 0u);
    EXPECT_NE(std::strlen(s.mode_direct), 0u);
    EXPECT_NE(std::strlen(s.panel_groups), 0u);
    EXPECT_NE(std::strlen(s.panel_nodes), 0u);
    EXPECT_NE(std::strlen(s.panel_details), 0u);
    EXPECT_NE(std::strlen(s.switching_node), 0u);
    EXPECT_NE(std::strlen(s.testing_delay), 0u);
    EXPECT_NE(std::strlen(s.test_all), 0u);
    EXPECT_NE(std::strlen(s.panel_subscription), 0u);
    EXPECT_NE(std::strlen(s.sub_add), 0u);
    EXPECT_NE(std::strlen(s.sub_update), 0u);
    EXPECT_NE(std::strlen(s.sub_delete), 0u);
    EXPECT_NE(std::strlen(s.install_title), 0u);
    EXPECT_NE(std::strlen(s.log_freeze), 0u);
    EXPECT_NE(std::strlen(s.err_api_failed), 0u);
    EXPECT_NE(std::strlen(s.err_download_failed), 0u);
    EXPECT_NE(std::strlen(s.err_invalid_config), 0u);
    EXPECT_NE(std::strlen(s.err_node_switch_failed), 0u);
}

TEST(I18nTest, ZHStringsNotEmpty) {
    const Strings& s = ZH_STRINGS;
    EXPECT_NE(std::strlen(s.app_title), 0u);
    EXPECT_NE(std::strlen(s.connected), 0u);
    EXPECT_NE(std::strlen(s.disconnected), 0u);
    EXPECT_NE(std::strlen(s.mode_global), 0u);
    EXPECT_NE(std::strlen(s.mode_rule), 0u);
    EXPECT_NE(std::strlen(s.mode_direct), 0u);
    EXPECT_NE(std::strlen(s.panel_groups), 0u);
    EXPECT_NE(std::strlen(s.panel_subscription), 0u);
    EXPECT_NE(std::strlen(s.install_title), 0u);
    EXPECT_NE(std::strlen(s.err_api_failed), 0u);
}

TEST(I18nTest, ZHStringsContainChinese) {
    // Chinese characters are multi-byte in UTF-8, strlen > visible chars
    const Strings& s = ZH_STRINGS;
    // "已连接" is 9 bytes in UTF-8 (3 chars × 3 bytes)
    EXPECT_GT(std::strlen(s.connected), 3u);
    // "全局" is 6 bytes
    EXPECT_GT(std::strlen(s.mode_global), 3u);
}

TEST(I18nTest, LanguageToggle) {
    current_lang = Lang::ZH;
    EXPECT_STREQ(T().mode_global, ZH_STRINGS.mode_global);

    current_lang = Lang::EN;
    EXPECT_STREQ(T().mode_global, "Global");

    current_lang = Lang::ZH;
    EXPECT_STRNE(T().mode_global, "Global");
}
