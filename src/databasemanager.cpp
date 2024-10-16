#include "snapdecision/databasemanager.h"

#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QString>
#include <QVariant>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "snapdecision/enums.h"

std::string describeDatabase(const QSqlDatabase& db)
{
  if (!db.isOpen())
  {
    return "Database is not open.";
  }

  std::string description;

  // Get the list of tables
  QStringList tables = db.tables();
  int tableCount = tables.size();

  if (tableCount == 0)
  {
    description += "Number of tables: " + std::to_string(tableCount) + "\n";
  }

  for (const QString& tableName : tables)
  {
    QSqlQuery query(db);
    query.exec("SELECT COUNT(*) FROM " + tableName);

    int rowCount = 0;
    if (query.next())
    {
      rowCount = query.value(0).toInt();
    }
    else
    {
      description += "Failed to query table: " + tableName.toStdString() + "\n";
      continue;
    }

    description += "Table: " + tableName.toStdString() + ", Rows: " + std::to_string(rowCount) + "\n";
  }

  return description;
}

template <typename T>
T getDefaultDatabaseValue(const std::string& column_name)
{
  if constexpr (std::is_same_v<T, std::string>)
  {
    if (column_name == "decision")
      return to_string(DecisionType::Unknown);
    if (column_name == "exposure_program")
      return to_string(ExposureProgram::NotDefined);
    if (column_name == "metering_mode")
      return to_string(MeteringMode::Unknown);
    if (column_name == "make" || column_name == "model" || column_name == "date_time" ||
        column_name == "date_time_original" || column_name == "sub_sec_time_original")
      return "";
    // Add more string-type columns and their defaults here
  }
  else if constexpr (std::is_same_v<T, int>)
  {
    if (column_name == "image_width" || column_name == "image_height" || column_name == "bits_per_sample" ||
        column_name == "iso_speed_ratings" || column_name == "orientation")
      return 0;
    // Add more integer-type columns and their defaults here
  }
  else if constexpr (std::is_same_v<T, double>)
  {
    if (column_name == "f_number" || column_name == "exposure_time" || column_name == "aperture_value" ||
        column_name == "brightness_value" || column_name == "exposure_bias_value" ||
        column_name == "subject_distance" || column_name == "focal_length")
      return 0.0;
    // Add more double-type columns and their defaults here
  }
  else if constexpr (std::is_same_v<T, std::size_t>)
  {
    if (column_name == "creation_ms")
      return 0.0;
    // Add more double-type columns and their defaults here
  }
  else
  {
    static_assert(std::is_same_v<T, void>, "Unsupported type for getDefaultDatabaseValue");
  }

  std::cerr << "Invalid column name or type for default value: " << column_name << std::endl;
  throw std::invalid_argument("Invalid column name or type for default value: " + column_name);
}

// Helper function to extract filename from absolute path
std::string extractFilename(const std::string& absolutePath)
{
    return QFileInfo(QString::fromStdString(absolutePath)).fileName().toStdString();
}

// Helper function to convert the entire table from absolute paths to filenames
bool convertTableToUseFilenames(QSqlDatabase& db, const DiagnosticFunction& diagnostic)
{
    if (!db.isOpen())
    {
        diagnostic(LogLevel::Error, "Database is not open.");
        return false;
    }

    // Start a transaction
    if (!db.transaction())
    {
        diagnostic(LogLevel::Error, "Failed to start database transaction: " + db.lastError().text().toStdString());
        return false;
    }

    // Select all rows from the table
    QSqlQuery selectQuery(db);
    if (!selectQuery.exec("SELECT absolute_path FROM image_data"))
    {
        diagnostic(LogLevel::Error, "Failed to select absolute_path from image_data: " + selectQuery.lastError().text().toStdString());
        db.rollback();
        return false;
    }

    // Prepare the update query
    QSqlQuery updateQuery(db);
    updateQuery.prepare("UPDATE image_data SET absolute_path = :filename WHERE absolute_path = :absolute_path");

    while (selectQuery.next())
    {
        QString absolutePath = selectQuery.value(0).toString();
        QString filename = QFileInfo(absolutePath).fileName();

        updateQuery.bindValue(":filename", filename);
        updateQuery.bindValue(":absolute_path", absolutePath);

        if (!updateQuery.exec())
        {
            diagnostic(LogLevel::Error, "Failed to update absolute_path from " + absolutePath.toStdString() + " to " + filename.toStdString() + ": " + updateQuery.lastError().text().toStdString());
            db.rollback();
            return false;
        }
    }

    // Commit the transaction
    if (!db.commit())
    {
        diagnostic(LogLevel::Error, "Failed to commit database transaction: " + db.lastError().text().toStdString());
        return false;
    }

    return true;
}

template <typename T>
std::optional<T> getColumnValue(QSqlDatabase& db, const DiagnosticFunction& diagnostic, const std::string& primaryKey,
                                const std::string& columnName)
{
    if (!db.isOpen())
    {
        diagnostic(LogLevel::Error, "Database is not open.");
        return std::nullopt;
    }

    T defaultValue = getDefaultDatabaseValue<T>(columnName);

    // Convert primaryKey to filename
    std::string filename = extractFilename(primaryKey);

    // Attempt to retrieve the value using the filename
    QSqlQuery query(db);
    query.prepare("SELECT " + QString::fromStdString(columnName) + " FROM image_data WHERE absolute_path = :primaryKey");
    query.bindValue(":primaryKey", QString::fromStdString(filename));

    if (!query.exec())
    {
        diagnostic(LogLevel::Error, "Failed to execute query: " + query.lastError().text().toStdString());
        return std::nullopt;
    }

    if (query.next())
    {
        // Value found, return it
        if constexpr (std::is_same_v<T, std::string>)
        {
            std::string strValue = query.value(0).toString().toStdString();
            return (strValue != defaultValue) ? std::optional<T>{ strValue } : std::nullopt;
        }
        else
        {
            T typedValue = query.value(0).value<T>();
            return (typedValue != defaultValue) ? std::optional<T>{ typedValue } : std::nullopt;
        }
    }
    else
    {
        // Lookup failed, convert the entire table
        if (!convertTableToUseFilenames(db, diagnostic))
        {
            return std::nullopt; // Conversion failed
        }

        // Retry the query
        query.clear();
        query.prepare("SELECT " + QString::fromStdString(columnName) + " FROM image_data WHERE absolute_path = :primaryKey");
        query.bindValue(":primaryKey", QString::fromStdString(filename));

        if (!query.exec())
        {
            diagnostic(LogLevel::Error, "Failed to execute query after table conversion: " + query.lastError().text().toStdString());
            return std::nullopt;
        }

        if (query.next())
        {
            // Value found after conversion
            if constexpr (std::is_same_v<T, std::string>)
            {
                std::string strValue = query.value(0).toString().toStdString();
                return (strValue != defaultValue) ? std::optional<T>{ strValue } : std::nullopt;
            }
            else
            {
                T typedValue = query.value(0).value<T>();
                return (typedValue != defaultValue) ? std::optional<T>{ typedValue } : std::nullopt;
            }
        }
        else
        {
            return std::nullopt; // Still not found
        }
    }
}

template <typename T>
bool setColumnValue(QSqlDatabase& db, const DiagnosticFunction& diagnostic, const std::string& primaryKey,
                    const std::string& column_name, const T& value)
{
    if (!db.isOpen())
    {
        diagnostic(LogLevel::Error, "Database is not open.");
        return false;
    }

    // Convert primaryKey to filename
    std::string filename = extractFilename(primaryKey);

    // Check if the row exists
    QSqlQuery checkQuery(db);
    checkQuery.prepare("SELECT COUNT(*) FROM image_data WHERE absolute_path = :primaryKey");
    checkQuery.bindValue(":primaryKey", QString::fromStdString(filename));
    if (!checkQuery.exec() || !checkQuery.next())
    {
        diagnostic(LogLevel::Error, "Failed to check for existing row.");
        return false;
    }

    bool rowExists = checkQuery.value(0).toInt() > 0;

    if (!rowExists)
    {
        // Convert the entire table
        if (!convertTableToUseFilenames(db, diagnostic))
        {
            return false; // Conversion failed
        }

        // Retry checking if the row exists
        checkQuery.clear();
        checkQuery.prepare("SELECT COUNT(*) FROM image_data WHERE absolute_path = :primaryKey");
        checkQuery.bindValue(":primaryKey", QString::fromStdString(filename));
        if (!checkQuery.exec() || !checkQuery.next())
        {
            diagnostic(LogLevel::Error, "Failed to check for existing row after table conversion.");
            return false;
        }

        rowExists = checkQuery.value(0).toInt() > 0;
    }

    QSqlQuery query(db);
    QString sql;

    if (rowExists)
    {
        // Update the existing row
        sql = QString("UPDATE image_data SET %1 = :value WHERE absolute_path = :primaryKey")
                  .arg(QString::fromStdString(column_name));
    }
    else
    {
        // Insert a new row
        sql = QString("INSERT INTO image_data (absolute_path, %1) VALUES (:primaryKey, :value)")
                  .arg(QString::fromStdString(column_name));
    }

    query.prepare(sql);
    query.bindValue(":primaryKey", QString::fromStdString(filename));

    if constexpr (std::is_same_v<T, std::string>)
    {
        query.bindValue(":value", QString::fromStdString(value));
    }
    else
    {
        query.bindValue(":value", QVariant::fromValue(value));
    }

    if (!query.exec())
    {
        diagnostic(LogLevel::Error, "Failed to execute query: " + query.lastError().text().toStdString() +
                                        "\nSource SQL: " + sql.toStdString());
        return false;
    }

    return true;
}

DatabaseManager::DatabaseManager(DiagnosticFunction diagnostic_function) : diagnostic_function_(diagnostic_function)
{
  db = createDb(":memory:");
}

void DatabaseManager::checkpoint()
{
  std::lock_guard lock(mutex_);

  if (db && db->databaseName() != ":memory:")
  {
    QSqlQuery query(*db);

    if (!query.exec("PRAGMA wal_checkpoint(FULL)"))
    {
      // Handle the error appropriately
      diagnostic_function_(LogLevel::Error, "Failed to execute query: " + query.lastQuery().toStdString());
      diagnostic_function_(LogLevel::Error, "Failed to checkpoint: " + query.lastError().text().toStdString());
    }
  }
}

void DatabaseManager::switchToInMemory()
{
  std::lock_guard lock(mutex_);
  if (db && db->databaseName() != ":memory:")
  {
    copyDataToNewDb(":memory:");
  }
}

void DatabaseManager::switchToFileBased(const std::string& filePath)
{
  std::lock_guard lock(mutex_);
  if (db && db->databaseName().toStdString() != filePath)
  {
    copyDataToNewDb(QString::fromStdString(filePath));
  }
}

std::string DatabaseManager::getLocation() const
{
  std::lock_guard lock(mutex_);
  if (db)
  {
    return db->databaseName().toStdString();
  }
  return "Uninitialized";
}

template <typename T, typename F>
auto transformOptional(const T& opt, const F& func)
{
  using return_optional = std::optional<typename std::invoke_result<F, typename T::value_type>::type>;

  if (opt.has_value())
  {
    return return_optional(func(opt.value()));
  }
  return return_optional{};
}

void DatabaseManager::setDecision(const std::string& image_path, DecisionType decision)
{
  std::lock_guard lock(mutex_);  // mutex is recursive
  setColumnValue(*db, diagnostic_function_, image_path, "decision", to_string(decision));
}

std::optional<DecisionType> DatabaseManager::getDecision(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  const auto opt_val = getColumnValue<std::string>(*db, diagnostic_function_, image_path, "decision");

  return transformOptional(opt_val, to_DecisionType);
}

void DatabaseManager::setExposureProgram(const std::string& image_path, ExposureProgram exposureProgram)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "exposure_program", to_string(exposureProgram));
}

void DatabaseManager::setMeteringMode(const std::string& image_path, MeteringMode meteringMode)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "metering_mode", to_string(meteringMode));
}

std::optional<ExposureProgram> DatabaseManager::getExposureProgram(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  const auto opt_val = getColumnValue<std::string>(*db, diagnostic_function_, image_path, "exposure_program");
  return transformOptional(opt_val, to_ExposureProgram);
}

std::optional<MeteringMode> DatabaseManager::getMeteringMode(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  const auto opt_val = getColumnValue<std::string>(*db, diagnostic_function_, image_path, "metering_mode");
  return transformOptional(opt_val, to_MeteringMode);
}

void DatabaseManager::setMake(const std::string& image_path, const std::string& make)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "make", make);
}

void DatabaseManager::setModel(const std::string& image_path, const std::string& model)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "model", model);
}

void DatabaseManager::setDateTime(const std::string& image_path, const std::string& dateTime)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "date_time", dateTime);
}

void DatabaseManager::setDateTimeOriginal(const std::string& image_path, const std::string& dateTimeOriginal)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "date_time_original", dateTimeOriginal);
}

void DatabaseManager::setSubSecTimeOriginal(const std::string& image_path, const std::string& subSecTimeOriginal)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "sub_sec_time_original", subSecTimeOriginal);
}

std::optional<std::string> DatabaseManager::getMake(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<std::string>(*db, diagnostic_function_, image_path, "make");
}

std::optional<std::string> DatabaseManager::getModel(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<std::string>(*db, diagnostic_function_, image_path, "model");
}

std::optional<std::string> DatabaseManager::getDateTime(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<std::string>(*db, diagnostic_function_, image_path, "date_time");
}

std::optional<std::string> DatabaseManager::getDateTimeOriginal(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<std::string>(*db, diagnostic_function_, image_path, "date_time_original");
}

std::optional<std::string> DatabaseManager::getSubSecTimeOriginal(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<std::string>(*db, diagnostic_function_, image_path, "sub_sec_time_original");
}

void DatabaseManager::setImageWidth(const std::string& image_path, int width)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "image_width", width);
}

void DatabaseManager::setImageHeight(const std::string& image_path, int height)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "image_height", height);
}

void DatabaseManager::setBitsPerSample(const std::string& image_path, int bitsPerSample)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "bits_per_sample", bitsPerSample);
}

void DatabaseManager::setISOSpeedRatings(const std::string& image_path, int isoSpeedRatings)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "iso_speed_ratings", isoSpeedRatings);
}

void DatabaseManager::setCreationMs(const std::string& image_path, std::size_t creation_ms)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "creation_ms", creation_ms);
}

std::optional<int> DatabaseManager::getImageWidth(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<int>(*db, diagnostic_function_, image_path, "image_width");
}

std::optional<int> DatabaseManager::getImageHeight(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<int>(*db, diagnostic_function_, image_path, "image_height");
}

std::optional<int> DatabaseManager::getBitsPerSample(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<int>(*db, diagnostic_function_, image_path, "bits_per_sample");
}

std::optional<int> DatabaseManager::getISOSpeedRatings(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<int>(*db, diagnostic_function_, image_path, "iso_speed_ratings");
}

std::optional<std::size_t> DatabaseManager::getCreationMs(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<std::size_t>(*db, diagnostic_function_, image_path, "creation_ms");
}

std::vector<std::string> DatabaseManager::getDeleteDecisionFilenames()
{
  std::lock_guard lock(mutex_);

  const auto decision_text = to_string(DecisionType::Delete);

  std::vector<std::string> paths;

  // Prepare the SQL query using a placeholder for the decision text
  QSqlQuery query(*db);
  query.prepare("SELECT absolute_path FROM image_data WHERE decision = :decision");

  // Bind the decision_text to the placeholder
  query.bindValue(":decision", QString::fromStdString(decision_text));

  // Execute the query
  if (query.exec())
  {
    while (query.next())
    {
      // Extract the absolute_path and add it to the vector
      QString path = query.value(0).toString();
      paths.push_back(path.toStdString());
    }
  }
  else
  {
    // Handle query execution error
    std::cerr << "Query failed: " << query.lastError().text().toStdString() << std::endl;
  }

  return paths;
}

void DatabaseManager::removeRowsIfAbsolutePath(std::function<bool(const std::string&)> condition)
{
  std::lock_guard lock(mutex_);

  QSqlQuery query(*db);

  // Select all rows from the table
  if (!query.exec("SELECT absolute_path FROM image_data"))
  {
    // Handle error
    std::cerr << "Query failed: " << query.lastError().text().toStdString() << std::endl;
    return;
  }

  while (query.next())
  {
    QString absolutePath = query.value(0).toString();
    std::string absolutePathStd = absolutePath.toStdString();

    // Check the condition
    if (!condition(absolutePathStd))
    {
      // Delete the row if the condition is false
      QSqlQuery deleteQuery;
      deleteQuery.prepare("DELETE FROM image_data WHERE absolute_path = :absolute_path");
      deleteQuery.bindValue(":absolute_path", absolutePath);

      if (!deleteQuery.exec())
      {
        // Handle error in delete operation
        std::cerr << "Delete failed: " << deleteQuery.lastError().text().toStdString() << std::endl;
      }
    }
  }
}

bool DatabaseManager::removeRowForPath(const std::string& path)
{
  std::lock_guard lock(mutex_);
  QSqlQuery query(*db);

  query.prepare("DELETE FROM image_data WHERE absolute_path = :path");
  query.bindValue(":path", QString::fromStdString(path));

  if (!query.exec())
  {
    diagnostic_function_(LogLevel::Error, "Failed to execute query: " + query.lastQuery().toStdString());
    diagnostic_function_(LogLevel::Error, "Failed to delete row: " + query.lastError().text().toStdString());
    return false;
  }

  return true;
}

void DatabaseManager::setFNumber(const std::string& image_path, double fNumber)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "f_number", fNumber);
}

void DatabaseManager::setExposureTime(const std::string& image_path, double shutterSpeedValue)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "exposure_time", shutterSpeedValue);
}

void DatabaseManager::setApertureValue(const std::string& image_path, double apertureValue)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "aperture_value", apertureValue);
}

void DatabaseManager::setBrightnessValue(const std::string& image_path, double brightnessValue)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "brightness_value", brightnessValue);
}

void DatabaseManager::setExposureBiasValue(const std::string& image_path, double value)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "exposure_bias_value", value);
}

void DatabaseManager::setSubjectDistance(const std::string& image_path, double value)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "subject_distance", value);
}

void DatabaseManager::setFocalLength(const std::string& image_path, double value)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "focal_length", value);
}

void DatabaseManager::setOrientation(const std::string& image_path, int value)
{
  std::lock_guard lock(mutex_);
  setColumnValue(*db, diagnostic_function_, image_path, "orientation", value);
}

std::optional<double> DatabaseManager::getFNumber(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<double>(*db, diagnostic_function_, image_path, "f_number");
}

std::optional<double> DatabaseManager::getShutterSpeedValue(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<double>(*db, diagnostic_function_, image_path, "exposure_time");
}

std::optional<double> DatabaseManager::getApertureValue(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<double>(*db, diagnostic_function_, image_path, "aperture_value");
}

std::optional<double> DatabaseManager::getBrightnessValue(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<double>(*db, diagnostic_function_, image_path, "brightness_value");
}

std::optional<double> DatabaseManager::getExposureBiasValue(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<double>(*db, diagnostic_function_, image_path, "exposure_bias_value");
}

std::optional<double> DatabaseManager::getSubjectDistance(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<double>(*db, diagnostic_function_, image_path, "subject_distance");
}

std::optional<double> DatabaseManager::getFocalLength(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<double>(*db, diagnostic_function_, image_path, "focal_length");
}

std::optional<int> DatabaseManager::getOrientation(const std::string& image_path)
{
  std::lock_guard lock(mutex_);
  return getColumnValue<int>(*db, diagnostic_function_, image_path, "orientation");
}

std::array<std::size_t, 5> DatabaseManager::getDecisionCounts()
{
  std::lock_guard lock(mutex_);
  QSqlQuery query(*db);

  std::array<std::size_t, 5> counts;
  counts[0] = 0;
  counts[1] = 0;
  counts[2] = 0;
  counts[3] = 0;
  counts[4] = 0;

  // SQL query to count different DecisionType values
  std::string countQuery = "SELECT decision, COUNT(decision) FROM image_data GROUP BY decision";

  if (!query.exec(QString::fromStdString(countQuery)))
  {
    // Handle the error appropriately
    diagnostic_function_(LogLevel::Error, "Failed to execute query: " + query.lastQuery().toStdString());
  }
  else
  {
    while (query.next())
    {
      std::string decisionType = query.value(0).toString().toStdString();

      switch (to_DecisionType(decisionType))
      {
        case DecisionType::Unknown:
          counts[0] += query.value(1).toULongLong();
          break;
        case DecisionType::Delete:
          counts[1] += query.value(1).toULongLong();
          break;
        case DecisionType::Unclassified:
          counts[2] += query.value(1).toULongLong();
          break;
      case DecisionType::Keep:
        counts[3] += query.value(1).toULongLong();
        break;
      case DecisionType::SuperKeep:
        counts[4] += query.value(1).toULongLong();
        break;
      }
    }
  }
  return counts;
}

void DatabaseManager::close()
{
  std::lock_guard lock(mutex_);

  db->commit();
  db->close();
  db.reset();
  QSqlDatabase::removeDatabase(QSqlDatabase::defaultConnection);
  db = createDb(":memory:");
}

void DatabaseManager::initializeDb(QSqlDatabase& target)
{
  std::string defaultDecision = to_string(DecisionType::Unknown);
  std::string defaultExposureProgram = to_string(ExposureProgram::NotDefined);
  std::string defaultMeteringMode = to_string(MeteringMode::Unknown);

  std::string sqlCommand = "CREATE TABLE IF NOT EXISTS image_data ("
                           "absolute_path TEXT PRIMARY KEY, "
                           "decision TEXT DEFAULT '" +
                           defaultDecision +
                           "', "
                           "image_width INTEGER DEFAULT 0, "
                           "image_height INTEGER DEFAULT 0, "
                           "make TEXT DEFAULT '', "
                           "model TEXT DEFAULT '', "
                           "bits_per_sample INTEGER DEFAULT 0, "
                           "date_time TEXT DEFAULT '', "
                           "date_time_original TEXT DEFAULT '', "
                           "sub_sec_time_original TEXT DEFAULT '', "
                           "f_number REAL DEFAULT 0.0, "
                           "exposure_program TEXT DEFAULT '" +
                           defaultExposureProgram +
                           "', "
                           "iso_speed_ratings INTEGER DEFAULT 0, "
                           "exposure_time REAL DEFAULT 0.0, "
                           "aperture_value REAL DEFAULT 0.0, "
                           "brightness_value REAL DEFAULT 0.0, "
                           "exposure_bias_value REAL DEFAULT 0.0, "
                           "subject_distance REAL DEFAULT 0.0, "
                           "focal_length REAL DEFAULT 0.0, "
                           "orientation INTEGER DEFAULT 0.0, "
                           "metering_mode TEXT DEFAULT '" +
                           defaultMeteringMode +
                           "', "
                           "creation_ms BIGINT DEFAULT 0)";

  QSqlQuery query(target);
  if (!query.exec(QString::fromStdString(sqlCommand)))
  {
    // Handle the error appropriately
    diagnostic_function_(LogLevel::Error, "Failed to execute query: " + query.lastQuery().toStdString());
    diagnostic_function_(LogLevel::Error, "Failed to create table: " + query.lastError().text().toStdString());
  }

  if (!query.exec("PRAGMA synchronous = NORMAL"))
  {
    diagnostic_function_(LogLevel::Error, "Failed to set PRAGMA synchronous = NORMAL");
  }
  if (!query.exec("PRAGMA journal_mode = WAL"))
  {
    diagnostic_function_(LogLevel::Error, "Failed to set PRAGMA journal_mode = WA");
  }
}

std::shared_ptr<QSqlDatabase> DatabaseManager::createDb(const QString& db_name, const QString& connection)
{
  auto database = [&connection]()
  {
    if (connection.isEmpty())
    {
      return std::make_shared<QSqlDatabase>(QSqlDatabase::addDatabase("QSQLITE"));
    }
    return std::make_shared<QSqlDatabase>(QSqlDatabase::addDatabase("QSQLITE", connection));
  }();

  database->setDatabaseName(db_name);
  if (!database->open())
  {
    diagnostic_function_(LogLevel::Error, "Unable to open database: " + db_name.toStdString());
  }
  else
  {
    initializeDb(*database);
  }

  return database;
}

void DatabaseManager::copyDataToNewDb(const QString& new_db_name)
{
  auto temp_db = createDb(":memory:", "temp_db");

  if (!temp_db->open())
  {
    diagnostic_function_(LogLevel::Error, "Unable to open temp database.");
    return;
  }

  copyDbContents(*db, *temp_db);

  db->commit();
  db->close();
  db.reset();
  QSqlDatabase::removeDatabase(QSqlDatabase::defaultConnection);
  db = createDb(new_db_name);

  if (!db->open())
  {
    diagnostic_function_(LogLevel::Error, "Unable to open new database.");
    return;
  }

  copyDbContents(*temp_db, *db);

  temp_db->commit();
  temp_db->close();
  temp_db.reset();

  QSqlDatabase::removeDatabase("temp_db");
}

void DatabaseManager::copyDbContents(QSqlDatabase& source_db, QSqlDatabase& target_db)
{
  initializeDb(target_db);

  if (!target_db.transaction())
  {
    diagnostic_function_(LogLevel::Error, "Failed to start transaction on target database.");
    return;
  }

  QSqlQuery sourceQuery(source_db);
  if (!sourceQuery.exec("SELECT * FROM image_data"))
  {
    target_db.rollback();  // Rollback the transaction

    diagnostic_function_(LogLevel::Error, "Failed to execute query on source database.");
    diagnostic_function_(LogLevel::Error, describeDatabase(source_db));
    return;
  }

  QStringList columnNames;
  QStringList bindValues;
  for (int i = 0; i < sourceQuery.record().count(); ++i)
  {
    QString fieldName = sourceQuery.record().fieldName(i);
    columnNames << fieldName;
    bindValues << ":" + fieldName;
  }

  QString insertSql =
      QString("INSERT OR REPLACE INTO image_data (%1) VALUES (%2)").arg(columnNames.join(", "), bindValues.join(", "));

  while (sourceQuery.next())
  {
    QSqlQuery insertQuery(target_db);
    insertQuery.prepare(insertSql);

    for (int i = 0; i < sourceQuery.record().count(); ++i)
    {
      insertQuery.bindValue(":" + sourceQuery.record().fieldName(i), sourceQuery.value(i));
    }

    if (!insertQuery.exec())
    {
      QString err = "Failed to insert data: " + insertQuery.lastError().text() + "\n";
      diagnostic_function_(LogLevel::Error, err.toStdString());
      target_db.rollback();
      return;
    }
  }

  if (!target_db.commit())
  {
    target_db.rollback();  // Rollback the transaction
    diagnostic_function_(LogLevel::Error, "Failed to commit transaction on target database.");
    return;
  }
}

#include <cassert>

bool DatabaseManager::unitTest()
{
  DatabaseManager dbManager(makeDefaultDiagnosticFunction());

  // Test setting and retrieving a decision in in-memory mode
  std::string testImagePath1 = "/path/to/image1.jpg";
  dbManager.setDecision(testImagePath1, DecisionType::Keep);

  auto decision1 = dbManager.getDecision(testImagePath1);
  assert(decision1.has_value());
  assert(decision1.has_value() && decision1.value() == DecisionType::Keep);

  // Test for an unclassified image (one that has no decision yet)
  std::string unclassifiedImagePath = "/path/to/unclassified.jpg";
  auto unclassifiedDecision = dbManager.getDecision(unclassifiedImagePath);
  assert(!unclassifiedDecision.has_value() || unclassifiedDecision.value() == DecisionType::Unclassified);

  // Switch to file-based mode and test persistence
  // dbManager.switchToFileBased("D:/programming/my_test.db");
  dbManager.switchToFileBased("test.db");
  auto decision1AfterFileBased = dbManager.getDecision(testImagePath1);
  assert(decision1AfterFileBased.has_value() && decision1AfterFileBased.value() == DecisionType::Keep);

  // Test setting and retrieving a new decision in file-based mode
  std::string testImagePath2 = "/path/to/image2.jpg";
  dbManager.setDecision(testImagePath2, DecisionType::Delete);
  auto decision2 = dbManager.getDecision(testImagePath2);
  assert(decision2.has_value() && decision2.value() == DecisionType::Delete);

  // Switch back to in-memory and ensure the first decision is not present
  dbManager.switchToInMemory();
  auto decision1InMemory = dbManager.getDecision(testImagePath1);
  assert(decision1InMemory.has_value() && decision1InMemory.value() == DecisionType::Keep);

  // Test saving and retrieving creation time
  std::size_t testCreationTime1 = 123456789;
  dbManager.setCreationMs(testImagePath1, testCreationTime1);
  auto creationTime1 = dbManager.getCreationMs(testImagePath1);
  assert(creationTime1.has_value() && creationTime1.value() == testCreationTime1);

  // Test if creation time is preserved when updating decision
  dbManager.setDecision(testImagePath1, DecisionType::Delete);
  auto decision1Updated = dbManager.getDecision(testImagePath1);
  auto creationTime1AfterUpdate = dbManager.getCreationMs(testImagePath1);
  assert(decision1Updated.has_value() && decision1Updated.value() == DecisionType::Delete);
  assert(creationTime1AfterUpdate.has_value() && creationTime1AfterUpdate.value() == testCreationTime1);

  // Test default creation time for a new image
  std::string testImagePath3 = "/path/to/image3.jpg";
  dbManager.setDecision(testImagePath3, DecisionType::Keep);
  auto creationTime3 = dbManager.getCreationMs(testImagePath3);
  assert(!creationTime3.has_value() || creationTime3.value() == 0);

  std::cout << "All DB tests passed successfully.\n";

  return true;
}
