#include "RecentBooksStore.h"

#include <Epub.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include <Xtc.h>

#include <algorithm>

#include "util/StringUtils.h"

namespace {
constexpr uint8_t RECENT_BOOKS_FILE_VERSION = 4;
constexpr char RECENT_BOOKS_FILE[] = "/.crosspoint/recent.bin";
constexpr char RECENT_BOOKS_TMP_FILE[] = "/.crosspoint/recent.tmp";
constexpr char RECENT_BOOKS_BAK_FILE[] = "/.crosspoint/recent.bak";
constexpr int MAX_RECENT_BOOKS = 10;

template <typename T>
bool readPodChecked(FsFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

bool readStringChecked(FsFile& file, std::string& s) {
  uint32_t len = 0;
  if (!readPodChecked(file, len)) {
    return false;
  }
  if (len > 4096 || file.available() < len) {
    return false;
  }
  s.resize(len);
  if (len == 0) {
    return true;
  }
  return file.read(reinterpret_cast<uint8_t*>(&s[0]), len) == static_cast<int>(len);
}

bool loadRecentBooksFromPath(const char* path, std::vector<RecentBook>& outBooks) {
  FsFile inputFile;
  if (!SdMan.openFileForRead("RBS", path, inputFile)) {
    return false;
  }

  uint8_t version = 0;
  if (!readPodChecked(inputFile, version)) {
    inputFile.close();
    return false;
  }

  if (version > RECENT_BOOKS_FILE_VERSION || version == 0) {
    Serial.printf("[%lu] [RBS] Deserialization failed: Unknown version %u\n", millis(), version);
    inputFile.close();
    return false;
  }

  uint8_t count = 0;
  if (!readPodChecked(inputFile, count) || count > MAX_RECENT_BOOKS) {
    Serial.printf("[%lu] [RBS] Deserialization failed: invalid count %u\n", millis(), count);
    inputFile.close();
    return false;
  }

  std::vector<RecentBook> parsedBooks;
  parsedBooks.reserve(count);

  for (uint8_t i = 0; i < count; i++) {
    std::string pathValue;
    if (!readStringChecked(inputFile, pathValue) || pathValue.empty()) {
      inputFile.close();
      return false;
    }

    if (version == 1) {
      RecentBook book = RECENT_BOOKS.getDataFromBook(pathValue);
      parsedBooks.push_back(book);
      continue;
    }

    std::string title;
    std::string author;
    if (!readStringChecked(inputFile, title) || !readStringChecked(inputFile, author)) {
      inputFile.close();
      return false;
    }

    if (version == 2) {
      RecentBook book = RECENT_BOOKS.getDataFromBook(pathValue);
      if (book.title.empty() && book.author.empty()) {
        parsedBooks.push_back({pathValue, title, author, ""});
      } else {
        parsedBooks.push_back(book);
      }
      continue;
    }

    std::string coverBmpPath;
    if (!readStringChecked(inputFile, coverBmpPath)) {
      inputFile.close();
      return false;
    }
    int16_t progressPercent = -1;
    if (version >= 4) {
      if (!readPodChecked(inputFile, progressPercent)) {
        inputFile.close();
        return false;
      }
    }
    parsedBooks.push_back({pathValue, title, author, coverBmpPath, progressPercent});
  }

  inputFile.close();
  outBooks = std::move(parsedBooks);
  return true;
}
}  // namespace

RecentBooksStore RecentBooksStore::instance;

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string& coverBmpPath) {
  if (recentBooks.empty()) {
    loadFromFile();
  }

  // Remove existing entry if present
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    recentBooks.erase(it);
  }

  // Add to front
  recentBooks.insert(recentBooks.begin(), {path, title, author, coverBmpPath});

  // Trim to max size
  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }

  saveToFile();
}

void RecentBooksStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                  const std::string& coverBmpPath) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    RecentBook& book = *it;
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    saveToFile();
  }
}

void RecentBooksStore::updateProgress(const std::string& path, int progressPercent) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    it->progressPercent = progressPercent;
    saveToFile();
  }
}

bool RecentBooksStore::saveToFile() const {
  // Make sure the directory exists
  SdMan.mkdir("/.crosspoint");

  FsFile outputFile;
  if (!SdMan.openFileForWrite("RBS", RECENT_BOOKS_TMP_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, RECENT_BOOKS_FILE_VERSION);
  const uint8_t count = static_cast<uint8_t>(recentBooks.size());
  serialization::writePod(outputFile, count);

  for (const auto& book : recentBooks) {
    serialization::writeString(outputFile, book.path);
    serialization::writeString(outputFile, book.title);
    serialization::writeString(outputFile, book.author);
    serialization::writeString(outputFile, book.coverBmpPath);
    const int16_t prog = static_cast<int16_t>(book.progressPercent);
    serialization::writePod(outputFile, prog);
  }

  outputFile.sync();
  outputFile.close();

  if (SdMan.exists(RECENT_BOOKS_BAK_FILE)) {
    SdMan.remove(RECENT_BOOKS_BAK_FILE);
  }

  if (SdMan.exists(RECENT_BOOKS_FILE) && !SdMan.rename(RECENT_BOOKS_FILE, RECENT_BOOKS_BAK_FILE)) {
    Serial.printf("[%lu] [RBS] Failed to rotate recent books backup\n", millis());
    SdMan.remove(RECENT_BOOKS_TMP_FILE);
    return false;
  }

  if (!SdMan.rename(RECENT_BOOKS_TMP_FILE, RECENT_BOOKS_FILE)) {
    Serial.printf("[%lu] [RBS] Failed to commit recent books file\n", millis());
    if (SdMan.exists(RECENT_BOOKS_BAK_FILE)) {
      SdMan.rename(RECENT_BOOKS_BAK_FILE, RECENT_BOOKS_FILE);
    }
    SdMan.remove(RECENT_BOOKS_TMP_FILE);
    return false;
  }

  Serial.printf("[%lu] [RBS] Recent books saved to file (%d entries)\n", millis(), count);
  return true;
}

RecentBook RecentBooksStore::getDataFromBook(std::string path) const {
  std::string lastBookFileName = "";
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash != std::string::npos) {
    lastBookFileName = path.substr(lastSlash + 1);
  }

  Serial.printf("Loading recent book: %s\n", path.c_str());

  // If epub, try to load the metadata for title/author and cover
  if (StringUtils::checkFileExtension(lastBookFileName, ".epub")) {
    Epub epub(path, "/.crosspoint");
    epub.load(false);
    return RecentBook{path, epub.getTitle(), epub.getAuthor(), epub.getThumbBmpPath()};
  } else if (StringUtils::checkFileExtension(lastBookFileName, ".xtch") ||
             StringUtils::checkFileExtension(lastBookFileName, ".xtc")) {
    // Handle XTC file
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      return RecentBook{path, xtc.getTitle(), xtc.getAuthor(), xtc.getThumbBmpPath()};
    }
  } else if (StringUtils::checkFileExtension(lastBookFileName, ".txt") ||
             StringUtils::checkFileExtension(lastBookFileName, ".md")) {
    return RecentBook{path, lastBookFileName, "", ""};
  }
  return RecentBook{path, "", "", ""};
}

bool RecentBooksStore::loadFromFile() {
  std::vector<RecentBook> parsedBooks;
  if (!loadRecentBooksFromPath(RECENT_BOOKS_FILE, parsedBooks)) {
    Serial.printf("[%lu] [RBS] Failed to load recent books, trying backup\n", millis());
    if (!loadRecentBooksFromPath(RECENT_BOOKS_BAK_FILE, parsedBooks)) {
      Serial.printf("[%lu] [RBS] Failed to load recent books backup\n", millis());
      return false;
    }
  }

  recentBooks = std::move(parsedBooks);
  Serial.printf("[%lu] [RBS] Recent books loaded from file (%d entries)\n", millis(), recentBooks.size());
  return true;
}
