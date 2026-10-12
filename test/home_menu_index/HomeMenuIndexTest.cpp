#include <gtest/gtest.h>

#include "activities/home/HomeMenuIndex.h"

namespace {

TEST(HomeMenuIndex, BaseMenuOrder) {
  EXPECT_EQ(homeMenuIndexToItem(0, false, false), HomeMenuItem::FILE_BROWSER);
  EXPECT_EQ(homeMenuIndexToItem(1, false, false), HomeMenuItem::LIBRARY);
  EXPECT_EQ(homeMenuIndexToItem(2, false, false), HomeMenuItem::FILE_TRANSFER);
  EXPECT_EQ(homeMenuIndexToItem(3, false, false), HomeMenuItem::SETTINGS_MENU);
  EXPECT_EQ(homeMenuIndexToItem(4, false, false), HomeMenuItem::NONE);
}

TEST(HomeMenuIndex, LibrarySlotShiftsLaterItems) {
  EXPECT_EQ(homeMenuIndexToItem(2, true, false), HomeMenuItem::OPDS_BROWSER);
  EXPECT_EQ(homeMenuIndexToItem(3, true, false), HomeMenuItem::FILE_TRANSFER);
  EXPECT_EQ(homeMenuIndexToItem(4, true, false), HomeMenuItem::SETTINGS_MENU);
  EXPECT_EQ(homeMenuIndexToItem(5, true, false), HomeMenuItem::NONE);
}

TEST(HomeMenuIndex, SyncIsTheLastItemOnlyWhenPresent) {
  EXPECT_EQ(homeMenuIndexToItem(4, false, true), HomeMenuItem::SYNC);
  EXPECT_EQ(homeMenuIndexToItem(5, false, true), HomeMenuItem::NONE);
  EXPECT_EQ(homeMenuIndexToItem(5, true, true), HomeMenuItem::SYNC);
  EXPECT_EQ(homeMenuIndexToItem(6, true, true), HomeMenuItem::NONE);
  EXPECT_NE(homeMenuIndexToItem(4, false, false), HomeMenuItem::SYNC);
}

TEST(HomeMenuIndex, ItemToIndexInvertsIndexToItem) {
  for (const bool hasLibrarySlot : {false, true}) {
    for (const bool hasSync : {false, true}) {
      for (int index = 0; index < 7; index++) {
        const HomeMenuItem item = homeMenuIndexToItem(index, hasLibrarySlot, hasSync);
        if (item == HomeMenuItem::NONE) continue;
        EXPECT_EQ(homeMenuItemToIndex(item, hasLibrarySlot, hasSync), index)
            << "library=" << hasLibrarySlot << " sync=" << hasSync << " index=" << index;
      }
    }
  }
}

TEST(HomeMenuIndex, AbsentItemsFallBackToTheFirstEntry) {
  EXPECT_EQ(homeMenuItemToIndex(HomeMenuItem::SYNC, false, false), 0);
  EXPECT_EQ(homeMenuItemToIndex(HomeMenuItem::OPDS_BROWSER, false, false), 0);
  EXPECT_EQ(homeMenuItemToIndex(HomeMenuItem::NONE, true, true), 0);
}

}  // namespace
