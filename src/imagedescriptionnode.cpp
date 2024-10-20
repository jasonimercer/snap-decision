#include "snapdecision/imagedescriptionnode.h"

#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QString>
#include <fstream>
#include <iostream>

#include "snapdecision/utils.h"
#include "snapdecision/TinyEXIF.h"

ImageDescriptionNode::ImageDescriptionNode(NodeType type) : node_type(type)
{
}

static bool canOpenImage(const QString& file_path)
{
  QImageReader reader(file_path);
  return reader.canRead();
}

static std::size_t creationDateInMsSinceEpoch(const QFileInfo& file_info)
{
  QDateTime creation_time = file_info.birthTime();  // or fileInfo.created();
  qint64 ms_since_epoch = creation_time.toMSecsSinceEpoch();
  return static_cast<std::size_t>(ms_since_epoch);
}

std::optional<TinyEXIF::EXIFInfo> readEXIFData(const std::string& image_path)
{
  std::ifstream file(image_path, std::ifstream::in | std::ifstream::binary);
  if (!file)
  {
    return std::nullopt;
  }

  TinyEXIF::EXIFInfo exif_info(file);
  if (exif_info.Fields)
  {
    return exif_info;
  }
  return std::nullopt;
}

static void populateNodeFromDatabase(const ImageDescriptionNode::Ptr& n, const DatabaseManager::Ptr& db)
{
  const auto img = n->full_path;

  n->decision = db->getDecision(img).value_or(DecisionType::Unclassified);
  n->exposure_program = db->getExposureProgram(img).value_or(ExposureProgram::NotDefined);
  n->metering_mode = db->getMeteringMode(img).value_or(MeteringMode::Unknown);
  n->time_ms = db->getCreationMs(img).value_or(0);
  n->make = db->getMake(img).value_or("");
  n->model = db->getModel(img).value_or("");
  n->width = db->getImageWidth(img).value_or(0);
  n->height = db->getImageHeight(img).value_or(0);
  n->iso = db->getISOSpeedRatings(img).value_or(0);
  n->f_number = db->getFNumber(img).value_or(0);

  n->shutter_speed = db->getShutterSpeedValue(img).value_or(0);
  n->exposure_bias = db->getExposureBiasValue(img).value_or(0);
  n->focal_length = db->getFocalLength(img).value_or(0);
  n->orientation = db->getOrientation(img).value_or(0);

  n->ready = true;  // no more writes from the loading threads
}

static void doEXIFLookup(const ImageDescriptionNode::Ptr& node, const DatabaseManager::Ptr& database_manager)
{
  const auto image_path = node->full_path;

  const auto exif_opt = readEXIFData(image_path);

  if (!exif_opt.has_value())
  {
    database_manager->setCreationMs(image_path, node->time_ms);
    populateNodeFromDatabase(node, database_manager);
    return;
  }

  const auto exif = exif_opt.value();

  database_manager->setMake(image_path, exif.Make);
  database_manager->setModel(image_path, exif.Model);
  database_manager->setDateTime(image_path, exif.DateTime);
  database_manager->setDateTimeOriginal(image_path, exif.DateTimeOriginal);
  database_manager->setSubSecTimeOriginal(image_path, exif.SubSecTimeOriginal);
  database_manager->setImageWidth(image_path, exif.ImageWidth);
  database_manager->setImageHeight(image_path, exif.ImageHeight);
  database_manager->setBitsPerSample(image_path, exif.BitsPerSample);
  database_manager->setISOSpeedRatings(image_path, exif.ISOSpeedRatings);
  database_manager->setFNumber(image_path, exif.FNumber);
  database_manager->setExposureTime(image_path, exif.ExposureTime);
  database_manager->setApertureValue(image_path, exif.ApertureValue);
  database_manager->setBrightnessValue(image_path, exif.BrightnessValue);
  database_manager->setExposureBiasValue(image_path, exif.ExposureBiasValue);
  database_manager->setSubjectDistance(image_path, exif.SubjectDistance);
  database_manager->setFocalLength(image_path, exif.FocalLength);
  database_manager->setOrientation(image_path, exif.Orientation);

  database_manager->setExposureProgram(image_path, static_cast<ExposureProgram>(exif.ExposureProgram));
  database_manager->setMeteringMode(image_path, static_cast<MeteringMode>(exif.MeteringMode));

  std::size_t creation_ms = node->time_ms;

  std::string date = exif.DateTimeOriginal;

  if (date.empty())
  {
    date = exif.DateTime;
  }

  database_manager->setCreationMs(image_path, creation_ms);
  if (!date.empty())
  {
    QString exifTimestamp = QString::fromStdString(date);
    QDateTime dateTime = QDateTime::fromString(exifTimestamp, "yyyy:MM:dd hh:mm:ss");
    if (dateTime.isValid())
    {
      creation_ms = static_cast<std::size_t>(dateTime.toMSecsSinceEpoch());

      if (const auto n = toInt(exif.SubSecTimeOriginal); n.has_value())
      {
        creation_ms += n.value() * 10;
      }
    }
  }
  database_manager->setCreationMs(image_path, creation_ms);

  populateNodeFromDatabase(node, database_manager);
}

static void scheduleEXIFLookup(const ImageDescriptionNode::Ptr& node, const TaskQueue::Ptr& task_queue,
                               const DatabaseManager::Ptr& database_manager, const SimpleFunction& on_finish)
{
  ImageDescriptionNode::WeakPtr weak_node = node;
  DatabaseManager::WeakPtr weak_db = database_manager;

  const auto worker_function = [weak_node, weak_db, on_finish](double& progress)
  {
    progress = 0.0;

    if (const auto& node = weak_node.lock())
    {
      if (const auto& db = weak_db.lock())
      {
        doEXIFLookup(node, db);
        populateNodeFromDatabase(node, db);
      }
    }
    on_finish();
    progress = 1.0;
  };

  task_queue->submit(worker_function);
}

static void scheduleDbLookup(const ImageDescriptionNode::Ptr& node, const TaskQueue::Ptr& task_queue,
                             const DatabaseManager::Ptr& database_manager, const SimpleFunction& on_finish)
{
  ImageDescriptionNode::WeakPtr weak_node = node;
  DatabaseManager::WeakPtr weak_db = database_manager;

  const auto worker_function = [weak_node, weak_db, on_finish](double& progress)
  {
    progress = 0.0;

    if (const auto& node = weak_node.lock())
    {
      if (const auto& db = weak_db.lock())
      {
        populateNodeFromDatabase(node, db);
      }
    }
    on_finish();
    progress = 1.0;
  };

  task_queue->submit(worker_function);
}

static void populateExifInfo(const ImageDescriptionNode::Ptr& node, const TaskQueue::Ptr& task_queue,
                             const DatabaseManager::Ptr& database_manager, const SimpleFunction& on_finish)
{
  const auto db_time = database_manager->getCreationMs(node->full_path);

  if (db_time)  // then we have data on-hand
  {
    scheduleDbLookup(node, task_queue, database_manager, on_finish);
    return;
  }

  scheduleEXIFLookup(node, task_queue, database_manager, on_finish);
}

std::string getExtension(const std::string& filePath)
{
  QString q_file_path = QString::fromStdString(filePath);
  QFileInfo file_info(q_file_path);
  QString extension = file_info.suffix();
  return extension.toStdString();
}

bool isMostlyUpperCase(const std::string& str)
{
  int counter = 0;
  for (char ch : str)
  {
    counter += std::isupper(ch) ? 1 : std::islower(ch) ? -1 : 0;
  }
  return counter > 0;
}

std::string findRawImage(const std::string& file_path)
{
  QString q_file_path = QString::fromStdString(file_path);
  QFileInfo file_info(q_file_path);
  QDir directory = file_info.dir();
  QString base_name = file_info.completeBaseName();

  const auto ext = getExtension(file_path);

  const bool prefer_upper = isMostlyUpperCase(ext);

  const auto getCandidates = [&](const auto& ext)
  {
    QString potential_file_lower = directory.filePath(base_name + "." + ext.toLower());
    QString potential_file_upper = directory.filePath(base_name + "." + ext.toUpper());

    QStringList files_to_check;
    if (prefer_upper)
    {
      files_to_check << potential_file_upper << potential_file_lower;
    }
    else
    {
      files_to_check << potential_file_lower << potential_file_upper;
    }
    return files_to_check;
  };

  // List of common raw file extensions
  QStringList raw_extensions = { "raw", "cr3", "cr2", "nef", "arw", "raf", "orf", "dng", "rw2", "pef", "srw" };

  for (const QString& ext : raw_extensions)
  {
    for (const auto& file : getCandidates(ext))
    {
      if (QFileInfo::exists(file))
      {
        return file.toStdString();
      }
    }
  }

  return "";
}

ImageDescriptionNode::Ptr buildImageDescriptionNode(const std::string& filename, const ImageCache::Ptr& image_cache,
                                                    const TaskQueue::Ptr& task_queue,
                                                    const DatabaseManager::Ptr& database_manager,
                                                    const SimpleFunction& on_finish)
{
  const auto q_filename = QString::fromStdString(filename);

  if (!canOpenImage(q_filename))
  {
    on_finish();
    return nullptr;
  }

  auto node = std::make_shared<ImageDescriptionNode>();
  QFileInfo file_info(q_filename);

  node->filename = extractFilename(q_filename.toStdString());

  node->full_path = file_info.absoluteFilePath().toStdString();
  node->full_raw_path = findRawImage(node->full_path);
  node->raw_path_extension = getExtension(node->full_raw_path);

  node->image_cache_handle_ = image_cache->getHandle(node->full_path);

  const auto parent_path = file_info.absolutePath().toStdString();

  const auto db_path = parent_path + "/.image_database.db";

  if (database_manager->getLocation() != db_path)
  {
    database_manager->switchToFileBased(db_path);

    setFileHidden(QString::fromStdString(db_path));
    setFileHidden(QString::fromStdString(db_path + "-shm"));
    setFileHidden(QString::fromStdString(db_path + "-wal"));
  }

  node->decision = database_manager->getDecision(node->full_path).value_or(DecisionType::Unclassified);

  node->time_ms = creationDateInMsSinceEpoch(file_info);

  populateExifInfo(node, task_queue, database_manager, on_finish);

  return node;
}
