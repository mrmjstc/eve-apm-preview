#include "protocolhandler.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QMessageBox>
#include <QSettings>
#include <QUrl>
#include <Windows.h>

static const QString PROTOCOL_NAME = "eveapm";
static const QString REGISTRY_BASE =
    "HKEY_CURRENT_USER\\Software\\Classes\\eveapm";

ProtocolHandler::ProtocolHandler(QObject *parent) : QObject(parent) {}

ProtocolHandler::~ProtocolHandler() {}

bool ProtocolHandler::handleUrl(const QString &url) {
  if (url.isEmpty()) {
    emit invalidUrl(url, "Empty URL");
    return false;
  }

  QUrl parsedUrl(url);

  if (!parsedUrl.isValid()) {
    emit invalidUrl(url, "Invalid URL format");
    qWarning() << "ProtocolHandler: Invalid URL:" << url;
    return false;
  }

  if (parsedUrl.scheme().toLower() != PROTOCOL_NAME) {
    emit invalidUrl(url, QString("Wrong scheme: expected '%1', got '%2'")
                             .arg(PROTOCOL_NAME)
                             .arg(parsedUrl.scheme()));
    qWarning() << "ProtocolHandler: Wrong scheme:" << parsedUrl.scheme();
    return false;
  }

  return parseAndEmit(parsedUrl);
}

bool ProtocolHandler::parseAndEmit(const QUrl &url) {
  QString host = url.host().toLower();
  QString path = url.path();

  // Remove leading slash
  if (path.startsWith('/')) {
    path = path.mid(1);
  }

  // URL decode the path
  QString decoded = QUrl::fromPercentEncoding(path.toUtf8());

  if (host == "profile") {
    if (decoded.isEmpty()) {
      emit invalidUrl(url.toString(), "Empty profile name");
      qWarning() << "ProtocolHandler: Empty profile name";
      return false;
    }

    if (!isValidProfileName(decoded)) {
      emit invalidUrl(url.toString(),
                      QString("Invalid profile name: '%1'").arg(decoded));
      qWarning() << "ProtocolHandler: Invalid profile name:" << decoded;
      return false;
    }

    qDebug() << "ProtocolHandler: Profile switch requested:" << decoded;
    emit profileRequested(decoded);
    return true;

  } else if (host == "character") {
    if (decoded.isEmpty()) {
      emit invalidUrl(url.toString(), "Empty character name");
      qWarning() << "ProtocolHandler: Empty character name";
      return false;
    }

    if (!isValidCharacterName(decoded)) {
      emit invalidUrl(url.toString(),
                      QString("Invalid character name: '%1'").arg(decoded));
      qWarning() << "ProtocolHandler: Invalid character name:" << decoded;
      return false;
    }

    qDebug() << "ProtocolHandler: Character activation requested:" << decoded;
    emit characterRequested(decoded);
    return true;

  } else if (host == "hotkey") {
    if (decoded == "suspend") {
      qDebug() << "ProtocolHandler: Hotkey suspend requested";
      emit hotkeySuspendRequested();
      return true;
    } else if (decoded == "resume") {
      qDebug() << "ProtocolHandler: Hotkey resume requested";
      emit hotkeyResumeRequested();
      return true;
    } else {
      emit invalidUrl(url.toString(),
                      QString("Unknown hotkey action: '%1'").arg(decoded));
      qWarning() << "ProtocolHandler: Unknown hotkey action:" << decoded;
      return false;
    }

  } else if (host == "thumbnail") {
    if (decoded == "hide") {
      qDebug() << "ProtocolHandler: Thumbnail hide requested";
      emit thumbnailHideRequested();
      return true;
    } else if (decoded == "show") {
      qDebug() << "ProtocolHandler: Thumbnail show requested";
      emit thumbnailShowRequested();
      return true;
    } else {
      emit invalidUrl(url.toString(),
                      QString("Unknown thumbnail action: '%1'").arg(decoded));
      qWarning() << "ProtocolHandler: Unknown thumbnail action:" << decoded;
      return false;
    }

  } else if (host == "config") {
    if (decoded.isEmpty() || decoded == "open") {
      qDebug() << "ProtocolHandler: Config dialog open requested";
      emit configOpenRequested();
      return true;
    } else {
      emit invalidUrl(url.toString(),
                      QString("Unknown config action: '%1'").arg(decoded));
      qWarning() << "ProtocolHandler: Unknown config action:" << decoded;
      return false;
    }

  } else {
    emit invalidUrl(url.toString(), QString("Unknown action: '%1'").arg(host));
    qWarning() << "ProtocolHandler: Unknown action:" << host;
    return false;
  }
}

bool ProtocolHandler::registerProtocol() {
  QString exePath = getExecutablePath();

  qDebug() << "ProtocolHandler: Registering protocol with executable:"
           << exePath;

  // Create main protocol key
  if (!setRegistryValue("HKEY_CURRENT_USER\\Software\\Classes\\eveapm", "",
                        "URL:EVE APM Protocol")) {
    qWarning() << "ProtocolHandler: Failed to set main protocol key";
    return false;
  }

  // Set URL Protocol marker
  if (!setRegistryValue("HKEY_CURRENT_USER\\Software\\Classes\\eveapm",
                        "URL Protocol", "")) {
    qWarning() << "ProtocolHandler: Failed to set URL Protocol marker";
    return false;
  }

  // Set command
  QString commandValue = QString("\"%1\" \"%2\"").arg(exePath).arg("%1");
  if (!setRegistryValue(
          "HKEY_CURRENT_USER\\Software\\Classes\\eveapm\\shell\\open\\command",
          "", commandValue)) {
    qWarning() << "ProtocolHandler: Failed to set command key";
    return false;
  }

  qDebug() << "ProtocolHandler: Protocol registered successfully";
  return true;
}

bool ProtocolHandler::unregisterProtocol() {
  qDebug() << "ProtocolHandler: Unregistering protocol";
  return deleteRegistryKey("HKEY_CURRENT_USER\\Software\\Classes\\eveapm");
}

bool ProtocolHandler::isProtocolRegistered() const {
  QString value =
      getRegistryValue("HKEY_CURRENT_USER\\Software\\Classes\\eveapm", "", "");
  return !value.isEmpty();
}

QString ProtocolHandler::getExecutablePath() const {
  QString exePath = QCoreApplication::applicationFilePath();
  // Normalize path separators for Windows
  exePath = QDir::toNativeSeparators(exePath);
  return exePath;
}

bool ProtocolHandler::isValidProfileName(const QString &profileName) const {
  if (profileName.isEmpty() || profileName.length() > 100) {
    return false;
  }

  // Allow alphanumeric, spaces, hyphens, underscores
  // Disallow path separators and other dangerous characters
  for (const QChar &ch : profileName) {
    if (!ch.isLetterOrNumber() && ch != ' ' && ch != '-' && ch != '_') {
      return false;
    }
  }

  return true;
}

bool ProtocolHandler::isValidCharacterName(const QString &characterName) const {
  if (characterName.isEmpty() || characterName.length() > 100) {
    return false;
  }

  // EVE character names can contain letters, spaces, hyphens, apostrophes
  for (const QChar &ch : characterName) {
    if (!ch.isLetterOrNumber() && ch != ' ' && ch != '-' && ch != '\'') {
      return false;
    }
  }

  return true;
}

bool ProtocolHandler::setRegistryValue(const QString &keyPath,
                                       const QString &valueName,
                                       const QString &data) {
  // Parse the registry path
  QString path = keyPath;
  if (path.startsWith("HKEY_CURRENT_USER\\")) {
    path = path.mid(18); // Remove "HKEY_CURRENT_USER\\"
  }

  // Create/open the registry key
  HKEY hKey;
  LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, path.toStdWString().c_str(),
                                0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE,
                                nullptr, &hKey, nullptr);

  if (result != ERROR_SUCCESS) {
    qWarning() << "Failed to create/open registry key:" << keyPath
               << "Error:" << result;
    return false;
  }

  // Set the value
  std::wstring wValueName =
      valueName.isEmpty() ? L"" : valueName.toStdWString();
  std::wstring wData = data.toStdWString();

  result = RegSetValueExW(
      hKey, valueName.isEmpty() ? nullptr : wValueName.c_str(), 0, REG_SZ,
      reinterpret_cast<const BYTE *>(wData.c_str()),
      static_cast<DWORD>((wData.length() + 1) * sizeof(wchar_t)));

  RegCloseKey(hKey);

  if (result != ERROR_SUCCESS) {
    qWarning() << "Failed to set registry value:" << valueName << "in"
               << keyPath << "Error:" << result;
    return false;
  }

  qDebug() << "Set registry value:" << keyPath << "/" << valueName << "="
           << data;
  return true;
}

bool ProtocolHandler::deleteRegistryKey(const QString &keyPath) {
  // Delete using Windows API for complete removal
  QString path = keyPath;
  if (path.startsWith("HKEY_CURRENT_USER\\")) {
    path = path.mid(18); // Remove "HKEY_CURRENT_USER\\"
  }

  LONG result = RegDeleteTreeW(HKEY_CURRENT_USER, path.toStdWString().c_str());

  if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND) {
    qDebug() << "ProtocolHandler: Registry key deleted:" << keyPath;
    return true;
  } else {
    qWarning() << "ProtocolHandler: Failed to delete registry key:" << keyPath
               << "Error:" << result;
    return false;
  }
}

QString ProtocolHandler::getRegistryValue(const QString &keyPath,
                                          const QString &valueName,
                                          const QString &defaultValue) const {
  // Parse the registry path
  QString path = keyPath;
  if (path.startsWith("HKEY_CURRENT_USER\\")) {
    path = path.mid(18); // Remove "HKEY_CURRENT_USER\\"
  }

  // Open the registry key
  HKEY hKey;
  LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, path.toStdWString().c_str(), 0,
                              KEY_READ, &hKey);

  if (result != ERROR_SUCCESS) {
    return defaultValue;
  }

  // Query the value
  wchar_t buffer[1024];
  DWORD bufferSize = sizeof(buffer);
  DWORD type;

  std::wstring wValueName =
      valueName.isEmpty() ? L"" : valueName.toStdWString();

  result = RegQueryValueExW(
      hKey, valueName.isEmpty() ? nullptr : wValueName.c_str(), nullptr, &type,
      reinterpret_cast<BYTE *>(buffer), &bufferSize);

  RegCloseKey(hKey);

  if (result != ERROR_SUCCESS || type != REG_SZ) {
    return defaultValue;
  }

  return QString::fromWCharArray(buffer);
}
