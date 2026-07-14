#include "FileBrowserActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Xtc.h>

#include <algorithm>

#include "BookmarkStore.h"
#include "components/icons/book.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FileBrowserActionActivity.h"
#include "MappedInputManager.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long COMPLETED_FEEDBACK_MS = 1000;
constexpr int ROOT_HINT_GAP = 20;

// Cover grid (Books folder only)
constexpr int GRID_COLS = 3;
constexpr int GRID_COVER_WIDTH = 136;
constexpr int GRID_COVER_HEIGHT = 160;
constexpr int GRID_SPACING = 14;
constexpr int GRID_TITLE_GAP = 4;
constexpr int GRID_BOOKS_PER_PAGE = 9;

bool isSleepFolderPath(const std::string& path) { return path == "/sleep" || path == "/.sleep"; }

bool isSleepImageFile(const std::string& path) {
  return FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path);
}

bool hasFileMetadata(const std::string& path) {
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path) || FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);
}

bool hasClearableBookCache(const std::string& path) {
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path);
}

void drawToast(const GfxRenderer& renderer, const char* msg) {
  constexpr int toastPadX = 20;
  constexpr int toastPadY = 12;
  const int msgW = renderer.getTextWidth(UI_10_FONT_ID, msg);
  const int msgH = renderer.getLineHeight(UI_10_FONT_ID);
  const int toastW = msgW + toastPadX * 2;
  const int toastH = msgH + toastPadY * 2;
  const int toastX = (renderer.getScreenWidth() - toastW) / 2;
  const int toastY = (renderer.getScreenHeight() - toastH) / 2;
  renderer.fillRect(toastX, toastY, toastW, toastH, true);
  renderer.drawText(UI_10_FONT_ID, toastX + toastPadX, toastY + toastPadY, msg, false);
  renderer.displayBuffer();
}

std::string buildFullPath(std::string basepath, const std::string& entry) {
  if (basepath.back() != '/') basepath += "/";
  return basepath + entry;
}

std::string normalizeDirectoryPath(std::string path) {
  while (path.length() > 1 && path.back() == '/') {
    path.pop_back();
  }
  return path;
}

std::string buildReadFolderDestination(const std::string& srcPath) {
  const size_t lastSlash = srcPath.rfind('/');
  const std::string filename = (lastSlash != std::string::npos) ? srcPath.substr(lastSlash + 1) : srcPath;

  Storage.mkdir("/Read");
  std::string dstPath = "/Read/" + filename;
  if (!Storage.exists(dstPath.c_str())) {
    return dstPath;
  }

  const size_t dotPos = filename.rfind('.');
  const std::string base = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
  const std::string ext = (dotPos != std::string::npos) ? filename.substr(dotPos) : "";
  int suffix = 2;
  do {
    dstPath = "/Read/" + base + " (" + std::to_string(suffix) + ")" + ext;
    suffix++;
  } while (Storage.exists(dstPath.c_str()) && suffix < 100);
  return dstPath;
}

bool containsHiddenPathSegment(const std::string& path) {
  if (path.empty()) return false;
  size_t segmentStart = (path.front() == '/') ? 1 : 0;
  while (segmentStart < path.length()) {
    const size_t segmentEnd = path.find('/', segmentStart);
    if (segmentStart < path.length() && path[segmentStart] == '.') {
      return true;
    }
    if (segmentEnd == std::string::npos) {
      break;
    }
    segmentStart = segmentEnd + 1;
  }
  return false;
}

void collectMetadataPathsRecursively(const std::string& dirPath, std::vector<std::string>& paths) {
  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    LOG_ERR("FileBrowser", "Failed to scan directory metadata before delete: %s", dirPath.c_str());
    return;
  }

  char name[256];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    const std::string childPath = buildFullPath(dirPath, name);
    if (file.isDirectory()) {
      collectMetadataPathsRecursively(childPath, paths);
    } else if (hasFileMetadata(childPath)) {
      paths.push_back(childPath);
    }
    file.close();
  }
  dir.close();
}

std::string getFileName(std::string filename);
}  // namespace

bool FileBrowserActivity::isInBooksFolder() const { return basepath == "/books"; }

void FileBrowserActivity::loadFiles() {
  files.clear();
  gridLoadedPage = -1;

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    return;
  }

  root.rewindDirectory();

  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if ((!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0) {
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(std::string(name) + "/");
    } else {
      std::string_view filename{name};
      if (mode == Mode::PickFirmware) {
        // Firmware picker: only show .bin files.
        if (FsHelpers::checkFileExtension(filename, ".bin")) {
          files.emplace_back(filename);
        }
      } else if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
                 FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
                 FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename)) {
        files.emplace_back(filename);
      }
    }
  }
  root.close();
  FsHelpers::sortFileList(files);
}

void FileBrowserActivity::onEnter() {
  Activity::onEnter();

  selectorIndex = 0;

  // If Confirm was held while this activity opened (typical when launched from a menu), ignore
  // its release — otherwise we'd immediately auto-open whatever is at index 0.
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);

    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    selectorIndex = findEntry(fileName);
  } else {
    loadFiles();
  }

  requestUpdate();
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
}

void FileBrowserActivity::clearFileMetadata(const std::string& fullPath) {
  if (FsHelpers::hasEpubExtension(fullPath)) {
    Epub(fullPath, "/.crosspoint").clearCache();
    BookmarkStore::deleteForFilePath(fullPath, "epub");
  } else if (FsHelpers::hasXtcExtension(fullPath)) {
    BookmarkStore::deleteForFilePath(fullPath, "xtc");
  } else if (FsHelpers::hasTxtExtension(fullPath) || FsHelpers::hasMarkdownExtension(fullPath)) {
    BookmarkStore::deleteForFilePath(fullPath, "txt");
  }
  LOG_DBG("FileBrowser", "Cleared metadata for: %s", fullPath.c_str());
}

bool FileBrowserActivity::clearBookCache(const std::string& fullPath) {
  if (FsHelpers::hasEpubExtension(fullPath)) {
    return Epub(fullPath, "/.crosspoint").clearCache();
  }
  if (FsHelpers::hasXtcExtension(fullPath)) {
    return Xtc(fullPath, "/.crosspoint").clearCache();
  }
  return false;
}

void FileBrowserActivity::promptDeleteFile(const std::string& fullPath, const std::string& entry) {
  auto handler = [this, fullPath](const ActivityResult& res) {
    if (res.isCancelled) {
      LOG_DBG("FileBrowser", "Delete cancelled by user");
      return;
    }

    LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
    clearFileMetadata(fullPath);
    if (!Storage.remove(fullPath.c_str())) {
      LOG_ERR("FileBrowser", "Failed to delete file: %s", fullPath.c_str());
      return;
    }

    LOG_DBG("FileBrowser", "Deleted successfully");
    if (isPinnedSleepFavorite(fullPath)) {
      unpinSleepFavorite();
    }

    loadFiles();
    if (files.empty()) {
      selectorIndex = 0;
    } else if (selectorIndex >= files.size()) {
      selectorIndex = files.size() - 1;
    }
    requestUpdate(true);
  };

  const std::string heading = tr(STR_DELETE) + std::string("? ");
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entry), handler);
}

void FileBrowserActivity::promptDeleteDirectory(const std::string& fullPath, const std::string& entry,
                                                const bool ignoreInitialConfirmRelease) {
  const std::string dirPath = normalizeDirectoryPath(fullPath);
  auto handler = [this, dirPath](const ActivityResult& res) {
    longPressConfirmHandled = false;
    if (res.isCancelled) {
      LOG_DBG("FileBrowser", "Delete cancelled by user");
      return;
    }

    std::vector<std::string> metadataPaths;
    collectMetadataPathsRecursively(dirPath, metadataPaths);

    LOG_DBG("FileBrowser", "Attempting to delete directory: %s", dirPath.c_str());
    if (!Storage.removeDir(dirPath.c_str())) {
      LOG_ERR("FileBrowser", "Failed to delete directory: %s", dirPath.c_str());
      return;
    }

    LOG_DBG("FileBrowser", "Deleted successfully");
    for (const auto& metadataPath : metadataPaths) {
      clearFileMetadata(metadataPath);
    }

    const std::string favoritePrefix = dirPath + "/";
    if (!APP_STATE.favoriteSleepImagePath.empty() && APP_STATE.favoriteSleepImagePath.rfind(favoritePrefix, 0) == 0) {
      unpinSleepFavorite();
    }

    loadFiles();
    if (files.empty()) {
      selectorIndex = 0;
    } else if (selectorIndex >= files.size()) {
      selectorIndex = files.size() - 1;
    }
    requestUpdate(true);
  };

  const std::string heading = tr(STR_DELETE) + std::string("? ");
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entry, ignoreInitialConfirmRelease),
      handler);
}

void FileBrowserActivity::pinSleepFavorite(const std::string& fullPath) {
  APP_STATE.favoriteSleepImagePath = fullPath;
  if (!APP_STATE.saveToFile()) {
    LOG_ERR("FileBrowser", "Failed to save favorite sleep image path: %s", fullPath.c_str());
    return;
  }
  LOG_INF("FileBrowser", "Pinned favorite sleep image: %s", fullPath.c_str());
  requestUpdate();
}

void FileBrowserActivity::unpinSleepFavorite() {
  if (APP_STATE.favoriteSleepImagePath.empty()) {
    return;
  }

  APP_STATE.favoriteSleepImagePath.clear();
  if (!APP_STATE.saveToFile()) {
    LOG_ERR("FileBrowser", "Failed to clear favorite sleep image path");
    return;
  }
  LOG_INF("FileBrowser", "Cleared favorite sleep image");
  requestUpdate();
}

bool FileBrowserActivity::isPinnedSleepFavorite(const std::string& fullPath) const {
  return APP_STATE.favoriteSleepImagePath == fullPath;
}

bool FileBrowserActivity::isEpubCompleted(const std::string& fullPath) const {
  const Epub epub(fullPath, "/.crosspoint");
  return BookReadingStats::load(epub.getCachePath()).isCompleted;
}

void FileBrowserActivity::toggleEpubCompleted(const std::string& fullPath, const std::string& entry) {
  Epub epub(fullPath, "/.crosspoint");
  epub.setupCacheDir();

  BookReadingStats stats = BookReadingStats::load(epub.getCachePath());
  const bool newCompleted = !stats.isCompleted;
  stats.isCompleted = newCompleted;

  GlobalReadingStats globalStats = GlobalReadingStats::load();
  if (newCompleted) {
    globalStats.completedBooks++;
  } else if (globalStats.completedBooks > 0) {
    globalStats.completedBooks--;
  }

  stats.save(epub.getCachePath());
  globalStats.save();

  completedFeedbackIsFinished = newCompleted;
  pendingCompletedFeedback = true;
  completedFeedbackShowTime = millis();

  if (newCompleted && SETTINGS.moveFinishedToReadFolder && fullPath.rfind("/Read/", 0) != 0) {
    const std::string oldCachePath = epub.getCachePath();
    const std::string dstPath = buildReadFolderDestination(fullPath);
    LOG_INF("FileBrowser", "Moving completed epub: %s -> %s", fullPath.c_str(), dstPath.c_str());
    if (!Storage.rename(fullPath.c_str(), dstPath.c_str())) {
      LOG_ERR("FileBrowser", "Failed to move book to 'Read' folder");
      snprintf(APP_STATE.pendingAlertTitle, sizeof(APP_STATE.pendingAlertTitle), "%s",
               tr(STR_MOVE_TO_READ_FAILED_TITLE));
      snprintf(APP_STATE.pendingAlertBody, sizeof(APP_STATE.pendingAlertBody), tr(STR_MOVE_TO_READ_FAILED_BODY),
               getFileName(entry).c_str());
      APP_STATE.pendingAlertGoHomeOnBack.store(false, std::memory_order_relaxed);
      APP_STATE.hasPendingAlert.store(true, std::memory_order_release);
      requestUpdate();
      return;
    }

    const std::string newCachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(dstPath));
    if (!oldCachePath.empty() && Storage.exists(oldCachePath.c_str())) {
      if (!Storage.rename(oldCachePath.c_str(), newCachePath.c_str())) {
        LOG_ERR("FileBrowser", "Failed to rename cache dir %s -> %s (non-fatal)", oldCachePath.c_str(),
                newCachePath.c_str());
      }
    }

    RECENT_BOOKS.updatePath(fullPath, dstPath, oldCachePath, newCachePath);
    if (APP_STATE.openEpubPath == fullPath) {
      APP_STATE.openEpubPath = dstPath;
      APP_STATE.saveToFile();
    }
  }

  loadFiles();
  selectorIndex = files.empty() ? 0 : std::min(selectorIndex, files.size() - 1);
  requestUpdate(true);
}

void FileBrowserActivity::showFileActionMenu(const std::string& entry, bool ignoreInitialConfirmRelease) {
  const std::string fullPath = buildFullPath(basepath, entry);
  std::vector<FileBrowserActionActivity::MenuItem> items;
  items.reserve(4);
  items.push_back({FileBrowserAction::Delete, StrId::STR_DELETE});
  if (hasClearableBookCache(fullPath)) {
    items.push_back({FileBrowserAction::DeleteCache, StrId::STR_DELETE_CACHE});
  }
  if (FsHelpers::hasEpubExtension(fullPath)) {
    items.push_back({FileBrowserAction::ToggleCompleted,
                     isEpubCompleted(fullPath) ? StrId::STR_MARK_UNFINISHED : StrId::STR_MARK_FINISHED});
  }

  const bool canPinFavorite = isSleepFolderPath(basepath) && isSleepImageFile(entry);
  if (canPinFavorite) {
    items.push_back(
        {isPinnedSleepFavorite(fullPath) ? FileBrowserAction::UnpinFavorite : FileBrowserAction::PinFavorite,
         isPinnedSleepFavorite(fullPath) ? StrId::STR_UNPIN_AS_FAVORITE : StrId::STR_PIN_AS_FAVORITE});
  }

  startActivityForResult(
      std::make_unique<FileBrowserActionActivity>(renderer, mappedInput, getFileName(entry), std::move(items),
                                                  ignoreInitialConfirmRelease),
      [this, fullPath, entry](const ActivityResult& result) {
        longPressConfirmHandled = false;
        if (result.isCancelled) {
          return;
        }

        const auto action = static_cast<FileBrowserAction>(std::get<FileBrowserActionResult>(result.data).action);
        switch (action) {
          case FileBrowserAction::Delete:
            promptDeleteFile(fullPath, entry);
            return;
          case FileBrowserAction::DeleteCache:
            if (!clearBookCache(fullPath)) {
              LOG_ERR("FileBrowser", "Failed to clear book cache for: %s", fullPath.c_str());
            } else {
              drawToast(renderer, tr(STR_BOOK_CACHE_DELETED));
              delay(1000);
            }
            requestUpdate();
            return;
          case FileBrowserAction::ToggleCompleted:
            toggleEpubCompleted(fullPath, entry);
            return;
          case FileBrowserAction::PinFavorite:
            if (FsHelpers::hasPngExtension(fullPath)) {
              startActivityForResult(
                  std::make_unique<ConfirmationActivity>(renderer, mappedInput, "", tr(STR_PIN_PNG_WARNING)),
                  [this, fullPath](const ActivityResult& confirmation) {
                    if (!confirmation.isCancelled) {
                      pinSleepFavorite(fullPath);
                    }
                  });
            } else {
              pinSleepFavorite(fullPath);
            }
            return;
          case FileBrowserAction::UnpinFavorite:
            unpinSleepFavorite();
            return;
        }
      });
}

void FileBrowserActivity::toggleHiddenFiles() {
  const std::string currentEntry =
      (!files.empty() && selectorIndex < files.size()) ? files[selectorIndex] : std::string();
  SETTINGS.showHiddenFiles = SETTINGS.showHiddenFiles ? 0 : 1;
  if (!SETTINGS.saveToFile()) {
    LOG_ERR("FileBrowser", "Failed to save showHiddenFiles=%u", SETTINGS.showHiddenFiles);
  }

  if (!SETTINGS.showHiddenFiles && containsHiddenPathSegment(basepath)) {
    basepath = "/";
  }

  loadFiles();
  selectorIndex = currentEntry.empty() ? 0 : findEntry(currentEntry);
  if (!files.empty() && selectorIndex >= files.size()) {
    selectorIndex = files.size() - 1;
  }
  requestUpdate();
}

void FileBrowserActivity::loop() {
  if (pendingCompletedFeedback) {
    const bool timedOut = (millis() - completedFeedbackShowTime) >= COMPLETED_FEEDBACK_MS;
    const bool navPressed = mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Down);
    if (timedOut || navPressed) {
      pendingCompletedFeedback = false;
      requestUpdate();
      return;
    }
  }

  // Long press BACK/HOME (1s+) toggles hidden files (Books mode only).
  // In firmware-pick mode we keep navigation simple: short Back = up dir / cancel.
  if (mode == Mode::Books && !longPressBackHandled && mappedInput.isPressed(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= GO_HOME_MS && !lockLongPressBack) {
    longPressBackHandled = true;
    if (isInBooksFolder()) {
      gridMode = !gridMode;
      gridSelectorIndex = 0;
      gridLoadedPage = -1;
      requestUpdate(true);
    } else {
      toggleHiddenFiles();
    }
    return;
  }

  if (lockLongPressBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
    return;
  }

  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
  int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, pathReserved);
  if (GUI.usesCompactFileBrowserRows()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight =
        renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
    pageItems = std::max(1, contentHeight / GUI.compactFileBrowserRowHeight(renderer));
  }

  if (!files.empty()) {
    const std::string& entry = files[selectorIndex];
    const bool isDirectory = (entry.back() == '/');
    if (mode == Mode::Books && !longPressConfirmHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        mappedInput.getHeldTime() >= GO_HOME_MS) {
      longPressConfirmHandled = true;
      if (isDirectory) {
        promptDeleteDirectory(buildFullPath(basepath, entry), entry, true);
      } else {
        showFileActionMenu(entry, true);
      }
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (longPressConfirmHandled) {
      longPressConfirmHandled = false;
      return;
    }
    if (lockNextConfirmRelease) {
      lockNextConfirmRelease = false;
      return;
    }
    if (files.empty()) return;

    if (gridMode && isInBooksFolder()) {
      size_t epubIdx = 0;
      for (const auto& f : files) {
        if (!FsHelpers::hasEpubExtension(f)) continue;
        if (epubIdx == gridSelectorIndex) {
          onSelectBook(buildFullPath(basepath, f));
          return;
        }
        epubIdx++;
      }
      return;
    }

    const std::string& entry = files[selectorIndex];
    bool isDirectory = (entry.back() == '/');

    // Firmware picker: select file -> return path; navigate into directories normally.
    if (mode == Mode::PickFirmware && !isDirectory) {
      std::string cleanBasePath = basepath;
      if (cleanBasePath.back() != '/') cleanBasePath += "/";
      ActivityResult res{FilePathResult{cleanBasePath + entry}};
      res.isCancelled = false;
      setResult(std::move(res));
      finish();
      return;
    }

    if (mode == Mode::Books && mappedInput.getHeldTime() >= GO_HOME_MS) {
      if (isDirectory) {
        promptDeleteDirectory(buildFullPath(basepath, entry), entry);
      } else {
        showFileActionMenu(entry);
      }
      return;
    } else {
      // --- SHORT PRESS ACTION: OPEN/NAVIGATE ---
      if (basepath.back() != '/') basepath += "/";

      if (isDirectory) {
        basepath += entry.substr(0, entry.length() - 1);
        loadFiles();
        selectorIndex = 0;
        requestUpdate();
      } else {
        onSelectBook(basepath + entry);
      }
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (longPressBackHandled) {
      longPressBackHandled = false;
      return;
    }
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        selectorIndex = findEntry(dirName);

        requestUpdate();
      } else if (mode == Mode::PickFirmware) {
        // Firmware picker at root: cancel back to caller instead of going home.
        ActivityResult res;
        res.isCancelled = true;
        setResult(std::move(res));
        finish();
      } else {
        onGoHome();
      }
    }
  }

  if (gridMode && isInBooksFolder()) {
    size_t epubCount = 0;
    for (const auto& f : files) {
      if (FsHelpers::hasEpubExtension(f)) epubCount++;
    }
    const int gridSize = static_cast<int>(epubCount);
    buttonNavigator.onNextRelease([this, gridSize] {
      gridSelectorIndex = static_cast<size_t>(
          ButtonNavigator::nextIndex(static_cast<int>(gridSelectorIndex), gridSize));
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this, gridSize] {
      gridSelectorIndex = static_cast<size_t>(
          ButtonNavigator::previousIndex(static_cast<int>(gridSelectorIndex), gridSize));
      requestUpdate();
    });
    buttonNavigator.onNextContinuous([this, gridSize] {
      gridSelectorIndex = static_cast<size_t>(
          ButtonNavigator::nextPageIndex(static_cast<int>(gridSelectorIndex), gridSize, GRID_BOOKS_PER_PAGE));
      requestUpdate();
    });
    buttonNavigator.onPreviousContinuous([this, gridSize] {
      gridSelectorIndex = static_cast<size_t>(
          ButtonNavigator::previousPageIndex(static_cast<int>(gridSelectorIndex), gridSize, GRID_BOOKS_PER_PAGE));
      requestUpdate();
    });
  } else {
    int listSize = static_cast<int>(files.size());
    buttonNavigator.onNextRelease([this, listSize] {
      selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this, listSize] {
      selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
      requestUpdate();
    });
    buttonNavigator.onNextContinuous([this, listSize, pageItems] {
      selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
      requestUpdate();
    });
    buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
      selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
      requestUpdate();
    });
  }
}

namespace {

std::string getFileName(std::string filename) {
  if (filename.back() == '/') {
    filename.pop_back();
    if (!UITheme::getInstance().getTheme().showsFileIcons()) {
      return "[" + filename + "]";
    }
    return filename;
  }
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

std::string getFileExtension(std::string filename) {
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  return filename.substr(pos);
}

}  // namespace

void FileBrowserActivity::render(RenderLock&&) {
  if (gridMode && isInBooksFolder()) {
    renderBooksGrid();
    return;
  }

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName =
      (mode == Mode::PickFirmware)
          ? std::string(tr(STR_SELECT_FIRMWARE_FILE))
          : ((basepath == "/") ? std::string(tr(STR_SD_CARD)) : basepath.substr(basepath.rfind('/') + 1));
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
  const int pathMaxWidth = pageWidth - metrics.contentSidePadding * 2;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  if (files.empty()) {
    const char* emptyMsg = (mode == Mode::PickFirmware) ? tr(STR_NO_BIN_FILES) : tr(STR_NO_FILES_FOUND);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, emptyMsg);
  } else {
    const bool compactFileRows = GUI.usesCompactFileBrowserRows();
    const std::function<std::string(int)> compactRowMarker =
        compactFileRows ? [this](int index) { return files[index].back() == '/' ? "folder" : ""; }
                        : std::function<std::string(int)>{};
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, files.size(), selectorIndex,
        [this](int index) { return getFileName(files[index]); }, compactRowMarker,
        [this](int index) { return UITheme::getFileIcon(files[index]); },
        [this](int index) {
          const std::string extension = getFileExtension(files[index]);
          const std::string fullPath = buildFullPath(basepath, files[index]);
          if (isPinnedSleepFavorite(fullPath)) {
            return extension.empty() ? "*" : "* " + extension;
          }
          return extension;
        },
        false);
  }

  // Full path display
  {
    const int separatorY = pathY - metrics.verticalSpacing / 2;
    renderer.drawLine(0, separatorY, pageWidth - 1, separatorY, 3, true);
    // Left-truncate so the deepest directory is always visible
    const char* pathStr = basepath.c_str();
    const char* pathDisplay = pathStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = pathMaxWidth - ellipsisWidth;
      // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
      const char* p = pathStr;
      while (*p) {
        if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      pathDisplay = leftTruncBuf;
    }
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, pathY, pathDisplay);
  }

  // Help text
  const char* backLabel = (basepath == "/") ? (mode == Mode::PickFirmware ? tr(STR_BACK) : tr(STR_HOME)) : tr(STR_BACK);
  // In PickFirmware mode, Confirm on a .bin returns the path to the caller (not "open"); show
  // STR_SELECT instead. Directories in the same picker still descend, so keep STR_OPEN there.
  const bool selectingFirmwareFile = mode == Mode::PickFirmware && !files.empty() && files[selectorIndex].back() != '/';
  const char* confirmLabel = files.empty() ? "" : (selectingFirmwareFile ? tr(STR_SELECT) : tr(STR_OPEN));
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, files.empty() ? "" : tr(STR_DIR_UP),
                                            files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (mode == Mode::Books && basepath == "/") {
    const int usedPathWidth = renderer.getTextWidth(SMALL_FONT_ID, basepath.c_str());
    const int hintMaxWidth = pathMaxWidth - usedPathWidth - ROOT_HINT_GAP;
    const auto hint = renderer.truncatedText(SMALL_FONT_ID, tr(STR_TOGGLE_HIDDEN_FILES_HINT), hintMaxWidth);
    const int hintWidth = renderer.getTextWidth(SMALL_FONT_ID, hint.c_str());
    renderer.drawText(SMALL_FONT_ID, pageWidth - metrics.contentSidePadding - hintWidth, pathY, hint.c_str());
  } else if (mode == Mode::Books && isInBooksFolder()) {
    const int usedPathWidth = renderer.getTextWidth(SMALL_FONT_ID, basepath.c_str());
    const int hintMaxWidth = pathMaxWidth - usedPathWidth - ROOT_HINT_GAP;
    const auto hint = renderer.truncatedText(SMALL_FONT_ID, tr(STR_TOGGLE_COVER_GRID_HINT), hintMaxWidth);
    const int hintWidth = renderer.getTextWidth(SMALL_FONT_ID, hint.c_str());
    renderer.drawText(SMALL_FONT_ID, pageWidth - metrics.contentSidePadding - hintWidth, pathY, hint.c_str());
  }

  if (pendingCompletedFeedback) {
    GUI.drawPopup(renderer, completedFeedbackIsFinished ? tr(STR_MARKED_FINISHED) : tr(STR_MARKED_UNFINISHED));
  }

  renderer.displayBuffer();
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}

void FileBrowserActivity::generateGridCovers(int pageStart, const std::vector<std::string>& epubs) {
  const int pageEnd = std::min(pageStart + GRID_BOOKS_PER_PAGE, static_cast<int>(epubs.size()));

  bool needsGeneration = false;
  for (int i = pageStart; i < pageEnd; ++i) {
    Epub epub(buildFullPath(basepath, epubs[i]), "/.crosspoint");
    const std::string thumbPath = epub.getThumbBmpPath(GRID_COVER_WIDTH, GRID_COVER_HEIGHT);
    if (thumbPath.empty() || !Storage.exists(thumbPath.c_str())) {
      needsGeneration = true;
      break;
    }
  }
  if (!needsGeneration) {
    gridLoadedPage = pageStart;
    return;
  }

  const Rect popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  const int total = pageEnd - pageStart;
  int done = 0;

  for (int i = pageStart; i < pageEnd; ++i) {
    const std::string fullPath = buildFullPath(basepath, epubs[i]);
    Epub epub(fullPath, "/.crosspoint");
    const std::string thumbPath = epub.getThumbBmpPath(GRID_COVER_WIDTH, GRID_COVER_HEIGHT);
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      done++;
      continue;
    }
    GUI.fillPopupProgress(renderer, popupRect, 10 + done * (90 / total));
    epub.generateThumbBmpFast(GRID_COVER_WIDTH, GRID_COVER_HEIGHT);
    done++;
  }

  gridLoadedPage = pageStart;
  requestUpdate();
}

void FileBrowserActivity::renderBooksGrid() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const std::string folderName = basepath.substr(basepath.rfind('/') + 1);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  // Collect epub files only
  std::vector<std::string> epubs;
  for (const auto& f : files) {
    if (FsHelpers::hasEpubExtension(f)) epubs.push_back(f);
  }

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int titleLineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int cellH = GRID_COVER_HEIGHT + GRID_TITLE_GAP + titleLineH;
  const int totalGridWidth = GRID_COLS * GRID_COVER_WIDTH + (GRID_COLS - 1) * GRID_SPACING;
  const int startX = (pageWidth - totalGridWidth) / 2;

  if (epubs.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_FILES_FOUND));
  } else {
    const int totalEpubs = static_cast<int>(epubs.size());
    const int totalPages = (totalEpubs + GRID_BOOKS_PER_PAGE - 1) / GRID_BOOKS_PER_PAGE;
    const int currentPage = static_cast<int>(gridSelectorIndex) / GRID_BOOKS_PER_PAGE;
    const int pageStart = currentPage * GRID_BOOKS_PER_PAGE;
    const int pageCount = std::min(GRID_BOOKS_PER_PAGE, totalEpubs - pageStart);

    for (int i = 0; i < pageCount; ++i) {
      const int epubIdx = pageStart + i;
      const int col = i % GRID_COLS;
      const int row = i / GRID_COLS;
      const int x = startX + col * (GRID_COVER_WIDTH + GRID_SPACING);
      const int y = contentTop + row * (cellH + GRID_SPACING);

      const std::string fullPath = buildFullPath(basepath, epubs[epubIdx]);
      Epub epub(fullPath, "/.crosspoint");
      const std::string thumbPath = epub.getThumbBmpPath(GRID_COVER_WIDTH, GRID_COVER_HEIGHT);

      int bx = x, by = y, bw = GRID_COVER_WIDTH, bh = GRID_COVER_HEIGHT;
      bool drawn = false;

      if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
        FsFile file;
        if (Storage.openFileForRead("FBGrid", thumbPath, file)) {
          Bitmap bmp(file);
          if (bmp.parseHeaders() == BmpReaderError::Ok && bmp.getWidth() > 0 && bmp.getHeight() > 0) {
            bw = std::min(GRID_COVER_WIDTH, bmp.getWidth());
            bh = std::min(GRID_COVER_HEIGHT, bmp.getHeight());
            bx = x + (GRID_COVER_WIDTH - bw) / 2;
            by = y + (GRID_COVER_HEIGHT - bh) / 2;
            renderer.drawBitmap(bmp, bx, by, bw, bh);
            renderer.drawRect(bx, by, bw, bh, 2, true);
            drawn = true;
          }
          file.close();
        }
      }
      if (!drawn) {
        renderer.drawRect(bx, by, bw, bh, 2, true);
        renderer.fillRect(bx + 2, by + 2, bw - 4, bh - 4, false);
        renderer.drawIcon(BookIcon, bx + (bw - 32) / 2, by + (bh - 32) / 2, 32, 32);
      }
      if (epubIdx == static_cast<int>(gridSelectorIndex)) {
        renderer.drawRect(bx - 2, by - 2, bw + 4, bh + 4, 3, true);
      }

      // Title centred below cover
      const int titleX = x;
      const int titleY = y + GRID_COVER_HEIGHT + GRID_TITLE_GAP;
      const std::string title = getFileName(epubs[epubIdx]);
      const std::string truncTitle = renderer.truncatedText(SMALL_FONT_ID, title.c_str(), GRID_COVER_WIDTH);
      const int textW = renderer.getTextWidth(SMALL_FONT_ID, truncTitle.c_str());
      renderer.drawText(SMALL_FONT_ID, titleX + (GRID_COVER_WIDTH - textW) / 2, titleY, truncTitle.c_str());
    }

    if (totalPages > 1) {
      constexpr int dotSize = 10;
      constexpr int dotSpacing = 8;
      const int totalDotWidth = totalPages * dotSize + (totalPages - 1) * dotSpacing;
      const int dotsStartX = (pageWidth - totalDotWidth) / 2;
      const int dotY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - 4;
      for (int p = 0; p < totalPages; p++) {
        const int dx = dotsStartX + p * (dotSize + dotSpacing);
        if (p == currentPage) {
          renderer.fillRoundedRect(dx, dotY, dotSize, dotSize, dotSize / 2, Color::Black);
        } else {
          renderer.drawRoundedRect(dx, dotY, dotSize, dotSize, 1, dotSize / 2, true);
        }
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), epubs.empty() ? "" : tr(STR_OPEN),
                                            tr(STR_DIR_PREV), tr(STR_DIR_NEXT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  // Generate missing covers after first paint so the user sees placeholder
  // slots immediately, then the loading popup appears as covers are generated.
  if (!epubs.empty()) {
    const int currentPage = static_cast<int>(gridSelectorIndex) / GRID_BOOKS_PER_PAGE;
    const int pageStart = currentPage * GRID_BOOKS_PER_PAGE;
    if (gridLoadedPage != pageStart) {
      generateGridCovers(pageStart, epubs);
    }
  }
}
