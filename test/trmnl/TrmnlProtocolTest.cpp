#include <gtest/gtest.h>

#include "TrmnlProtocol.h"

using namespace trmnl;

TEST(TrmnlDisplayReply, LaraPaperReply) {
  const auto reply = parseDisplayReply(
      R"({"status":0,"image_url":"https:\/\/paper.example\/storage\/a.bmp","filename":"a.bmp","refresh_rate":900})");
  EXPECT_TRUE(reply.ok);
  EXPECT_EQ(reply.imageUrl, "https://paper.example/storage/a.bmp");
  EXPECT_EQ(reply.filename, "a.bmp");
  EXPECT_EQ(reply.refreshSeconds, 900u);
}

TEST(TrmnlDisplayReply, StringRefreshRateAndMissingStatus) {
  const auto reply = parseDisplayReply(R"({"image_url":"http://x/y.bmp","refresh_rate":"1800"})");
  EXPECT_TRUE(reply.ok);
  EXPECT_EQ(reply.refreshSeconds, 1800u);
}

TEST(TrmnlDisplayReply, ErrorStatusIsNotOk) {
  EXPECT_FALSE(parseDisplayReply(R"({"status":404,"image_url":"http://x/y.bmp"})").ok);
}

TEST(TrmnlDisplayReply, MissingImageUrlIsNotOk) {
  EXPECT_FALSE(parseDisplayReply(R"({"status":0,"refresh_rate":300})").ok);
}

TEST(TrmnlDisplayReply, InvalidJsonIsNotOk) { EXPECT_FALSE(parseDisplayReply("<html>").ok); }

TEST(TrmnlSetup, ReadsApiKey) {
  EXPECT_EQ(parseSetupToken(R"({"status":200,"api_key":"abc123","friendly_id":"X"})"), "abc123");
  EXPECT_EQ(parseSetupToken(R"({"status":404})"), "");
  EXPECT_EQ(parseSetupToken("nope"), "");
}

TEST(TrmnlServerUrl, AddsSchemeAndDropsTrailingSlash) {
  EXPECT_EQ(normalizeServerUrl("paper.example/"), "https://paper.example");
  EXPECT_EQ(normalizeServerUrl("http://10.0.0.2:8080//"), "http://10.0.0.2:8080");
  EXPECT_EQ(normalizeServerUrl(""), "");
}

TEST(TrmnlNextWake, TakesTheLargerOfMinimumAndServer) {
  EXPECT_EQ(nextWakeSeconds(20, 900), 1200u);
  EXPECT_EQ(nextWakeSeconds(10, 3600), 3600u);
  EXPECT_EQ(nextWakeSeconds(0, 1800), 1800u);
  EXPECT_EQ(nextWakeSeconds(20, 0), 1200u);
}

TEST(TrmnlNextWake, ClampsAndTurnsOff) {
  EXPECT_EQ(nextWakeSeconds(0, 0), 0u);
  EXPECT_EQ(nextWakeSeconds(0, 30), 300u);
  EXPECT_EQ(nextWakeSeconds(0, 999999), 86400u);
}
