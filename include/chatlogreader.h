#ifndef CHATLOGREADER_H
#define CHATLOGREADER_H

#include <QDir>
#include <QFileSystemWatcher>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QThread>
#include <QTimer>

struct CharacterLocation {
  QString characterName;
  QString systemName;
  qint64 lastUpdate;

  CharacterLocation() : lastUpdate(0) {}
  CharacterLocation(const QString &name, const QString &system, qint64 time)
      : characterName(name), systemName(system), lastUpdate(time) {}
};

class ChatLogWorker : public QObject {
  Q_OBJECT

public:
  explicit ChatLogWorker(QObject *parent = nullptr);
  ~ChatLogWorker();

  void setCharacterNames(const QStringList &characters);
  void setLogDirectory(const QString &directory);
  void setGameLogDirectory(const QString &directory);
  void setEnableChatLogMonitoring(bool enabled);
  void setEnableGameLogMonitoring(bool enabled);

signals:
  void systemChanged(const QString &characterName, const QString &systemName);
  void characterLoggedIn(const QString &characterName);
  void characterLoggedOut(const QString &characterName);
  void combatEventDetected(const QString &characterName,
                           const QString &eventType, const QString &eventText);
  void combatDetected(const QString &characterName, const QString &combatData);

public slots:
  void startMonitoring();
  void stopMonitoring();
  void refreshMonitoring();
  void processLogFile(const QString &filePath);
  void markFileDirty(const QString &filePath);
  void checkForNewFiles();
  QString findLastMatchingLineInFile(const QString &filePath,
                                     const QRegularExpression &pattern,
                                     qint64 tailSize = 65536);
  QHash<QString, QString> buildListenerToFileMap(const QDir &dir,
                                                 const QStringList &filters,
                                                 int maxAgeHours = 24);

private:
  QString findLogFileForCharacter(const QString &characterName);
  QString findChatLogFileForCharacter(const QString &characterName);
  QString findGameLogFileForCharacter(const QString &characterName);
  QString extractSystemFromLine(const QString &logLine);
  QString sanitizeSystemName(const QString &system);
  QString extractCharacterFromLogFile(const QString &filePath);
  void parseLogLine(const QString &line, const QString &characterName);
  void scanExistingLogs();
  void handleMiningEvent(const QString &characterName, const QString &ore);
  void onMiningTimeout(const QString &characterName);
  void cleanupDebounceTimer(const QString &filePath);

  QString m_logDirectory;
  QString m_gameLogDirectory;
  QStringList m_characterNames;
  QHash<QString, QString> m_characterToLogFile;
  QHash<QString, qint64> m_filePositions;
  QHash<QString, qint64> m_fileLastModified;
  QHash<QString, qint64> m_fileLastSize;
  QHash<QString, qint64> m_fileLastProcessed;
  QHash<QString, QTimer *> m_debounceTimers;
  QHash<QString, CharacterLocation> m_characterLocations;
  QHash<QString, QString> m_fileToKeyMap;
  QFileSystemWatcher *m_fileWatcher;
  QTimer *m_scanTimer;
  QMutex m_mutex;
  bool m_running;
  bool m_enableChatLogMonitoring;
  bool m_enableGameLogMonitoring;
  QDateTime m_lastChatDirScanTime;
  QDateTime m_lastGameDirScanTime;
  QHash<QString, QString> m_cachedChatListenerMap;
  QHash<QString, QString> m_cachedGameListenerMap;
  QHash<QString, QTimer *> m_miningTimers;
  QHash<QString, bool> m_miningActiveState;
};

class ChatLogReader : public QObject {
  Q_OBJECT

public:
  explicit ChatLogReader(QObject *parent = nullptr);
  ~ChatLogReader();

  void setCharacterNames(const QStringList &characters);
  void setLogDirectory(const QString &directory);
  void setGameLogDirectory(const QString &directory);
  void setEnableChatLogMonitoring(bool enabled);
  void setEnableGameLogMonitoring(bool enabled);
  void start();
  void stop();
  void refreshMonitoring();

  QString getSystemForCharacter(const QString &characterName) const;
  bool isMonitoring() const;

signals:
  void systemChanged(const QString &characterName, const QString &systemName);
  void characterLoggedIn(const QString &characterName);
  void characterLoggedOut(const QString &characterName);
  void combatEventDetected(const QString &characterName,
                           const QString &eventType, const QString &eventText);
  void monitoringStarted();
  void monitoringStopped();

private slots:
  void handleSystemChanged(const QString &characterName,
                           const QString &systemName);

private:
  QThread *m_workerThread;
  ChatLogWorker *m_worker;
  mutable QMutex m_locationMutex;
  QHash<QString, QString> m_characterSystems;
  bool m_monitoring;
  QSet<QString> m_lastCharacterSet;
};

#endif
