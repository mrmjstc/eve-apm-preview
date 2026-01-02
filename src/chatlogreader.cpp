#include "chatlogreader.h"
#include "config.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

ChatLogWorker::ChatLogWorker(QObject *parent)
    : QObject(parent), m_fileWatcher(new QFileSystemWatcher(this)),
      m_scanTimer(new QTimer(this)), m_running(false),
      m_enableChatLogMonitoring(true), m_enableGameLogMonitoring(true) {
  connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this,
          &ChatLogWorker::markFileDirty);

  connect(m_fileWatcher, &QFileSystemWatcher::directoryChanged, this,
          &ChatLogWorker::checkForNewFiles);

  // Periodic fallback scan in case directory watching misses file creation
  // (e.g., on some network drives or during rapid file operations)
  connect(m_scanTimer, &QTimer::timeout, this,
          &ChatLogWorker::checkForNewFiles);
  m_scanTimer->setInterval(300000); // 5 minutes

  updateCustomNameCache();
}

static QString normalizeLogLine(const QString &line) {
  static const QRegularExpression controlCharsPattern(R"([\x00-\x1F\x7F])");
  static const QRegularExpression zeroWidthPattern(
      QString::fromUtf8("[\uFEFF\u200B\u200C\u200D\u2060]"),
      QRegularExpression::UseUnicodePropertiesOption);

  if (!zeroWidthPattern.isValid()) {
    qWarning() << "Invalid zeroWidthPattern:" << zeroWidthPattern.errorString();
  }

  QString s = line;

  bool needsNormalization = false;
  for (const QChar &ch : line) {
    ushort code = ch.unicode();
    if (code <= 0x1F || code == 0x7F || code == 0xFEFF || code == 0x200B ||
        code == 0x200C || code == 0x200D || code == 0x2060) {
      needsNormalization = true;
      break;
    }
  }

  if (needsNormalization) {
    if (zeroWidthPattern.isValid()) {
      s.remove(zeroWidthPattern);
    }
    s.remove(controlCharsPattern);
  }

  s = s.trimmed();
  return s;
}

/// Fast timestamp parser for EVE log format "yyyy.MM.dd HH:mm:ss"
static qint64 parseEVETimestamp(const QString &timestamp) {
  // Expected format: "yyyy.MM.dd HH:mm:ss" (19 chars)
  if (timestamp.length() != 19) {
    return QDateTime::currentMSecsSinceEpoch();
  }

  // Manual parsing is significantly faster for fixed format
  bool ok;
  int year = timestamp.mid(0, 4).toInt(&ok);
  if (!ok || year < 2000 || year > 2100) {
    return QDateTime::currentMSecsSinceEpoch();
  }

  int month = timestamp.mid(5, 2).toInt(&ok);
  if (!ok || month < 1 || month > 12) {
    return QDateTime::currentMSecsSinceEpoch();
  }

  int day = timestamp.mid(8, 2).toInt(&ok);
  if (!ok || day < 1 || day > 31) {
    return QDateTime::currentMSecsSinceEpoch();
  }

  int hour = timestamp.mid(11, 2).toInt(&ok);
  if (!ok || hour < 0 || hour > 23) {
    return QDateTime::currentMSecsSinceEpoch();
  }

  int minute = timestamp.mid(14, 2).toInt(&ok);
  if (!ok || minute < 0 || minute > 59) {
    return QDateTime::currentMSecsSinceEpoch();
  }

  int second = timestamp.mid(17, 2).toInt(&ok);
  if (!ok || second < 0 || second > 59) {
    return QDateTime::currentMSecsSinceEpoch();
  }

  QDateTime dt(QDate(year, month, day), QTime(hour, minute, second));
  return dt.isValid() ? dt.toMSecsSinceEpoch()
                      : QDateTime::currentMSecsSinceEpoch();
}

ChatLogWorker::~ChatLogWorker() {
  stopMonitoring();

  for (QTimer *timer : m_miningTimers) {
    if (timer) {
      timer->stop();
      timer->deleteLater();
    }
  }
  m_miningTimers.clear();
  m_miningActiveState.clear();
}

void ChatLogWorker::setCharacterNames(const QStringList &characters) {
  QMutexLocker locker(&m_mutex);

  QSet<QString> newCharacterSet =
      QSet<QString>(characters.begin(), characters.end());
  QSet<QString> oldCharacterSet =
      QSet<QString>(m_characterNames.begin(), m_characterNames.end());
  QSet<QString> removedCharacters = oldCharacterSet - newCharacterSet;

  for (const QString &characterName : removedCharacters) {
    QTimer *timer = m_miningTimers.value(characterName, nullptr);
    if (timer) {
      timer->stop();
      timer->deleteLater();
      m_miningTimers.remove(characterName);
    }
    m_miningActiveState.remove(characterName);
  }

  m_characterNames = characters;
}

void ChatLogWorker::setLogDirectory(const QString &directory) {
  QMutexLocker locker(&m_mutex);
  m_logDirectory = directory;
}

void ChatLogWorker::setGameLogDirectory(const QString &directory) {
  QMutexLocker locker(&m_mutex);
  m_gameLogDirectory = directory;
}

void ChatLogWorker::setEnableChatLogMonitoring(bool enabled) {
  QMutexLocker locker(&m_mutex);
  m_enableChatLogMonitoring = enabled;
}

void ChatLogWorker::setEnableGameLogMonitoring(bool enabled) {
  QMutexLocker locker(&m_mutex);
  m_enableGameLogMonitoring = enabled;
}

void ChatLogWorker::refreshMonitoring() {
  QMutexLocker locker(&m_mutex);

  if (!m_running) {
    return;
  }

  qDebug()
      << "ChatLogWorker: Refreshing monitoring with updated settings (ChatLog:"
      << m_enableChatLogMonitoring << ", GameLog:" << m_enableGameLogMonitoring
      << ")";

  if (m_enableChatLogMonitoring) {
    QDir logDir(m_logDirectory);
    if (logDir.exists() &&
        !m_fileWatcher->directories().contains(m_logDirectory)) {
      m_fileWatcher->addPath(m_logDirectory);
      qDebug() << "ChatLogWorker: Now watching Chatlogs directory:"
               << m_logDirectory;
    }
  } else {
    if (m_fileWatcher->directories().contains(m_logDirectory)) {
      m_fileWatcher->removePath(m_logDirectory);
      qDebug() << "ChatLogWorker: Stopped watching Chatlogs directory:"
               << m_logDirectory;
    }
  }

  if (m_enableGameLogMonitoring) {
    QDir gameLogDir(m_gameLogDirectory);
    if (gameLogDir.exists() &&
        !m_fileWatcher->directories().contains(m_gameLogDirectory)) {
      m_fileWatcher->addPath(m_gameLogDirectory);
      qDebug() << "ChatLogWorker: Now watching Gamelogs directory:"
               << m_gameLogDirectory;
    }
  } else {
    if (m_fileWatcher->directories().contains(m_gameLogDirectory)) {
      m_fileWatcher->removePath(m_gameLogDirectory);
      qDebug() << "ChatLogWorker: Stopped watching Gamelogs directory:"
               << m_gameLogDirectory;
    }
  }

  scanExistingLogs();

  qDebug() << "ChatLogWorker: Monitoring refresh completed";
}

void ChatLogWorker::startMonitoring() {
  QMutexLocker locker(&m_mutex);

  if (m_running) {
    return;
  }

  m_running = true;

  qDebug() << "ChatLogWorker: Starting monitoring (ChatLog:"
           << m_enableChatLogMonitoring
           << ", GameLog:" << m_enableGameLogMonitoring << ")";

  if (m_enableChatLogMonitoring) {
    QDir logDir(m_logDirectory);
    if (logDir.exists()) {
      if (!m_fileWatcher->directories().contains(m_logDirectory)) {
        m_fileWatcher->addPath(m_logDirectory);
        qDebug() << "ChatLogWorker: Watching Chatlogs directory:"
                 << m_logDirectory;
      }
    } else {
      qWarning() << "ChatLogWorker: Chatlogs directory does not exist:"
                 << m_logDirectory;
    }
  }

  if (m_enableGameLogMonitoring) {
    QDir gameLogDir(m_gameLogDirectory);
    if (gameLogDir.exists()) {
      if (!m_fileWatcher->directories().contains(m_gameLogDirectory)) {
        m_fileWatcher->addPath(m_gameLogDirectory);
        qDebug() << "ChatLogWorker: Watching Gamelogs directory:"
                 << m_gameLogDirectory;
      }
    } else {
      qWarning() << "ChatLogWorker: Gamelogs directory does not exist:"
                 << m_gameLogDirectory;
    }
  }

  scanExistingLogs();

  m_scanTimer->start();

  qDebug() << "ChatLogWorker: Monitoring started for" << m_characterNames.size()
           << "characters";
}

void ChatLogWorker::cleanupDebounceTimer(const QString &filePath) {
  QTimer *timer = m_debounceTimers.value(filePath, nullptr);
  if (timer) {
    timer->stop();
    timer->deleteLater();
    m_debounceTimers.remove(filePath);
  }
}

void ChatLogWorker::stopMonitoring() {
  QMutexLocker locker(&m_mutex);

  if (!m_running) {
    return;
  }

  m_running = false;
  m_scanTimer->stop();

  for (auto it = m_debounceTimers.begin(); it != m_debounceTimers.end(); ++it) {
    QTimer *t = it.value();
    if (t) {
      t->stop();
      t->deleteLater();
    }
  }
  m_debounceTimers.clear();

  if (!m_fileWatcher->files().isEmpty()) {
    m_fileWatcher->removePaths(m_fileWatcher->files());
  }
  if (!m_fileWatcher->directories().isEmpty()) {
    m_fileWatcher->removePaths(m_fileWatcher->directories());
  }

  m_characterToLogFile.clear();
  m_filePositions.clear();
  m_fileLastSize.clear();
  m_fileLastModified.clear();
  m_cachedChatListenerMap.clear();
  m_cachedGameListenerMap.clear();

  qDebug() << "ChatLogWorker: Monitoring stopped";
}

void ChatLogWorker::scanExistingLogs() {
  QElapsedTimer totalTimer;
  totalTimer.start();

  QHash<QString, QString> chatListenerMap = m_cachedChatListenerMap;
  QHash<QString, QString> gameListenerMap = m_cachedGameListenerMap;

  if (m_enableChatLogMonitoring) {
    QDir d(m_logDirectory);
    if (!d.exists()) {
      chatListenerMap.clear();
      m_cachedChatListenerMap.clear();
    } else {
      QFileInfo di(d.absolutePath());
      QDateTime dirLastMod = di.lastModified();
      if (m_lastChatDirScanTime.isNull() ||
          dirLastMod > m_lastChatDirScanTime) {
        QElapsedTimer chatMapTimer;
        chatMapTimer.start();
        QStringList filters;
        filters << "Local_*.txt";
        chatListenerMap = buildListenerToFileMap(d, filters, 24);
        m_cachedChatListenerMap = chatListenerMap;
        m_lastChatDirScanTime = dirLastMod;
        qDebug() << "ChatLogWorker: chatListenerMap build took"
                 << chatMapTimer.elapsed()
                 << "ms (files:" << chatListenerMap.count() << ")";
      } else {
        qDebug() << "ChatLogWorker: chat directory unchanged since last scan "
                    "(using cached map with"
                 << chatListenerMap.count() << "entries)";
      }
    }
  }
  if (m_enableGameLogMonitoring) {
    QDir gd(m_gameLogDirectory);
    if (!gd.exists()) {
      gameListenerMap.clear();
      m_cachedGameListenerMap.clear();
    } else {
      QFileInfo gdi(gd.absolutePath());
      QDateTime dirLastMod = gdi.lastModified();
      if (m_lastGameDirScanTime.isNull() ||
          dirLastMod > m_lastGameDirScanTime) {
        QElapsedTimer gameMapTimer;
        gameMapTimer.start();
        QStringList filters;
        filters << "*.txt";
        gameListenerMap = buildListenerToFileMap(gd, filters, 24);
        m_cachedGameListenerMap = gameListenerMap;
        m_lastGameDirScanTime = dirLastMod;
        qDebug() << "ChatLogWorker: gameListenerMap build took"
                 << gameMapTimer.elapsed()
                 << "ms (files:" << gameListenerMap.count() << ")";
      } else {
        qDebug() << "ChatLogWorker: gamelog directory unchanged since last "
                    "scan (using cached map with"
                 << gameListenerMap.count() << "entries)";
      }
    }
  }

  for (const QString &characterName : m_characterNames) {
    if (m_enableChatLogMonitoring) {
      QString chatLogFile;
      QString key = characterName.toLower();
      if (chatListenerMap.contains(key)) {
        chatLogFile = chatListenerMap.value(key);
      } else {
        chatLogFile = findChatLogFileForCharacter(characterName);
      }

      if (!chatLogFile.isEmpty()) {
        QString key = characterName + "_chatlog";
        QString currentFile = m_characterToLogFile.value(key);
        if (currentFile != chatLogFile) {
          if (!currentFile.isEmpty() &&
              m_fileWatcher->files().contains(currentFile)) {
            m_fileWatcher->removePath(currentFile);
            m_fileToKeyMap.remove(currentFile);
            cleanupDebounceTimer(currentFile);
          }

          bool shouldMonitorChatlog = !m_enableGameLogMonitoring;

          if (shouldMonitorChatlog) {
            m_characterToLogFile[key] = chatLogFile;
            m_fileToKeyMap[chatLogFile] = key;
            m_fileWatcher->addPath(chatLogFile);
            qDebug() << "ChatLogWorker: Monitoring CHATLOG for" << characterName
                     << ":" << chatLogFile;
          }

          // Optimized initial system detection - single file open
          QFileInfo fi(chatLogFile);
          QString lastSystemLine;

          // Try tail scan first (last 64KB)
          const qint64 tailSize = 65536;
          const qint64 fallbackSize = 5 * 1024 * 1024;

          QFile file(chatLogFile);
          if (file.open(QIODevice::ReadOnly)) {
            qint64 fileSize = file.size();
            qint64 startPos = 0;

            if (fileSize > tailSize + 1024) {
              startPos = fileSize - tailSize - 1024;
              file.seek(startPos);
            }

            QByteArray tailData = file.readAll();
            QString tailContent = QString::fromUtf8(tailData);
            QStringList tailLines = tailContent.split('\n', Qt::SkipEmptyParts);

            // Search backwards through tail lines for system change
            static QRegularExpression systemChangePattern(
                R"(\[\s*([\d.\s:]+)\]\s*EVE System\s*>\s*Channel changed to Local\s*:\s*(.+))",
                QRegularExpression::CaseInsensitiveOption |
                    QRegularExpression::UseUnicodePropertiesOption);

            for (int i = tailLines.size() - 1; i >= 0; --i) {
              QString normLine = normalizeLogLine(tailLines[i]);
              if (systemChangePattern.match(normLine).hasMatch()) {
                lastSystemLine = normLine;
                break;
              }
            }

            // Fallback: if tail didn't find anything and file is small, scan
            // all
            if (lastSystemLine.isEmpty() && fileSize <= fallbackSize) {
              qDebug() << "ChatLogWorker: tail scan found nothing, scanning "
                          "entire file for"
                       << chatLogFile;
              file.seek(0);
              QByteArray allData = file.readAll();
              QString allContent = QString::fromUtf8(allData);
              QStringList allLines = allContent.split('\n', Qt::SkipEmptyParts);

              for (const QString &line : allLines) {
                QString norm = normalizeLogLine(line);
                QString s = extractSystemFromLine(norm);
                if (!s.isEmpty()) {
                  lastSystemLine =
                      norm; // Keep overwriting to get the LAST match
                }
              }
            }

            if (!lastSystemLine.isEmpty()) {
              QRegularExpressionMatch match =
                  systemChangePattern.match(lastSystemLine);
              if (match.hasMatch()) {
                QString timestampStr = match.captured(1).trimmed();
                QString rawSystem = match.captured(2).trimmed();
                QString newSystem = sanitizeSystemName(rawSystem);

                qint64 updateTime = parseEVETimestamp(timestampStr);

                CharacterLocation &location =
                    m_characterLocations[characterName];

                // Only update if we don't already have newer data
                // This prevents chatlog rescans from overwriting current
                // position
                if (updateTime > location.lastUpdate ||
                    location.systemName.isEmpty()) {
                  location.characterName = characterName;
                  location.systemName = newSystem;
                  location.lastUpdate = updateTime;

                  qDebug() << "ChatLogWorker: Initial system for"
                           << characterName << ":" << newSystem << "(from"
                           << timestampStr << ", gamelog monitoring:"
                           << m_enableGameLogMonitoring << ")";
                  emit systemChanged(characterName, newSystem);
                } else {
                  qDebug() << "ChatLogWorker: Chatlog data for" << characterName
                           << "is older than current position, skipping";
                }
              }

              parseLogLine(lastSystemLine, characterName);
            }

            if (shouldMonitorChatlog) {
              m_filePositions[chatLogFile] = file.size();
              m_fileLastSize[chatLogFile] = fi.size();
              m_fileLastModified[chatLogFile] =
                  fi.lastModified().toMSecsSinceEpoch();
            } else {
              qDebug()
                  << "ChatLogWorker: Gamelog monitoring enabled - chatlog used "
                     "only for initial system detection, not monitored for"
                  << characterName;
            }

            file.close();
          }
        }
      }
    }

    if (m_enableGameLogMonitoring) {
      QString gameLogFile;
      QString key = characterName.toLower();
      if (gameListenerMap.contains(key)) {
        gameLogFile = gameListenerMap.value(key);
      } else {
        gameLogFile = findGameLogFileForCharacter(characterName);
      }

      if (!gameLogFile.isEmpty()) {
        QString key = characterName + "_gamelog";
        QString currentFile = m_characterToLogFile.value(key);
        if (currentFile != gameLogFile) {
          if (!currentFile.isEmpty() &&
              m_fileWatcher->files().contains(currentFile)) {
            m_fileWatcher->removePath(currentFile);
            m_fileToKeyMap.remove(currentFile);
            cleanupDebounceTimer(currentFile);
          }

          m_characterToLogFile[key] = gameLogFile;
          m_fileToKeyMap[gameLogFile] = key;
          m_fileWatcher->addPath(gameLogFile);

          qDebug() << "ChatLogWorker: Monitoring GAMELOG for" << characterName
                   << ":" << gameLogFile;

          // Scan gamelog for most recent system jump
          // Only update if this jump is NEWER than the chatlog system
          QFileInfo fi(gameLogFile);
          QString lastJumpLine;

          QFile file(gameLogFile);
          if (file.open(QIODevice::ReadOnly)) {
            qint64 fileSize = file.size();

            // Read last 64KB to find most recent jump
            const qint64 tailSize = 65536;
            qint64 startPos = 0;

            if (fileSize > tailSize + 1024) {
              startPos = fileSize - tailSize - 1024;
              file.seek(startPos);
            }

            QByteArray tailData = file.readAll();
            QString tailContent = QString::fromUtf8(tailData);
            QStringList tailLines = tailContent.split('\n', Qt::SkipEmptyParts);

            // Search backwards for most recent system jump
            static QRegularExpression jumpPattern(
                R"(\[\s*([\d.\s:]+)\]\s*\(None\)\s*Jumping from\s+(.+?)\s+to\s+(.+))");

            for (int i = tailLines.size() - 1; i >= 0; --i) {
              QString normLine = normalizeLogLine(tailLines[i]);
              if (jumpPattern.match(normLine).hasMatch()) {
                lastJumpLine = normLine;
                break;
              }
            }

            // Process the jump only if it's newer than chatlog data
            if (!lastJumpLine.isEmpty()) {
              QRegularExpressionMatch jumpMatch =
                  jumpPattern.match(lastJumpLine);
              if (jumpMatch.hasMatch()) {
                QString timestampStr = jumpMatch.captured(1).trimmed();
                QString fromSystem = jumpMatch.captured(2).trimmed();
                QString toSystem = jumpMatch.captured(3).trimmed();
                QString newSystem = sanitizeSystemName(toSystem);

                qint64 updateTime = parseEVETimestamp(timestampStr);

                // Check if this jump is newer than existing location data
                CharacterLocation &location =
                    m_characterLocations[characterName];
                if (updateTime > location.lastUpdate ||
                    location.systemName != newSystem) {
                  location.characterName = characterName;
                  location.systemName = newSystem;
                  location.lastUpdate = updateTime;

                  qDebug() << "ChatLogWorker: Updated system from GAMELOG for"
                           << characterName << ":" << newSystem << "(from"
                           << timestampStr << ") - overriding chatlog data";
                  emit systemChanged(characterName, newSystem);
                } else {
                  qDebug()
                      << "ChatLogWorker: GAMELOG jump for" << characterName
                      << "is older than chatlog data, keeping chatlog system";
                }
              }
            }

            m_filePositions[gameLogFile] = file.size();
            m_fileLastSize[gameLogFile] = fi.size();
            m_fileLastModified[gameLogFile] =
                fi.lastModified().toMSecsSinceEpoch();

            file.close();
          }
        }
      }
    }
  }

  qDebug() << "ChatLogWorker: File watcher now watching"
           << m_fileWatcher->files().count() << "files";

  QSet<QString> newFiles;
  for (auto it = m_characterToLogFile.constBegin();
       it != m_characterToLogFile.constEnd(); ++it) {
    newFiles.insert(it.value());
  }
  QStringList watchedFiles = m_fileWatcher->files();
  QStringList watchedDirs = m_fileWatcher->directories();
  for (const QString &w : watchedFiles) {
    if (!newFiles.contains(w)) {
      if (!watchedDirs.contains(w)) {
        qDebug() << "ChatLogWorker: Removing stale file watcher:" << w;
        m_fileWatcher->removePath(w);
        m_fileToKeyMap.remove(w);
        m_filePositions.remove(w);
        m_fileLastSize.remove(w);
        m_fileLastModified.remove(w);
        cleanupDebounceTimer(w);
      }
    }
  }
  qDebug() << "ChatLogWorker: scanExistingLogs total took"
           << totalTimer.elapsed() << "ms";
}

void ChatLogWorker::checkForNewFiles() {
  QMutexLocker locker(&m_mutex);

  if (!m_running) {
    return;
  }

  // Check if actual file lists have changed (new files appeared)
  // This prevents unnecessary scans when existing files are just modified
  bool newFilesFound = false;

  if (m_enableChatLogMonitoring) {
    QDir d(m_logDirectory);
    if (d.exists()) {
      QStringList chatFiles =
          d.entryList(QStringList() << "Local_*.txt", QDir::Files);
      QSet<QString> currentChatFiles;
      for (const QString &f : chatFiles) {
        currentChatFiles.insert(d.absoluteFilePath(f));
      }

      // Check if any new files appeared
      if (m_knownChatLogFiles.isEmpty()) {
        // First scan
        m_knownChatLogFiles = currentChatFiles;
        newFilesFound = true;
      } else if (currentChatFiles != m_knownChatLogFiles) {
        // Files added or removed
        qDebug() << "ChatLogWorker: Detected chat log file changes";
        m_knownChatLogFiles = currentChatFiles;
        newFilesFound = true;
      }
    }
  }

  if (m_enableGameLogMonitoring) {
    QDir gd(m_gameLogDirectory);
    if (gd.exists()) {
      QStringList gameFiles =
          gd.entryList(QStringList() << "*.txt", QDir::Files);
      QSet<QString> currentGameFiles;
      for (const QString &f : gameFiles) {
        currentGameFiles.insert(gd.absoluteFilePath(f));
      }

      // Check if any new files appeared
      if (m_knownGameLogFiles.isEmpty()) {
        // First scan
        m_knownGameLogFiles = currentGameFiles;
        newFilesFound = true;
      } else if (currentGameFiles != m_knownGameLogFiles) {
        // Files added or removed
        qDebug() << "ChatLogWorker: Detected game log file changes";
        m_knownGameLogFiles = currentGameFiles;
        newFilesFound = true;
      }
    }
  }

  if (newFilesFound) {
    qDebug() << "ChatLogWorker: New files detected, scanning...";
    scanExistingLogs();
  } else {
    // Directory unchanged - this was just individual file modifications
    // which are already handled by fileChanged signal
  }
}

QString
ChatLogWorker::findChatLogFileForCharacter(const QString &characterName) {

  QDir chatLogDir(m_logDirectory);
  if (!chatLogDir.exists()) {
    return QString();
  }

  QStringList filters;
  filters << "Local_*.txt";

  QFileInfoList chatFiles =
      chatLogDir.entryInfoList(filters, QDir::Files, QDir::Time);

  for (const QFileInfo &fileInfo : chatFiles) {
    QDateTime lastModified = fileInfo.lastModified();
    qint64 hoursSinceModified =
        lastModified.secsTo(QDateTime::currentDateTime()) / 3600;

    if (hoursSinceModified > 24) {
      continue;
    }

    QString foundCharacter =
        extractCharacterFromLogFile(fileInfo.absoluteFilePath());
    if (foundCharacter.compare(characterName, Qt::CaseInsensitive) == 0) {
      return fileInfo.absoluteFilePath();
    }
  }

  return QString();
}

QString
ChatLogWorker::findGameLogFileForCharacter(const QString &characterName) {

  QDir gameLogDir(m_gameLogDirectory);
  if (!gameLogDir.exists()) {
    return QString();
  }

  QStringList filters;
  filters << "*.txt";

  QFileInfoList gameFiles =
      gameLogDir.entryInfoList(filters, QDir::Files, QDir::Time);

  for (const QFileInfo &fileInfo : gameFiles) {
    QDateTime lastModified = fileInfo.lastModified();
    qint64 hoursSinceModified =
        lastModified.secsTo(QDateTime::currentDateTime()) / 3600;

    if (hoursSinceModified > 24) {
      continue;
    }

    QString foundCharacter =
        extractCharacterFromLogFile(fileInfo.absoluteFilePath());
    if (foundCharacter.compare(characterName, Qt::CaseInsensitive) == 0) {
      return fileInfo.absoluteFilePath();
    }
  }

  return QString();
}

QString ChatLogWorker::findLogFileForCharacter(const QString &characterName) {

  QDir chatLogDir(m_logDirectory);
  if (chatLogDir.exists()) {
    QStringList filters;
    filters << "Local_*.txt";

    QFileInfoList chatFiles =
        chatLogDir.entryInfoList(filters, QDir::Files, QDir::Time);

    for (const QFileInfo &fileInfo : chatFiles) {
      QDateTime lastModified = fileInfo.lastModified();
      qint64 hoursSinceModified =
          lastModified.secsTo(QDateTime::currentDateTime()) / 3600;

      if (hoursSinceModified > 24) {
        continue;
      }

      QString foundCharacter =
          extractCharacterFromLogFile(fileInfo.absoluteFilePath());
      if (foundCharacter.compare(characterName, Qt::CaseInsensitive) == 0) {
        return fileInfo.absoluteFilePath();
      }
    }
  }

  QDir gameLogDir(m_gameLogDirectory);
  if (gameLogDir.exists()) {
    QStringList filters;
    filters << "*.txt";

    QFileInfoList gameFiles =
        gameLogDir.entryInfoList(filters, QDir::Files, QDir::Time);

    for (const QFileInfo &fileInfo : gameFiles) {
      QDateTime lastModified = fileInfo.lastModified();
      qint64 hoursSinceModified =
          lastModified.secsTo(QDateTime::currentDateTime()) / 3600;

      if (hoursSinceModified > 24) {
        continue;
      }

      QString foundCharacter =
          extractCharacterFromLogFile(fileInfo.absoluteFilePath());
      if (foundCharacter.compare(characterName, Qt::CaseInsensitive) == 0) {
        return fileInfo.absoluteFilePath();
      }
    }
  }

  return QString();
}

QString ChatLogWorker::extractCharacterFromLogFile(const QString &filePath) {
  QFileInfo fileInfo(filePath);
  qint64 modTime = fileInfo.lastModified().toMSecsSinceEpoch();

  // Check cache first
  auto it = m_fileToCharacterCache.find(filePath);
  if (it != m_fileToCharacterCache.end() && it->second == modTime) {
    return it->first; // Return cached character name
  }

  // Cache miss or file modified - extract character name
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString();
  }

  QTextStream in(&file);
  in.setAutoDetectUnicode(true);

  static QRegularExpression listenerPattern(R"(Listener:\s+(.+))");

  QString fileName = fileInfo.fileName();
  bool isChatLog = fileName.startsWith("Local_", Qt::CaseInsensitive);

  QString characterName;

  if (isChatLog) {
    for (int i = 1; i <= 8 && !in.atEnd(); ++i) {
      in.readLine();
    }

    if (!in.atEnd()) {
      QString line = in.readLine();
      QRegularExpressionMatch match = listenerPattern.match(line);
      if (match.hasMatch()) {
        characterName = match.captured(1).trimmed();
      }
    }
  } else {
    for (int i = 1; i <= 2 && !in.atEnd(); ++i) {
      in.readLine();
    }

    if (!in.atEnd()) {
      QString line = in.readLine();
      QRegularExpressionMatch match = listenerPattern.match(line);
      if (match.hasMatch()) {
        characterName = match.captured(1).trimmed();
      }
    }
  }

  file.close();

  // Cache the result
  if (!characterName.isEmpty()) {
    m_fileToCharacterCache[filePath] = qMakePair(characterName, modTime);
  }

  return characterName;
}

void ChatLogWorker::processLogFile(const QString &filePath) {
  qint64 processStartTime = QDateTime::currentMSecsSinceEpoch();
  qDebug() << "ChatLogWorker: processLogFile STARTED for:" << filePath;

  QString characterName;
  qint64 lastPos;

  // Minimize mutex lock time - only for data access
  {
    QMutexLocker locker(&m_mutex);

    if (!m_running) {
      qDebug() << "ChatLogWorker: Not running, ignoring file change";
      return;
    }

    QString key = m_fileToKeyMap.value(filePath);
    if (!key.isEmpty()) {
      if (key.endsWith("_chatlog")) {
        characterName = key.left(key.length() - 8);
      } else if (key.endsWith("_gamelog")) {
        characterName = key.left(key.length() - 8);
      } else {
        characterName = key;
      }
    } else {
      for (auto it = m_characterToLogFile.constBegin();
           it != m_characterToLogFile.constEnd(); ++it) {
        if (it.value() == filePath) {
          QString k = it.key();
          if (k.endsWith("_chatlog")) {
            characterName = k.left(k.length() - 8);
          } else if (k.endsWith("_gamelog")) {
            characterName = k.left(k.length() - 8);
          } else {
            characterName = k;
          }
          break;
        }
      }
    }

    if (characterName.isEmpty()) {
      qDebug() << "ChatLogWorker: Could not find character for log file:"
               << filePath;
      return;
    }

    lastPos = m_filePositions.value(filePath, 0);
  }
  // Mutex released here - file I/O happens without lock

  qDebug() << "ChatLogWorker: Processing log for character:" << characterName;
  qDebug() << "ChatLogWorker: File path:" << filePath;

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) { // Removed | QIODevice::Text
    qWarning() << "ChatLogWorker: Failed to open log file:" << filePath;

    QMutexLocker locker(&m_mutex);
    m_fileWatcher->removePath(filePath);
    m_characterToLogFile.remove(characterName);
    m_filePositions.remove(filePath);
    cleanupDebounceTimer(filePath);
    return;
  }

  qint64 fileSize = file.size();
  qDebug() << "ChatLogWorker: File size:" << fileSize
           << ", Last position:" << lastPos;

  // Handle file truncation or rotation
  if (lastPos > fileSize) {
    lastPos = 0;
  }

  if (lastPos > 0 && fileSize >= lastPos) {
    file.seek(lastPos);
  } else {
    lastPos = 0;
  }

  // Read remaining data into buffer - much faster than line-by-line
  QByteArray newData = file.readAll();
  qint64 newPos = file.pos();
  file.close();

  if (newData.isEmpty()) {
    qDebug() << "ChatLogWorker: No new data to process";
    return;
  }

  // Convert to QString once with explicit UTF-8 encoding
  QString content = QString::fromUtf8(newData);

  // Split into lines and process
  QStringList lines = content.split('\n', Qt::SkipEmptyParts);

  qDebug() << "ChatLogWorker: Read" << lines.size() << "new lines from log";

  // Process all lines (without mutex locked)
  // Note: Qt::SkipEmptyParts already filters out empty lines
  for (const QString &line : lines) {
    parseLogLine(line, characterName);
  }

  // Update position with mutex
  {
    QMutexLocker locker(&m_mutex);
    m_filePositions[filePath] = newPos;
  }

  qint64 processElapsed =
      QDateTime::currentMSecsSinceEpoch() - processStartTime;
  qDebug() << "ChatLogWorker: processLogFile COMPLETED in" << processElapsed
           << "ms";
}

void ChatLogWorker::markFileDirty(const QString &filePath) {
  qint64 startTime = QDateTime::currentMSecsSinceEpoch();
  QDateTime nowTime = QDateTime::currentDateTime();
  qDebug() << "ChatLogWorker: markFileDirty CALLED at"
           << nowTime.toString("HH:mm:ss.zzz") << "for" << filePath;

  QMutexLocker locker(&m_mutex);

  QFileInfo fi(filePath);
  if (!fi.exists()) {
    qDebug() << "ChatLogWorker: markFileDirty called for non-existent file:"
             << filePath;
    return;
  }

  qint64 currentSize = fi.size();
  qint64 currentModified = fi.lastModified().toMSecsSinceEpoch();
  qint64 lastSize = m_fileLastSize.value(filePath, -1);
  qint64 lastModified = m_fileLastModified.value(filePath, -1);

  if (lastSize == currentSize && lastModified == currentModified) {
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startTime;
    qDebug() << "ChatLogWorker: markFileDirty - no change detected" << filePath
             << "(" << elapsed << "ms)";
    return;
  }

  qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startTime;
  qDebug() << "ChatLogWorker: markFileDirty for" << filePath
           << "(size:" << lastSize << "->" << currentSize << ")"
           << "(" << elapsed << "ms so far)";

  m_fileLastSize[filePath] = currentSize;
  m_fileLastModified[filePath] = currentModified;

  // Check rate limiting to batch rapid changes
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  qint64 lastProcessed = m_fileLastProcessed.value(filePath, 0);
  qint64 timeSinceLastProcess = now - lastProcessed;

  // If processed very recently (< 20ms), use short debounce to batch
  if (timeSinceLastProcess < 20) {
    qDebug() << "ChatLogWorker: Using debounce for rapid changes" << filePath
             << "(last processed" << timeSinceLastProcess << "ms ago)";

    QTimer *debounceTimer = m_debounceTimers.value(filePath, nullptr);

    if (!debounceTimer) {
      debounceTimer = new QTimer(this);
      debounceTimer->setSingleShot(true);
      debounceTimer->setInterval(10); // Very short 10ms debounce

      connect(debounceTimer, &QTimer::timeout, this, [this, filePath]() {
        QMutexLocker innerLocker(&m_mutex);

        m_fileLastProcessed[filePath] = QDateTime::currentMSecsSinceEpoch();

        QTimer *timerToDelete = m_debounceTimers.take(filePath);
        innerLocker.unlock();

        this->processLogFile(filePath);

        if (timerToDelete) {
          timerToDelete->deleteLater();
        }
      });

      m_debounceTimers[filePath] = debounceTimer;
    }

    // Restart the timer to batch multiple rapid changes
    debounceTimer->start();

    elapsed = QDateTime::currentMSecsSinceEpoch() - startTime;
    qDebug() << "ChatLogWorker: markFileDirty debounce timer started"
             << "(" << elapsed << "ms total)";
    return;
  }

  // Process immediately if enough time has passed
  m_fileLastProcessed[filePath] = now;
  locker.unlock();

  processLogFile(filePath);

  elapsed = QDateTime::currentMSecsSinceEpoch() - startTime;
  qDebug() << "ChatLogWorker: markFileDirty completed with immediate processing"
           << "(" << elapsed << "ms total)";
}

void ChatLogWorker::parseLogLine(const QString &line,
                                 const QString &characterName) {
  QString normalizedLine = normalizeLogLine(line);

  if (normalizedLine.isEmpty() || normalizedLine.length() < 25 ||
      normalizedLine.length() > 1000) {
    return;
  }

  // Use indexOf for faster string searching (search from position 20 to skip
  // timestamp)
  const int searchStart = 20;
  int notifyPos =
      normalizedLine.indexOf("(notify)", searchStart, Qt::CaseInsensitive);
  int questionPos =
      normalizedLine.indexOf("(question)", searchStart, Qt::CaseInsensitive);
  int miningPos =
      normalizedLine.indexOf("(mining)", searchStart, Qt::CaseInsensitive);
  int nonePos =
      normalizedLine.indexOf("(None)", searchStart, Qt::CaseInsensitive);
  int eveSystemPos =
      normalizedLine.indexOf("EVE System", searchStart, Qt::CaseInsensitive);

  // Handle EVE System messages (chatlog only)
  if (!m_enableGameLogMonitoring && eveSystemPos != -1) {
    static QRegularExpression systemChangePattern(
        R"(\[\s*([\d.\s:]+)\]\s*EVE System\s*>\s*Channel changed to Local\s*:\s*(.+))",
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::UseUnicodePropertiesOption);

    QRegularExpressionMatch match = systemChangePattern.match(normalizedLine);
    if (match.hasMatch()) {
      QString timestampStr = match.captured(1).trimmed();
      QString rawSystem = match.captured(2).trimmed();

      QString newSystem = sanitizeSystemName(rawSystem);

      qint64 updateTime = parseEVETimestamp(timestampStr);

      CharacterLocation &location = m_characterLocations[characterName];
      if (location.systemName != newSystem) {
        location.characterName = characterName;
        location.systemName = newSystem;
        location.lastUpdate = updateTime;

        qDebug() << "ChatLogWorker: System change detected (chatlog):"
                 << characterName << "->" << newSystem << "(from"
                 << timestampStr << ")";

        qint64 emitTime = QDateTime::currentMSecsSinceEpoch();
        emit systemChanged(characterName, newSystem);
        qint64 emitElapsed = QDateTime::currentMSecsSinceEpoch() - emitTime;
        qDebug() << "ChatLogWorker: systemChanged signal emitted in"
                 << emitElapsed << "ms";
      }
    }
    return;
  }

  // Handle fleet invites
  if (questionPos != -1) {
    static QRegularExpression fleetInvitePattern(
        R"(\[\s*[\d.\s:]+\]\s*\(question\)\s*<a href="[^"]+">([^<]+)</a>\s*wants you to join their fleet)");

    QRegularExpressionMatch fleetMatch =
        fleetInvitePattern.match(normalizedLine);
    if (fleetMatch.hasMatch()) {
      QString inviter = fleetMatch.captured(1).trimmed();
      QString eventText = QString("Fleet invite from %1").arg(inviter);
      qDebug() << "ChatLogWorker: Fleet invite detected for" << characterName
               << "from" << inviter;
      emit combatEventDetected(characterName, "fleet_invite", eventText);
    }
    return;
  }

  // Handle notification messages
  if (notifyPos != -1) {
    // Sub-search within notify messages using indexOf for efficiency
    int followingPos =
        normalizedLine.indexOf("Following", notifyPos, Qt::CaseInsensitive);
    int regroupingPos =
        normalizedLine.indexOf("Regrouping", notifyPos, Qt::CaseInsensitive);
    int compressedPos =
        normalizedLine.indexOf("compressed", notifyPos, Qt::CaseInsensitive);
    int cloakPos = normalizedLine.indexOf("cloak deactivates", notifyPos,
                                          Qt::CaseInsensitive);

    if (followingPos != -1) {
      static QRegularExpression followWarpPattern(
          R"(\[\s*[\d.\s:]+\]\s*\(notify\)\s*Following\s+(.+?)\s+in warp)");

      QRegularExpressionMatch followMatch =
          followWarpPattern.match(normalizedLine);
      if (followMatch.hasMatch()) {
        QString leader = followMatch.captured(1).trimmed();

        QString displayName = m_cachedCustomNames.value(leader, leader);

        QString eventText = QString("Following %1").arg(displayName);
        qDebug() << "ChatLogWorker: Follow warp detected for" << characterName
                 << "->" << leader
                 << (displayName != leader
                         ? QString(" (displayed as: %1)").arg(displayName)
                         : "");
        emit combatEventDetected(characterName, "follow_warp", eventText);
        return;
      }
    }

    if (regroupingPos != -1) {
      static QRegularExpression regroupPattern(
          R"(\[\s*[\d.\s:]+\]\s*\(notify\)\s*Regrouping to\s+(.+?)(?:\.|$))");

      QRegularExpressionMatch regroupMatch =
          regroupPattern.match(normalizedLine);
      if (regroupMatch.hasMatch()) {
        QString leader = regroupMatch.captured(1).trimmed();

        QString displayName = m_cachedCustomNames.value(leader, leader);

        QString eventText = QString("Regrouping to %1").arg(displayName);
        qDebug() << "ChatLogWorker: Regroup detected for" << characterName
                 << "->" << leader
                 << (displayName != leader
                         ? QString(" (displayed as: %1)").arg(displayName)
                         : "");
        emit combatEventDetected(characterName, "regroup", eventText);
        return;
      }
    }

    if (compressedPos != -1) {
      static QRegularExpression compressionPattern(
          R"(\[\s*[\d.\s:]+\]\s*\(notify\)\s*Successfully compressed\s+(.+?)\s+into\s+(\d+)\s+(.+))");

      QRegularExpressionMatch compressMatch =
          compressionPattern.match(normalizedLine);
      if (compressMatch.hasMatch()) {
        QString count = compressMatch.captured(2).trimmed();
        QString compressedItem = compressMatch.captured(3).trimmed();
        if (compressedItem.endsWith('.')) {
          compressedItem.chop(1);
        }
        QString eventText =
            QString("Compressed: %1x %2").arg(count, compressedItem);
        qDebug() << "ChatLogWorker: Compression detected for" << characterName
                 << ":" << eventText;
        emit combatEventDetected(characterName, "compression", eventText);
        return;
      }
    }

    if (cloakPos != -1) {
      static QRegularExpression decloakPattern(
          R"(\[\s*[\d.\s:]+\]\s*\(notify\)\s*Your cloak deactivates due to proximity to (?:a nearby )?(.+?)\.)");

      QRegularExpressionMatch decloakMatch =
          decloakPattern.match(normalizedLine);
      if (decloakMatch.hasMatch()) {
        QString source = decloakMatch.captured(1).trimmed();
        QString eventText = QString("Decloaked by %1").arg(source);
        qDebug() << "ChatLogWorker: Decloak detected for" << characterName
                 << "- Source:" << source;
        emit combatEventDetected(characterName, "decloak", eventText);
        return;
      }
    }
    return;
  }

  // Handle mining events
  if (miningPos != -1) {
    static QRegularExpression miningPattern(R"(\[\s*[\d.\s:]+\]\s*\(mining\))");

    QRegularExpressionMatch miningMatch = miningPattern.match(normalizedLine);
    if (miningMatch.hasMatch()) {
      qDebug() << "ChatLogWorker: Mining event detected";
      handleMiningEvent(characterName, "ore");
    }
    return;
  }

  // Handle system jumps (gamelog)
  if (nonePos != -1) {
    int jumpingPos =
        normalizedLine.indexOf("Jumping", nonePos, Qt::CaseInsensitive);
    if (jumpingPos != -1) {
      static QRegularExpression jumpPattern(
          R"(\[\s*([\d.\s:]+)\]\s*\(None\)\s*Jumping from\s+(.+?)\s+to\s+(.+))");

      QRegularExpressionMatch jumpMatch = jumpPattern.match(normalizedLine);
      if (jumpMatch.hasMatch()) {
        QString timestampStr = jumpMatch.captured(1).trimmed();
        QString fromSystem = jumpMatch.captured(2).trimmed();
        QString toSystem = jumpMatch.captured(3).trimmed();

        QString newSystem = sanitizeSystemName(toSystem);

        qint64 updateTime = parseEVETimestamp(timestampStr);

        CharacterLocation &location = m_characterLocations[characterName];
        if (location.systemName != newSystem) {
          location.characterName = characterName;
          location.systemName = newSystem;
          location.lastUpdate = updateTime;

          QDateTime detectTime = QDateTime::currentDateTime();
          qDebug() << "ChatLogWorker: System jump detected (gamelog) at"
                   << detectTime.toString("HH:mm:ss.zzz") << "-"
                   << characterName << "from" << fromSystem << "to" << newSystem
                   << "(jump timestamp:" << timestampStr << ")";

          qint64 emitTime = QDateTime::currentMSecsSinceEpoch();
          emit systemChanged(characterName, newSystem);
          qint64 emitElapsed = QDateTime::currentMSecsSinceEpoch() - emitTime;
          qDebug() << "ChatLogWorker: systemChanged signal emitted in"
                   << emitElapsed << "ms";
        }
      }
    }
  }
}

void ChatLogWorker::handleMiningEvent(const QString &characterName,
                                      const QString &ore) {
  int timeoutMs = Config::instance().miningTimeoutSeconds() * 1000;

  qDebug() << "ChatLogWorker: Mining event detected for" << characterName
           << "- ore:" << ore << "- timeout:" << timeoutMs << "ms";

  QTimer *timer = m_miningTimers.value(characterName, nullptr);
  if (!timer) {
    timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(timeoutMs);
    connect(timer, &QTimer::timeout, this,
            [this, characterName]() { onMiningTimeout(characterName); });
    m_miningTimers[characterName] = timer;
    qDebug() << "ChatLogWorker: Created new mining timer for" << characterName;
  } else {
    timer->setInterval(timeoutMs);
    qDebug() << "ChatLogWorker: Restarting existing mining timer for"
             << characterName;
  }

  if (!m_miningActiveState.value(characterName, false)) {
    m_miningActiveState[characterName] = true;
    emit combatEventDetected(characterName, "mining_started", "Mining started");
    qDebug() << "ChatLogWorker: Mining started for" << characterName;
  } else {
    qDebug() << "ChatLogWorker: Mining already active for" << characterName
             << ", resetting timer";
  }

  timer->start();
  qDebug() << "ChatLogWorker: Mining timer started/restarted for"
           << characterName << "- will timeout in" << timeoutMs << "ms";
}

void ChatLogWorker::onMiningTimeout(const QString &characterName) {
  if (m_miningActiveState.value(characterName, false)) {
    m_miningActiveState[characterName] = false;
    emit combatEventDetected(characterName, "mining_stopped", "Mining stopped");
    qDebug() << "ChatLogWorker: Mining stopped for" << characterName
             << "(timeout)";
  }
}

void ChatLogWorker::updateCustomNameCache() {
  m_cachedCustomNames = Config::instance().getAllCustomThumbnailNames();
  qDebug() << "ChatLogWorker: Updated custom name cache with"
           << m_cachedCustomNames.size() << "entries";
}

QString ChatLogWorker::extractSystemFromLine(const QString &logLine) {
  static QRegularExpression pattern(
      R"(Channel changed to Local\s*:\s*(.+))",
      QRegularExpression::CaseInsensitiveOption |
          QRegularExpression::UseUnicodePropertiesOption);
  QRegularExpressionMatch match = pattern.match(logLine);

  if (match.hasMatch()) {
    return sanitizeSystemName(match.captured(1).trimmed());
  }

  return QString();
}

QHash<QString, QString> ChatLogWorker::buildListenerToFileMap(
    const QDir &dir, const QStringList &filters, int maxAgeHours) {
  QHash<QString, QString> result;

  if (!dir.exists()) {
    return result;
  }

  QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Time);

  for (const QFileInfo &fi : files) {
    QDateTime lastModified = fi.lastModified();
    qint64 hoursSinceModified =
        lastModified.secsTo(QDateTime::currentDateTime()) / 3600;
    if (hoursSinceModified > maxAgeHours) {
      continue;
    }

    QString character = extractCharacterFromLogFile(fi.absoluteFilePath());
    if (!character.isEmpty()) {
      QString key = character.toLower();
      if (!result.contains(key)) {
        result.insert(key, fi.absoluteFilePath());
      }
    }
  }

  return result;
}

QString ChatLogWorker::sanitizeSystemName(const QString &system) {
  static const QRegularExpression htmlTagPattern("<[^>]*>");
  static const QRegularExpression whitespacePattern("\\s+");

  QString s = system;
  s = s.remove(htmlTagPattern);

  s = s.trimmed();
  s = s.replace(whitespacePattern, " ");

  if (!s.isEmpty() && (s.endsWith('.') || s.endsWith(','))) {
    s.chop(1);
    s = s.trimmed();
  }

  return s;
}

ChatLogReader::ChatLogReader(QObject *parent)
    : QObject(parent), m_workerThread(new QThread(this)),
      m_worker(new ChatLogWorker()), m_monitoring(false) {
  m_worker->moveToThread(m_workerThread);

  // Set higher priority for faster event processing
  m_workerThread->setPriority(QThread::HighPriority);

  connect(m_worker, &ChatLogWorker::systemChanged, this,
          &ChatLogReader::handleSystemChanged, Qt::QueuedConnection);
  connect(m_worker, &ChatLogWorker::combatEventDetected, this,
          &ChatLogReader::combatEventDetected, Qt::QueuedConnection);
  connect(m_worker, &ChatLogWorker::characterLoggedIn, this,
          &ChatLogReader::characterLoggedIn, Qt::QueuedConnection);
  connect(m_worker, &ChatLogWorker::characterLoggedOut, this,
          &ChatLogReader::characterLoggedOut, Qt::QueuedConnection);

  connect(m_workerThread, &QThread::started, m_worker,
          &ChatLogWorker::startMonitoring);

  qDebug() << "ChatLogReader: Created";
}

ChatLogReader::~ChatLogReader() {
  stop();
  if (!m_workerThread->wait(3000)) {
    qWarning()
        << "ChatLogReader: Worker thread did not stop in time, terminating";
    m_workerThread->terminate();
    m_workerThread->wait();
  }

  delete m_worker;
  m_worker = nullptr;

  qDebug() << "ChatLogReader: Destroyed";
}

void ChatLogReader::setCharacterNames(const QStringList &characters) {
  QSet<QString> newSet;
  newSet.reserve(characters.size());
  for (const QString &ch : characters) {
    QString n = ch.trimmed().toLower();
    if (!n.isEmpty())
      newSet.insert(n);
  }

  if (newSet == m_lastCharacterSet) {
    return;
  }

  m_lastCharacterSet = newSet;

  m_worker->setCharacterNames(characters);

  if (m_monitoring && m_workerThread->isRunning()) {
    QMetaObject::invokeMethod(m_worker, "checkForNewFiles",
                              Qt::QueuedConnection);
  }
}

void ChatLogReader::setLogDirectory(const QString &directory) {
  m_worker->setLogDirectory(directory);
  qDebug() << "ChatLogReader: Chatlog directory set to:" << directory;
}

void ChatLogReader::setGameLogDirectory(const QString &directory) {
  m_worker->setGameLogDirectory(directory);
  qDebug() << "ChatLogReader: Gamelog directory set to:" << directory;
}

void ChatLogReader::setEnableChatLogMonitoring(bool enabled) {
  m_worker->setEnableChatLogMonitoring(enabled);
  qDebug() << "ChatLogReader: Chat log monitoring enabled:" << enabled;
}

void ChatLogReader::setEnableGameLogMonitoring(bool enabled) {
  m_worker->setEnableGameLogMonitoring(enabled);
  qDebug() << "ChatLogReader: Game log monitoring enabled:" << enabled;
}

void ChatLogReader::refreshMonitoring() {
  if (!m_monitoring || !m_workerThread->isRunning()) {
    qDebug() << "ChatLogReader: Cannot refresh - monitoring not active";
    return;
  }

  qDebug() << "ChatLogReader: Requesting monitoring refresh";
  QMetaObject::invokeMethod(m_worker, "refreshMonitoring",
                            Qt::QueuedConnection);
}

void ChatLogReader::start() {
  if (m_monitoring) {
    qDebug() << "ChatLogReader: Already monitoring";
    return;
  }

  qDebug() << "ChatLogReader: Starting monitoring";
  m_monitoring = true;

  if (!m_workerThread->isRunning()) {
    m_workerThread->start();
  } else {
    QMetaObject::invokeMethod(m_worker, "startMonitoring",
                              Qt::QueuedConnection);
  }

  emit monitoringStarted();
}

void ChatLogReader::stop() {
  if (!m_monitoring) {
    return;
  }

  qDebug() << "ChatLogReader: Stopping monitoring";
  m_monitoring = false;

  if (m_workerThread->isRunning()) {
    QMetaObject::invokeMethod(m_worker, "stopMonitoring", Qt::QueuedConnection);
  }

  emit monitoringStopped();
}

QString
ChatLogReader::getSystemForCharacter(const QString &characterName) const {
  QMutexLocker locker(&m_locationMutex);
  return m_characterSystems.value(characterName, QString());
}

bool ChatLogReader::isMonitoring() const { return m_monitoring; }

void ChatLogReader::handleSystemChanged(const QString &characterName,
                                        const QString &systemName) {
  {
    QMutexLocker locker(&m_locationMutex);
    m_characterSystems[characterName] = systemName;
  }

  emit systemChanged(characterName, systemName);
}
