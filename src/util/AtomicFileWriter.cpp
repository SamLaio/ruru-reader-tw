#include "AtomicFileWriter.h"

#include <Arduino.h>
#include <SDCardManager.h>

namespace {
std::string tmpPathFor(const std::string& path) { return path + ".tmp"; }
std::string bakPathFor(const std::string& path) { return path + ".bak"; }

bool commitAtomicFile(const char* moduleName, const std::string& path) {
  const std::string tmpPath = tmpPathFor(path);
  const std::string bakPath = bakPathFor(path);

  if (SdMan.exists(bakPath.c_str())) {
    SdMan.remove(bakPath.c_str());
  }

  if (SdMan.exists(path.c_str()) && !SdMan.rename(path.c_str(), bakPath.c_str())) {
    Serial.printf("[%lu] [%s] Atomic write failed, cannot move old file: %s\n", millis(), moduleName, path.c_str());
    SdMan.remove(tmpPath.c_str());
    return false;
  }

  if (!SdMan.rename(tmpPath.c_str(), path.c_str())) {
    Serial.printf("[%lu] [%s] Atomic write failed, cannot move tmp file: %s\n", millis(), moduleName, path.c_str());
    if (SdMan.exists(bakPath.c_str())) {
      SdMan.rename(bakPath.c_str(), path.c_str());
    }
    SdMan.remove(tmpPath.c_str());
    return false;
  }

  if (SdMan.exists(bakPath.c_str())) {
    SdMan.remove(bakPath.c_str());
  }
  return true;
}
}  // namespace

bool writeBinaryFileAtomic(const char* moduleName, const std::string& path, const uint8_t* data, size_t size) {
  if (!data && size > 0) {
    return false;
  }

  const std::string tmpPath = tmpPathFor(path);
  if (SdMan.exists(tmpPath.c_str())) {
    SdMan.remove(tmpPath.c_str());
  }

  FsFile file;
  if (!SdMan.openFileForWrite(moduleName, tmpPath, file)) {
    return false;
  }

  const size_t written = file.write(data, size);
  file.close();
  if (written != size) {
    SdMan.remove(tmpPath.c_str());
    Serial.printf("[%lu] [%s] Atomic write failed, short write: %s\n", millis(), moduleName, path.c_str());
    return false;
  }

  return commitAtomicFile(moduleName, path);
}

bool writeTextFileAtomic(const char* moduleName, const std::string& path, const std::string& payload) {
  return writeBinaryFileAtomic(moduleName, path, reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
}
