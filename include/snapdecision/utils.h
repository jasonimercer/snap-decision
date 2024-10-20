#pragma once

#include <windows.h>

#include <QDateTime>
#include <QFileInfo>
#include <QString>
#include <optional>
#include <string>

#include "types.h"

inline TimeMs getCurrentTimeMilliseconds()
{
  QDateTime currentTime = QDateTime::currentDateTime();
  return static_cast<TimeMs>(currentTime.toMSecsSinceEpoch());
}

inline QString timeToString(const TimeMs& time)
{
  QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(time);
  return dateTime.toString("yyyy-MM-dd HH:mm:ss");
}

inline QString timeToStringPrecise(const TimeMs& time)
{
  QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(time);
  return dateTime.toString("HH:mm:ss.zzz");
}

inline std::optional<int> toInt(const QString& str)
{
  bool ok;
  int result = str.toInt(&ok);

  if (ok)
  {
    return result;
  }

  return std::nullopt;
}

inline std::optional<int> toInt(const std::string& str)
{
  return toInt(QString::fromStdString(str));
}

// Helper function to extract filename from absolute path
inline std::string extractFilename(const std::string& absolutePath)
{
  return QFileInfo(QString::fromStdString(absolutePath)).fileName().toStdString();
}

inline bool setFileHidden(const QString& file_path)
{
  // Convert QString to LPCWSTR (wide string)
  LPCWSTR path = (const wchar_t*)file_path.utf16();

  // Get current attributes of the file
  DWORD attributes = GetFileAttributes(path);

  if (attributes == INVALID_FILE_ATTRIBUTES)
  {
    // Handle error: unable to get file attributes
    return false;
  }

  // Set the hidden attribute
  if (!SetFileAttributes(path, attributes | FILE_ATTRIBUTE_HIDDEN))
  {
    // Handle error: unable to set file attributes
    return false;
  }

  return true;
}
