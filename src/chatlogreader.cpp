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
#include <QStringDecoder>
#include <QTextStream>
#include <QTimer>

ChatLogWorker::ChatLogWorker(QObject *parent)
    : QObject(parent), m_pollTimer(new QTimer(this)),
      m_scanTimer(new QTimer(this)),
      m_directoryWatcher(new QFileSystemWatcher(this)),
      m_currentPollInterval(SLOW_POLL_MS), m_activeFilesLastPoll(0),
      m_running(false), m_enableChatLogMonitoring(true),
      m_enableGameLogMonitoring(true) {

  // Poll timer for checking file changes
  connect(m_pollTimer, &QTimer::timeout, this, &ChatLogWorker::pollLogFiles);
  m_pollTimer->setInterval(m_currentPollInterval);

  // Scan timer for finding new log files (as backup)
  connect(m_scanTimer, &QTimer::timeout, this,
          &ChatLogWorker::checkForNewFiles);
  m_scanTimer->setInterval(SCAN_INTERVAL_MS);

  // Directory watcher for immediate new file detection
  connect(m_directoryWatcher, &QFileSystemWatcher::directoryChanged, this,
          &ChatLogWorker::onDirectoryChanged);

  updateCustomNameCache();
}

/// Normalize a log line by removing invisible/problematic characters.
/// This function is kept for potential fallback scenarios, but is no longer
/// used in the hot path since EVE Online's log files are generally clean.
/// Previously, this was called for every line parsed, causing significant
/// CPU overhead with 40+ monitored files.
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

static qint64 parseEVETimestamp(const QString &timestamp) {
  if (timestamp.length() != 19) {
    return QDateTime::currentMSecsSinceEpoch();
  }

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

  // Clean up log file states
  qDeleteAll(m_logFiles);
  m_logFiles.clear();

  // Clean up mining timers
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

  // No need to watch directories with polling architecture
  // Just rescan the log files
  scanExistingLogs();

  qDebug() << "ChatLogWorker: Monitoring refresh completed";
}

void ChatLogWorker::startMonitoring() {
  QMutexLocker locker(&m_mutex);

  if (m_running) {
    return;
  }

  m_running = true;

  qDebug() << "ChatLogWorker: Starting polling-based monitoring (ChatLog:"
           << m_enableChatLogMonitoring
           << ", GameLog:" << m_enableGameLogMonitoring << ")";

  // Set up directory watching for instant new file detection
  QStringList watchedDirs = m_directoryWatcher->directories();
  if (!watchedDirs.isEmpty()) {
    m_directoryWatcher->removePaths(watchedDirs);
  }

  if (m_enableChatLogMonitoring && QDir(m_logDirectory).exists()) {
    m_directoryWatcher->addPath(m_logDirectory);
    qDebug() << "ChatLogWorker: Watching chatlog directory:" << m_logDirectory;
  }

  if (m_enableGameLogMonitoring && QDir(m_gameLogDirectory).exists()) {
    m_directoryWatcher->addPath(m_gameLogDirectory);
    qDebug() << "ChatLogWorker: Watching gamelog directory:"
             << m_gameLogDirectory;
  }

  // Scan for existing logs and set up initial state
  scanExistingLogs();

  // Start polling timer
  m_pollTimer->start();

  // Start scan timer for finding new files
  m_scanTimer->start();

  qDebug() << "ChatLogWorker: Monitoring started for" << m_characterNames.size()
           << "characters with" << m_logFiles.size() << "log files"
           << "- poll interval:" << m_currentPollInterval << "ms";
}

void ChatLogWorker::stopMonitoring() {
  QMutexLocker locker(&m_mutex);

  if (!m_running) {
    return;
  }

  m_running = false;

  // Stop timers
  m_pollTimer->stop();
  m_scanTimer->stop();

  // Stop directory watching
  QStringList watchedDirs = m_directoryWatcher->directories();
  if (!watchedDirs.isEmpty()) {
    m_directoryWatcher->removePaths(watchedDirs);
    qDebug() << "ChatLogWorker: Stopped watching" << watchedDirs.size()
             << "directories";
  }

  // Clean up log file states
  qDeleteAll(m_logFiles);
  m_logFiles.clear();

  m_cachedChatListenerMap.clear();
  m_cachedGameListenerMap.clear();

  qDebug() << "ChatLogWorker: Polling-based monitoring stopped";
}

void ChatLogWorker::scanExistingLogs() {
  QElapsedTimer totalTimer;
  totalTimer.start();

  QHash<QString, QString> chatListenerMap = m_cachedChatListenerMap;
  QHash<QString, QString> gameListenerMap = m_cachedGameListenerMap;

  // Build chat listener map (scan directory for Local_*.txt files)
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

  // Build game listener map (scan directory for *.txt game logs)
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

  // Track which files should exist after this scan
  QSet<QString> newFiles;

  // Set up LogFileState for each character's log files
  for (const QString &characterName : m_characterNames) {
    // Chat log setup
    if (m_enableChatLogMonitoring) {
      QString chatLogFile;
      QString key = characterName.toLower();
      if (chatListenerMap.contains(key)) {
        chatLogFile = chatListenerMap.value(key);
      } else {
        chatLogFile = findChatLogFileForCharacter(characterName);
      }

      if (!chatLogFile.isEmpty()) {
        newFiles.insert(chatLogFile);

        // Create or reuse LogFileState
        if (!m_logFiles.contains(chatLogFile)) {
          LogFileState *state = new LogFileState();
          state->filePath = chatLogFile;
          state->characterName = characterName;
          state->isChatLog = true;
          readInitialState(state);
          m_logFiles[chatLogFile] = state;

          qDebug() << "ChatLogWorker: Monitoring CHATLOG for" << characterName
                   << ":" << chatLogFile;
        }
      }
    }

    // Game log setup
    if (m_enableGameLogMonitoring) {
      QString gameLogFile;
      QString key = characterName.toLower();
      if (gameListenerMap.contains(key)) {
        gameLogFile = gameListenerMap.value(key);
      } else {
        gameLogFile = findGameLogFileForCharacter(characterName);
      }

      if (!gameLogFile.isEmpty()) {
        newFiles.insert(gameLogFile);

        // Create or reuse LogFileState
        if (!m_logFiles.contains(gameLogFile)) {
          LogFileState *state = new LogFileState();
          state->filePath = gameLogFile;
          state->characterName = characterName;
          state->isChatLog = false;
          readInitialState(state);
          m_logFiles[gameLogFile] = state;

          qDebug() << "ChatLogWorker: Monitoring GAMELOG for" << characterName
                   << ":" << gameLogFile;
        }
      }
    }
  }

  // Remove LogFileState objects for files that no longer exist or are not
  // monitored
  QStringList staleFiles;
  for (auto it = m_logFiles.constBegin(); it != m_logFiles.constEnd(); ++it) {
    if (!newFiles.contains(it.key())) {
      staleFiles.append(it.key());
    }
  }

  for (const QString &staleFile : staleFiles) {
    qDebug() << "ChatLogWorker: Removing stale log file:" << staleFile;
    delete m_logFiles.take(staleFile);
  }

  qDebug() << "ChatLogWorker: Now monitoring" << m_logFiles.count()
           << "log files";
  qDebug() << "ChatLogWorker: scanExistingLogs total took"
           << totalTimer.elapsed() << "ms";
}

void ChatLogWorker::readInitialState(LogFileState *state) {
  QFileInfo fi(state->filePath);

  if (!fi.exists()) {
    state->lastSize = 0;
    state->position = 0;
    state->lastModified = 0;
    return;
  }

  state->lastSize = fi.size();
  state->lastModified = fi.lastModified().toMSecsSinceEpoch();

  const qint64 tailSize = 65536;
  const qint64 fallbackSize = 5 * 1024 * 1024;

  QFile file(state->filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    state->position = 0;
    return;
  }

  qint64 fileSize = file.size();
  qint64 startPos = 0;

  // Read tail of file to find initial system
  if (fileSize > tailSize + 1024) {
    startPos = fileSize - tailSize - 1024;
    file.seek(startPos);
  }

  QByteArray tailData = file.readAll();

  // Chatlogs use UTF-16 LE, gamelogs use UTF-8
  QString tailContent;
  if (state->isChatLog) {
    auto decoder = QStringDecoder(QStringDecoder::Utf16LE);
    tailContent = decoder(tailData);
    // Remove BOM character if present
    if (tailContent.startsWith(QChar(0xFEFF))) {
      tailContent.remove(0, 1);
    }
  } else {
    tailContent = QString::fromUtf8(tailData);
  }

  QStringList tailLines = tailContent.split('\n', Qt::SkipEmptyParts);

  QString lastRelevantLine;

  if (state->isChatLog) {
    // Chat log: look for "Channel changed to Local:" lines
    static QRegularExpression systemChangePattern(
        R"(\[\s*([\d.\s:]+)\]\s*EVE System\s*>\s*Channel changed to Local\s*:\s*(.+))",
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::UseUnicodePropertiesOption);

    for (int i = tailLines.size() - 1; i >= 0; --i) {
      QString line = tailLines[i].trimmed();
      if (systemChangePattern.match(line).hasMatch()) {
        lastRelevantLine = line;
        break;
      }
    }

    // If tail scan found nothing and file is small enough, scan entire file
    if (lastRelevantLine.isEmpty() && fileSize <= fallbackSize) {
      qDebug()
          << "ChatLogWorker: tail scan found nothing, scanning entire file for"
          << state->filePath << "(size:" << fileSize << "bytes)";
      file.seek(0);
      QByteArray allData = file.readAll();

      // Chatlogs use UTF-16 LE, gamelogs use UTF-8
      QString allContent;
      if (state->isChatLog) {
        auto decoder = QStringDecoder(QStringDecoder::Utf16LE);
        allContent = decoder(allData);
        // Remove BOM character if present
        if (allContent.startsWith(QChar(0xFEFF))) {
          allContent.remove(0, 1);
        }
      } else {
        allContent = QString::fromUtf8(allData);
      }

      QStringList allLines = allContent.split('\n', Qt::SkipEmptyParts);

      for (const QString &line : allLines) {
        QString s = extractSystemFromLine(line.trimmed());
        if (!s.isEmpty()) {
          lastRelevantLine = line.trimmed();
        }
      }

      if (lastRelevantLine.isEmpty()) {
        qDebug()
            << "ChatLogWorker: No system change found in entire chatlog for"
            << state->characterName;
      }
    } else if (lastRelevantLine.isEmpty() && fileSize > fallbackSize) {
      qDebug() << "ChatLogWorker: tail scan found nothing and file too large ("
               << fileSize << "bytes) for full scan:" << state->filePath;
    }

    if (!lastRelevantLine.isEmpty()) {
      QRegularExpressionMatch match =
          systemChangePattern.match(lastRelevantLine);
      if (match.hasMatch()) {
        QString timestampStr = match.captured(1).trimmed();
        QString rawSystem = match.captured(2).trimmed();
        QString newSystem = sanitizeSystemName(rawSystem);

        qint64 updateTime = parseEVETimestamp(timestampStr);

        CharacterLocation &location =
            m_characterLocations[state->characterName];

        // Early exit if already in this system (skip timestamp checks)
        if (!location.systemName.isEmpty() &&
            location.systemName == newSystem) {
          return;
        }

        if (updateTime > location.lastUpdate || location.systemName.isEmpty()) {
          location.characterName = state->characterName;
          location.systemName = newSystem;
          location.lastUpdate = updateTime;

          qDebug() << "ChatLogWorker: Initial system for"
                   << state->characterName << ":" << newSystem << "(from"
                   << timestampStr << ")";
          emit systemChanged(state->characterName, newSystem);
        } else {
          qDebug() << "ChatLogWorker: Chatlog data for" << state->characterName
                   << "is older than current position, skipping";
        }
      }

      parseLogLine(lastRelevantLine, state->characterName);
    }
  } else {
    // Game log: look for "Jumping from" and conduit jump lines
    static QRegularExpression jumpPattern(
        R"(\[\s*([\d.\s:]+)\]\s*\(None\)\s*Jumping from\s+(.+?)\s+to\s+(.+))");
    static QRegularExpression conduitPattern(
        R"(\[\s*([\d.\s:]+)\]\s*\(notify\)\s*A Conduit Field activated by .+ jumps you to\s+(.+))");

    for (int i = tailLines.size() - 1; i >= 0; --i) {
      QString line = tailLines[i].trimmed();
      if (jumpPattern.match(line).hasMatch() ||
          conduitPattern.match(line).hasMatch()) {
        lastRelevantLine = line;
        break;
      }
    }

    if (!lastRelevantLine.isEmpty()) {
      QRegularExpressionMatch jumpMatch = jumpPattern.match(lastRelevantLine);
      QRegularExpressionMatch conduitMatch =
          conduitPattern.match(lastRelevantLine);

      QString timestampStr;
      QString newSystem;

      if (jumpMatch.hasMatch()) {
        timestampStr = jumpMatch.captured(1).trimmed();
        QString toSystem = jumpMatch.captured(3).trimmed();
        newSystem = sanitizeSystemName(toSystem);
      } else if (conduitMatch.hasMatch()) {
        timestampStr = conduitMatch.captured(1).trimmed();
        QString toSystem = conduitMatch.captured(2).trimmed();
        newSystem = sanitizeSystemName(toSystem);
      }

      if (!newSystem.isEmpty()) {
        qint64 updateTime = parseEVETimestamp(timestampStr);

        CharacterLocation &location =
            m_characterLocations[state->characterName];

        // Early exit if already in this system (skip timestamp checks)
        if (!location.systemName.isEmpty() &&
            location.systemName == newSystem) {
          return;
        }

        if (updateTime > location.lastUpdate || location.systemName.isEmpty()) {
          location.characterName = state->characterName;
          location.systemName = newSystem;
          location.lastUpdate = updateTime;

          qDebug() << "ChatLogWorker: Updated system from GAMELOG for"
                   << state->characterName << ":" << newSystem << "(from"
                   << timestampStr << ") - overriding chatlog data";
          emit systemChanged(state->characterName, newSystem);
        } else {
          qDebug() << "ChatLogWorker: GAMELOG jump for" << state->characterName
                   << "is older than current location (current:"
                   << location.systemName << "at" << location.lastUpdate
                   << "ms, gamelog:" << newSystem << "at" << updateTime
                   << "ms), keeping current system";
        }
      }
    }
  }

  // Set position to end of file (we've processed initial state)
  state->position = fileSize;
  file.close();
}

void ChatLogWorker::checkForNewFiles() {
  QMutexLocker locker(&m_mutex);

  if (!m_running) {
    return;
  }

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

      if (m_knownChatLogFiles.isEmpty()) {
        m_knownChatLogFiles = currentChatFiles;
        newFilesFound = true;
      } else if (currentChatFiles != m_knownChatLogFiles) {
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

      if (m_knownGameLogFiles.isEmpty()) {
        m_knownGameLogFiles = currentGameFiles;
        newFilesFound = true;
      } else if (currentGameFiles != m_knownGameLogFiles) {
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

  auto it = m_fileToCharacterCache.find(filePath);
  if (it != m_fileToCharacterCache.end() && it->second == modTime) {
    return it->first;
  }

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

  if (!characterName.isEmpty()) {
    m_fileToCharacterCache[filePath] = qMakePair(characterName, modTime);
  }

  return characterName;
}

void ChatLogWorker::pollLogFiles() {
  QMutexLocker locker(&m_mutex);

  if (!m_running) {
    return;
  }

  bool hadActivity = false;

  // Check all monitored log files for changes
  for (auto it = m_logFiles.begin(); it != m_logFiles.end(); ++it) {
    LogFileState *state = it.value();

    if (readNewLines(state)) {
      hadActivity = true;
    }
  }

  // Update polling rate based on activity
  updatePollingRate(hadActivity);
}

bool ChatLogWorker::readNewLines(LogFileState *state) {
  QFileInfo fi(state->filePath);

  if (!fi.exists()) {
    return false;
  }

  qint64 currentSize = fi.size();
  qint64 currentModified = fi.lastModified().toMSecsSinceEpoch();

  // Check if file has changed
  if (currentSize == state->lastSize &&
      currentModified == state->lastModified) {
    state->hadActivityLastPoll = false;
    return false;
  }

  // Handle file truncation (log rotation or reset)
  if (currentSize < state->lastSize) {
    qDebug() << "ChatLogWorker: File truncated, resetting position:"
             << state->filePath;
    state->position = 0;
    state->partialLine.clear();
  }

  QFile file(state->filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }

  // Seek to last position
  if (state->position > currentSize) {
    state->position = 0; // File was truncated
  }

  file.seek(state->position);

  // Read new data
  QByteArray newData = file.readAll();
  file.close();

  if (newData.isEmpty()) {
    state->lastSize = currentSize;
    state->lastModified = currentModified;
    state->hadActivityLastPoll = false;
    return false;
  }

  // Update position
  state->position += newData.size();
  state->lastSize = currentSize;
  state->lastModified = currentModified;

  // Chatlogs use UTF-16 LE, gamelogs use UTF-8
  QString newText;
  if (state->isChatLog) {
    auto decoder = QStringDecoder(QStringDecoder::Utf16LE);
    newText = decoder(newData);
  } else {
    newText = QString::fromUtf8(newData);
  }

  // Combine partial line from previous read with new data
  QString text = state->partialLine + newText;
  QStringList lines = text.split('\n');

  // Save last line if it doesn't end with newline (partial line)
  if (!text.endsWith('\n')) {
    state->partialLine = lines.takeLast();
  } else {
    state->partialLine.clear();
  }

  // Process complete lines
  bool hadRelevantLines = false;
  for (const QString &line : lines) {
    if (line.isEmpty()) {
      continue;
    }

    // Fast check: skip 95% of lines that aren't system changes or jumps
    if (!shouldParseLine(line, state->isChatLog)) {
      continue;
    }

    // This is a relevant line, parse it
    hadRelevantLines = true;
    parseLogLine(line.trimmed(), state->characterName);
  }

  state->hadActivityLastPoll = hadRelevantLines;
  return hadRelevantLines;
}

bool ChatLogWorker::shouldParseLine(const QString &line, bool isChatLog) {
  // Fast character-based filter to skip most lines
  // We're looking for:
  // - Chat logs: "EVE System" (for "Channel changed to Local:")
  // - Game logs: "Jumping from", "Undocking", or event markers like (notify),
  // (question), (mining)

  if (isChatLog) {
    // Look for "EVE System" indicator (case insensitive check via chars)
    // Most lines are player chat, skip those fast
    if (line.contains("EVE System", Qt::CaseInsensitive)) {
      return true;
    }
  } else {
    // Game log: look for jump, undock, or event type markers
    if (line.contains("Jumping", Qt::CaseInsensitive) ||
        line.contains("Undocking", Qt::CaseInsensitive) ||
        line.contains("(notify)", Qt::CaseInsensitive) ||
        line.contains("(question)", Qt::CaseInsensitive) ||
        line.contains("(mining)", Qt::CaseInsensitive) ||
        line.contains("(None)", Qt::CaseInsensitive)) {
      return true;
    }
  }

  return false;
}

void ChatLogWorker::updatePollingRate(bool hadActivity) {
  int desiredInterval = SLOW_POLL_MS;

  // Use fast polling if we had activity this cycle
  if (hadActivity) {
    desiredInterval = FAST_POLL_MS;
    m_activeFilesLastPoll = 10; // Reset momentum counter on activity
  } else {
    // Decrement counter when no activity
    if (m_activeFilesLastPoll > 0) {
      m_activeFilesLastPoll--;
    }
  }

  // Keep fast polling for a bit after activity stops (momentum)
  if (m_activeFilesLastPoll > 0) {
    desiredInterval = FAST_POLL_MS;
  }

  // Update timer interval if it changed
  if (desiredInterval != m_currentPollInterval) {
    m_currentPollInterval = desiredInterval;
    m_pollTimer->setInterval(desiredInterval);

    // Don't spam logs, only when switching rates
    static int lastLoggedInterval = -1;
    if (lastLoggedInterval != desiredInterval) {
      qDebug() << "ChatLogWorker: Switching poll rate to" << desiredInterval
               << "ms";
      lastLoggedInterval = desiredInterval;
    }
  }
}

void ChatLogWorker::parseLogLine(const QString &line,
                                 const QString &characterName) {
  // Trust EVE's logs to be clean - skip expensive normalization
  // Only use simple trimming for performance with 40+ monitored files
  QString workingLine = line.trimmed();

  // Remove BOM character that appears at the start of each chatlog line
  if (!workingLine.isEmpty() && workingLine[0] == QChar(0xFEFF)) {
    workingLine.remove(0, 1);
  }

  if (workingLine.isEmpty() || workingLine.length() < 25 ||
      workingLine.length() > 1000) {
    return;
  }

  const int searchStart = 20;
  int notifyPos =
      workingLine.indexOf("(notify)", searchStart, Qt::CaseInsensitive);
  int questionPos =
      workingLine.indexOf("(question)", searchStart, Qt::CaseInsensitive);
  int miningPos =
      workingLine.indexOf("(mining)", searchStart, Qt::CaseInsensitive);
  int nonePos = workingLine.indexOf("(None)", searchStart, Qt::CaseInsensitive);
  int eveSystemPos =
      workingLine.indexOf("EVE System", searchStart, Qt::CaseInsensitive);

  if (eveSystemPos != -1) {
    static QRegularExpression systemChangePattern(
        R"(\[\s*([\d.\s:]+)\]\s*EVE System\s*>\s*Channel changed to Local\s*:\s*(.+))",
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::UseUnicodePropertiesOption);

    QRegularExpressionMatch match = systemChangePattern.match(workingLine);
    if (match.hasMatch()) {
      QString timestampStr = match.captured(1).trimmed();
      QString rawSystem = match.captured(2).trimmed();

      QString newSystem = sanitizeSystemName(rawSystem);

      CharacterLocation &location = m_characterLocations[characterName];

      // Early exit if already in this system (skip timestamp checks)
      if (!location.systemName.isEmpty() && location.systemName == newSystem) {
        return;
      }

      qint64 updateTime = parseEVETimestamp(timestampStr);

      if (updateTime > location.lastUpdate ||
          (updateTime == location.lastUpdate &&
           location.systemName != newSystem)) {
        location.characterName = characterName;
        location.systemName = newSystem;
        location.lastUpdate = updateTime;

        qDebug() << "ChatLogWorker: System change detected (chatlog):"
                 << characterName << "->" << newSystem << "(from"
                 << timestampStr << ", was at" << location.systemName << "at"
                 << location.lastUpdate << "ms)";

        qint64 emitTime = QDateTime::currentMSecsSinceEpoch();
        emit systemChanged(characterName, newSystem);
        qint64 emitElapsed = QDateTime::currentMSecsSinceEpoch() - emitTime;
        qDebug() << "ChatLogWorker: systemChanged signal emitted in"
                 << emitElapsed << "ms";
      } else {
        qDebug() << "ChatLogWorker: Chatlog system change for" << characterName
                 << "is older than current location (current:"
                 << location.systemName << "at" << location.lastUpdate
                 << "ms, chatlog:" << newSystem << "at" << updateTime
                 << "ms), ignoring";
      }
    }
    return;
  }

  if (questionPos != -1) {
    static QRegularExpression fleetInvitePattern(
        R"(\[\s*[\d.\s:]+\]\s*\(question\)\s*<a href="[^"]+">([^<]+)</a>\s*wants you to join their fleet)");

    QRegularExpressionMatch fleetMatch = fleetInvitePattern.match(workingLine);
    if (fleetMatch.hasMatch()) {
      QString inviter = fleetMatch.captured(1).trimmed();
      QString eventText = QString("Fleet invite from %1").arg(inviter);
      qDebug() << "ChatLogWorker: Fleet invite detected for" << characterName
               << "from" << inviter;
      emit combatEventDetected(characterName, "fleet_invite", eventText);
    }
    return;
  }

  if (notifyPos != -1) {
    int followingPos =
        workingLine.indexOf("Following", notifyPos, Qt::CaseInsensitive);
    int regroupingPos =
        workingLine.indexOf("Regrouping", notifyPos, Qt::CaseInsensitive);
    int compressedPos =
        workingLine.indexOf("compressed", notifyPos, Qt::CaseInsensitive);
    int cloakPos = workingLine.indexOf("cloak deactivates", notifyPos,
                                       Qt::CaseInsensitive);
    int crystalBrokePos = workingLine.indexOf(
        "deactivates due to the destruction", notifyPos, Qt::CaseInsensitive);

    if (followingPos != -1) {
      static QRegularExpression followWarpPattern(
          R"(\[\s*[\d.\s:]+\]\s*\(notify\)\s*Following\s+(.+?)\s+in warp)");

      QRegularExpressionMatch followMatch =
          followWarpPattern.match(workingLine);
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

      QRegularExpressionMatch regroupMatch = regroupPattern.match(workingLine);
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
          compressionPattern.match(workingLine);
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

      QRegularExpressionMatch decloakMatch = decloakPattern.match(workingLine);
      if (decloakMatch.hasMatch()) {
        QString source = decloakMatch.captured(1).trimmed();
        QString eventText = QString("Decloaked by %1").arg(source);
        qDebug() << "ChatLogWorker: Decloak detected for" << characterName
                 << "- Source:" << source;
        emit combatEventDetected(characterName, "decloak", eventText);
        return;
      }
    }

    if (crystalBrokePos != -1) {
      static QRegularExpression crystalPattern(
          R"(\[\s*[\d.\s:]+\]\s*\(notify\)\s*(.+?)\s+deactivates due to the destruction of the\s+(.+?)\s+it was fitted with)");

      QRegularExpressionMatch crystalMatch = crystalPattern.match(workingLine);
      if (crystalMatch.hasMatch()) {
        QString module = crystalMatch.captured(1).trimmed();
        QString crystal = crystalMatch.captured(2).trimmed();
        QString eventText = QString("Crystal broke: %1").arg(crystal);
        qDebug() << "ChatLogWorker: Mining crystal broke detected for"
                 << characterName << "- Module:" << module
                 << "- Crystal:" << crystal;
        emit combatEventDetected(characterName, "crystal_broke", eventText);
        return;
      }
    }
    return;
  }

  if (miningPos != -1) {
    static QRegularExpression miningPattern(R"(\[\s*[\d.\s:]+\]\s*\(mining\))");

    QRegularExpressionMatch miningMatch = miningPattern.match(workingLine);
    if (miningMatch.hasMatch()) {
      qDebug() << "ChatLogWorker: Mining event detected";
      handleMiningEvent(characterName, "ore");
    }
    return;
  }

  if (nonePos != -1) {
    int jumpingPos =
        workingLine.indexOf("Jumping", nonePos, Qt::CaseInsensitive);
    if (jumpingPos != -1) {
      static QRegularExpression jumpPattern(
          R"(\[\s*([\d.\s:]+)\]\s*\(None\)\s*Jumping from\s+(.+?)\s+to\s+(.+))");

      QRegularExpressionMatch jumpMatch = jumpPattern.match(workingLine);
      if (jumpMatch.hasMatch()) {
        QString timestampStr = jumpMatch.captured(1).trimmed();
        QString fromSystem = jumpMatch.captured(2).trimmed();
        QString toSystem = jumpMatch.captured(3).trimmed();

        QString newSystem = sanitizeSystemName(toSystem);

        CharacterLocation &location = m_characterLocations[characterName];

        // Early exit if already in this system (skip timestamp checks)
        if (!location.systemName.isEmpty() &&
            location.systemName == newSystem) {
          return;
        }

        qint64 updateTime = parseEVETimestamp(timestampStr);

        if (updateTime > location.lastUpdate ||
            (updateTime == location.lastUpdate &&
             location.systemName != newSystem)) {
          location.characterName = characterName;
          location.systemName = newSystem;
          location.lastUpdate = updateTime;

          QDateTime detectTime = QDateTime::currentDateTime();
          qDebug() << "ChatLogWorker: System jump detected (gamelog) at"
                   << detectTime.toString("HH:mm:ss.zzz") << "-"
                   << characterName << "from" << fromSystem << "to" << newSystem
                   << "(jump timestamp:" << timestampStr << ", was at"
                   << location.systemName << "at" << location.lastUpdate
                   << "ms)";

          qint64 emitTime = QDateTime::currentMSecsSinceEpoch();
          emit systemChanged(characterName, newSystem);
          qint64 emitElapsed = QDateTime::currentMSecsSinceEpoch() - emitTime;
          qDebug() << "ChatLogWorker: systemChanged signal emitted in"
                   << emitElapsed << "ms";
        } else {
          qDebug() << "ChatLogWorker: Gamelog jump for" << characterName
                   << "is older than current location (current:"
                   << location.systemName << "at" << location.lastUpdate
                   << "ms, gamelog:" << newSystem << "at" << updateTime
                   << "ms), ignoring";
        }
      }
    }
  }

  // Check for conduit jumps: "(notify) A Conduit Field activated by ... jumps
  // you to [system]"
  if (notifyPos != -1) {
    int conduitPos =
        workingLine.indexOf("Conduit Field", notifyPos, Qt::CaseInsensitive);
    if (conduitPos != -1) {
      static QRegularExpression conduitPattern(
          R"(\[\s*([\d.\s:]+)\]\s*\(notify\)\s*A Conduit Field activated by .+ jumps you to\s+(.+))");

      QRegularExpressionMatch conduitMatch = conduitPattern.match(workingLine);
      if (conduitMatch.hasMatch()) {
        QString timestampStr = conduitMatch.captured(1).trimmed();
        QString toSystem = conduitMatch.captured(2).trimmed();

        QString newSystem = sanitizeSystemName(toSystem);

        CharacterLocation &location = m_characterLocations[characterName];

        // Early exit if already in this system (skip timestamp checks)
        if (!location.systemName.isEmpty() &&
            location.systemName == newSystem) {
          return;
        }

        qint64 updateTime = parseEVETimestamp(timestampStr);

        if (updateTime > location.lastUpdate ||
            (updateTime == location.lastUpdate &&
             location.systemName != newSystem)) {
          location.characterName = characterName;
          location.systemName = newSystem;
          location.lastUpdate = updateTime;

          QDateTime detectTime = QDateTime::currentDateTime();
          qDebug() << "ChatLogWorker: Conduit jump detected (gamelog) at"
                   << detectTime.toString("HH:mm:ss.zzz") << "-"
                   << characterName << "to" << newSystem
                   << "(jump timestamp:" << timestampStr << ")";

          emit systemChanged(characterName, newSystem);
        } else {
          qDebug() << "ChatLogWorker: Conduit jump for" << characterName
                   << "is older than current location (current:"
                   << location.systemName << "at" << location.lastUpdate
                   << "ms, gamelog:" << newSystem << "at" << updateTime
                   << "ms), ignoring";
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

void ChatLogWorker::onDirectoryChanged(const QString &path) {
  qDebug() << "ChatLogWorker: Directory changed detected:" << path
           << "- triggering immediate file scan";

  // Immediately check for new files when directory changes
  // This catches new character logins much faster than the 5-minute timer
  checkForNewFiles();
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
