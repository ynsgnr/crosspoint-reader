#pragma once

#include "activities/HomeMenuItem.h"

// Menu order: File Browser, Library, [OPDS or Plugins], File Transfer, Settings, [Sync].
// The bracketed entries exist only when their flag is set.
inline int homeMenuItemToIndex(const HomeMenuItem item, const bool hasLibrarySlot, const bool hasSync) {
  int i = 0;
  if (item == HomeMenuItem::FILE_BROWSER) return i;
  ++i;
  if (item == HomeMenuItem::LIBRARY) return i;
  ++i;
  if (item == HomeMenuItem::OPDS_BROWSER) return hasLibrarySlot ? i : 0;
  if (hasLibrarySlot) ++i;
  if (item == HomeMenuItem::FILE_TRANSFER) return i;
  ++i;
  if (item == HomeMenuItem::SETTINGS_MENU) return i;
  ++i;
  if (item == HomeMenuItem::SYNC) return hasSync ? i : 0;
  return 0;
}

inline HomeMenuItem homeMenuIndexToItem(const int index, const bool hasLibrarySlot, const bool hasSync) {
  int i = 0;
  if (index == i++) return HomeMenuItem::FILE_BROWSER;
  if (index == i++) return HomeMenuItem::LIBRARY;
  if (hasLibrarySlot && index == i++) return HomeMenuItem::OPDS_BROWSER;
  if (index == i++) return HomeMenuItem::FILE_TRANSFER;
  if (index == i++) return HomeMenuItem::SETTINGS_MENU;
  if (hasSync && index == i) return HomeMenuItem::SYNC;
  return HomeMenuItem::NONE;
}
