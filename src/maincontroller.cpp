#include "snapdecision/maincontroller.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QObject>
#include <cmath>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <QImageReader>
#include <QStringList>

#include "snapdecision/enums.h"
#include "snapdecision/mainwindow.h"
#include "snapdecision/types.h"
#include "snapdecision/utils.h"
#include "ui_mainwindow.h"

MainController::MainController(MainModel* model, MainWindow* view, Settings* settings)
  : QObject(), model_(model), view_(view), settings_(settings)
{
  setupConnections();

  view->set_settings_ = [this](const Settings& s) { this->setSettings(s); };
  view->get_settings_ = [this]() { return *settings_; };

  model_->image_group_->get_settings_ = [this]() { return *settings_; };

  view->ui->treeView->setModel(model->image_tree_model_.get());

  view->ui->treeView->expandAll();
  view->ui->treeView->setRootIsDecorated(false);
  view->ui->treeView->setItemsExpandable(false);
  view->ui->treeView->setStyleSheet("QTreeView::branch {  border-image: url(none.png); }\n"
                                    "QTreeView::item:disabled { color: black; }");
  view->initialize();

  QSettings qsettings("JasonIMercer", "SnapDecision");
  loadResource(qsettings.value("lastLoadResource").toString());

  updateDecisionCounts();

  setSettings(*settings_);
}

void MainController::treeBuildComplete()
{
  const auto& image_group_ = model_->image_group_;
  model_->image_tree_model_->setImageRoot(image_group_->tree_root_);
  view_->ui->treeView->expandAll();

  QFileInfo fileInfo(resource_);

  if (image_group_ && !image_group_->flat_list_.empty())
  {
    if (fileInfo.isDir())
    {
      focusOnNode(QString::fromStdString(image_group_->flat_list_.front()->full_path));
    }
    if (fileInfo.isFile())
    {
      focusOnNode(fileInfo.absoluteFilePath());
    }
  }
  updateDecisionCounts();
}


QStringList getImageFileExtensions() {
    QStringList extensions;
    foreach (QByteArray format, QImageReader::supportedImageFormats()) {
        extensions.append("*." + format);
    }
    return extensions;
}

// Usage

void MainController::loadResource(const QString& path)
{
  if (path.isEmpty())
  {
    return;
  }

  QDir directory;
  QFileInfo fileInfo(path);

  resource_ = path;

  if (fileInfo.isDir())
  {
    directory = QDir(path);
  }
  else if (fileInfo.isFile())
  {
    directory = fileInfo.dir();
  }
  else
  {
    return;
  }

  QSettings settings("JasonIMercer", "SnapDecision");
  settings.setValue("lastLoadResource", path);

  std::vector<std::string> image_filenames;

  QStringList files = directory.entryList(getImageFileExtensions());



  for (const QString& file : files)
  {
    image_filenames.push_back(directory.absoluteFilePath(file).toStdString());
  }

  model_->image_group_->loadFiles(image_filenames, model_->image_cache_, model_->task_queue_, model_->database_manager_,
                                  model_->diagnostic_function_);
}

void MainController::setupConnections()
{
  connect(view_, &MainWindow::resourceDropped, this, &MainController::loadResource);
  connect(view_, &MainWindow::imageFocused, this, &MainController::focusOnNode);
  connect(view_->ui->action_Move_Delete, &QAction::triggered, this, &MainController::moveDeleteMarked);
  connect(view_->ui->action_Quit, &QAction::triggered, this, []() { QCoreApplication::quit(); });

  connect(model_->image_group_.get(), SIGNAL(treeBuildComplete()), this, SLOT(treeBuildComplete()));

  connect(&model_->image_cache_->signal_emitter, SIGNAL(memoryUsageChanged(CurrentMaxCount)), this,
          SLOT(memoryUsageChanged(CurrentMaxCount)));

  auto key_func = [this](QKeyEvent* event) { this->keyPressed(event); };

  connect(view_->ui->actionTool1, &QAction::triggered, this, [this]() { executeTool(0); });
  connect(view_->ui->actionTool2, &QAction::triggered, this, [this]() { executeTool(1); });
  connect(view_->ui->actionTool3, &QAction::triggered, this, [this]() { executeTool(2); });
  connect(view_->ui->actionTool4, &QAction::triggered, this, [this]() { executeTool(3); });

  view_->ui->graphicsView->key_event_function_ = key_func;
  view_->ui->treeView->key_event_function_ = key_func;
}

static std::optional<std::string> shutterSpeedToString(double speed)
{
  if (speed <= 0)
  {
    return std::nullopt;
  }
  std::ostringstream oss;
  if (speed < 1.0)
  {
    // Convert to a fraction
    int denominator = std::round(1.0 / speed);
    oss << "1/" << denominator << " s";
  }
  else
  {
    // Use decimal representation
    oss << speed << " s";
  }
  return oss.str();
}

std::string exposureCompToString(double value)
{
  std::ostringstream oss;

  // Determine the sign
  std::string sign = (value >= 0) ? "+ " : "- ";

  // Work with absolute value for simplicity
  value = std::abs(value);

  // Check if the value is close to an integer
  double integerPart;
  double fractionalPart = modf(value, &integerPart);
  if (fractionalPart < 0.01 || fractionalPart > 0.99)
  {
    // Close to an integer
    oss << sign << static_cast<int>(std::round(value));
  }
  else
  {
    // Handle fractional parts (quarters, thirds, halves)
    oss << sign << static_cast<int>(integerPart) << ' ';
    if (std::abs(fractionalPart - 0.5) < 0.01)
    {
      oss << "1/2";
    }
    else if (std::abs(fractionalPart - 0.333) < 0.01 || std::abs(fractionalPart - 0.667) < 0.01)
    {
      oss << std::round(fractionalPart * 3) << "/3";
    }
    else if (std::abs(fractionalPart - 0.25) < 0.01 || std::abs(fractionalPart - 0.75) < 0.01)
    {
      oss << std::round(fractionalPart * 4) << "/4";
    }
    else
    {
      // For non-standard values, just round to nearest fraction
      int nearestFraction = std::round(fractionalPart * 4);
      oss << nearestFraction << "/4";
    }
  }

  return oss.str();
}

void MainController::focusOnNode(const QString& image_name)
{
  auto treeView = view_->ui->treeView;

  auto* model = static_cast<ImageTreeModel*>(treeView->model());

  QModelIndex index = model->indexForImage(image_name);
  if (index.isValid())
  {
    treeView->setCurrentIndex(index);
    treeView->scrollTo(index);
  }
  else
  {
    qDebug() << "Failed to lookup " << image_name;
  }
  current_image_full_path_ = image_name;

  auto node = model->nodeFromIndex(index);
  if (node)
  {
    view_->ui->graphicsView->setImage(node->image_cache_handle_->blockingImage(), image_name.toStdString(),
                                      node->orientation);

    view_->ui->graphicsView->showDecision(node->decision);

    const auto os = [](const std::string& txt) -> std::optional<std::string>
    {
      if (txt.empty())
      {
        return std::nullopt;
      }
      return txt;
    };

    std::string zoom;
    if (node->focal_length > 0)
    {
      zoom = std::to_string(static_cast<int>(node->focal_length)) + "mm";
    }

    std::string ec = exposureCompToString(node->exposure_bias);

    std::string iso;
    if (node->iso > 0)
    {
      iso = std::to_string(node->iso);
    }

    std::string f;
    if (node->f_number > 0)
    {
      std::ostringstream oss;
      oss << "F " << node->f_number;
      f = oss.str();
    }

    std::string time;
    if (node->time_ms > 0)
    {
      time = timeToStringPrecise(node->time_ms).toStdString();
    }

    view_->setImageProperties(os(time), os(node->exposureProgramString()), os(zoom),
                              shutterSpeedToString(node->shutter_speed), os(f), os(ec), os(iso));
  }
  else
  {
    const auto none = std::nullopt;
    view_->setImageProperties(none, none, none, none, none, none, none);
  }

  const auto& image_group_ = model_->image_group_;

  if (const auto& current = image_group_->getIndexClosestTo(current_focus_index_, node); current)
  {
    previous_focus_index_ = current_focus_index_;
    current_focus_index_ = current.value();

    const auto probably_next = predictNextNode(1);

    if (const auto next_node = image_group_->getNodeAtIndex(probably_next); next_node)
    {
      next_node->image_cache_handle_->scheduleImage();
    }
  }
}

void MainController::memoryUsageChanged(CurrentMaxCount cmc)
{
  if (settings_->show_debug_console_)
  {
    auto& [used, max, count] = cmc;

    used /= 1000000;
    max /= 1000000;

    auto msg = QString("Image Cache: %3 images (%1 MB / %2 MB)").arg(used).arg(max).arg(count);

    view_->statusBar()->showMessage(msg, 5000);
  }
}

void MainController::keyPressed(QKeyEvent* event)
{
  if (!event)
  {
    return;
  }

  const QKeySequence input((int)event->modifiers() | event->key());

  const auto isMatch = [&](const auto& seq) { return seq.matches(input) == QKeySequence::ExactMatch; };

  int pan_amount = 50;
  if (isMatch(settings_->key_pan_up_))
  {
    view_->ui->graphicsView->panView(0, pan_amount);
    event->accept();
    return;
  }

  if (isMatch(settings_->key_pan_down_))
  {
    view_->ui->graphicsView->panView(0, -pan_amount);
    event->accept();
    return;
  }

  if (isMatch(settings_->key_pan_left_))
  {
    view_->ui->graphicsView->panView(pan_amount, 0);
    event->accept();
    return;
  }

  if (isMatch(settings_->key_pan_right_))
  {
    view_->ui->graphicsView->panView(-pan_amount, 0);
    event->accept();
    return;
  }

  if (isMatch(settings_->key_vote_up_))
  {
    vote(1);
    event->accept();
    return;
  }

  if (isMatch(settings_->key_vote_down_))
  {
    vote(-1);
    event->accept();
    return;
  }

  if (isMatch(settings_->key_prev_image_))
  {
    view_->ui->treeView->navigate(-1);
    event->accept();
    return;
  }

  if (isMatch(settings_->key_next_image_))
  {
    view_->ui->treeView->navigate(1);
    event->accept();
    return;
  }

  if (isMatch(settings_->key_reset_view_))
  {
    view_->ui->graphicsView->fitPixmapInView();
    event->accept();
    return;
  }

  if (isMatch(settings_->key_delete_and_next_))
  {
    vote(-2);
    view_->ui->treeView->navigate(1);
    event->accept();
    return;
  }

  if (isMatch(settings_->key_keep_and_next_))
  {
    vote(2);
    view_->ui->treeView->navigate(1);
    event->accept();
    return;
  }

  if (isMatch(settings_->key_zoom_1_))
  {
    view_->ui->graphicsView->setViewLevel(1);
    event->accept();
    return;
  }

  if (isMatch(settings_->key_zoom_2_))
  {
    view_->ui->graphicsView->setViewLevel(2);
    event->accept();
    return;
  }

  if (isMatch(settings_->key_zoom_3_))
  {
    view_->ui->graphicsView->setViewLevel(3);
    event->accept();
    return;
  }

  if (isMatch(settings_->key_zoom_4_))
  {
    view_->ui->graphicsView->setViewLevel(4);
    event->accept();
    return;
  }

  if (isMatch(settings_->key_zoom_5_))
  {
    view_->ui->graphicsView->setViewLevel(5);
    event->accept();
    return;
  }
}

void MainController::setSettings(const Settings& s)
{
  const bool regen_tree = settings_->burst_threshold_ms_ != s.burst_threshold_ms_ ||
                          settings_->location_theshold_ms_ != s.location_theshold_ms_;

  *settings_ = s;

  model_->image_cache_->setMaxMemoryUsage(settings_->cache_memory_mb_ * 1000000);
  model_->image_cache_->manageCache();

  if (regen_tree)
  {
    resource_ = current_image_full_path_;
    model_->image_group_->onFileListLoadComplete();
  }

  view_->ui->txtDebugOutput->setVisible(settings_->show_debug_console_);

  view_->ui->action_Preferences->setShortcut(settings_->key_preferences_);
  view_->ui->action_Move_Delete->setShortcut(settings_->key_delete_marked_);

  const auto setTool = [this](auto* action, std::size_t idx)
  {
    const auto& t = settings_->tool[idx];
    const bool b = t.enabled && !t.name.isEmpty();
    action->setEnabled(b);
    action->setVisible(b);
    action->setShortcut(t.sequence);
    action->setText(t.name);
  };

  setTool(view_->ui->actionTool1, 0);
  setTool(view_->ui->actionTool2, 1);
  setTool(view_->ui->actionTool3, 2);
  setTool(view_->ui->actionTool4, 3);
}

void MainController::updateDecisionCounts()
{
  const auto counts = model_->database_manager_->getDecisionCounts();

  const int delete_count = static_cast<int>(counts[1]);
  const int unclassified_count = static_cast<int>(counts[2] + counts[0]);
  const int keep_count = static_cast<int>(counts[3]);

  view_->ui->category_display->setCounts(delete_count, unclassified_count, keep_count);
}

std::string appendPathFragment(const std::string& filePath, std::string pathFragment)
{
  // Convert std::string to QString
  QString qFilePath = QString::fromStdString(filePath);

  // Sanitize the path fragment: remove leading/trailing separators
  QString qPathFragment = QString::fromStdString(pathFragment).trimmed();
  if (qPathFragment.startsWith(QDir::separator()) || qPathFragment.startsWith('/'))
  {
    qPathFragment.remove(0, 1);
  }
  if (qPathFragment.endsWith(QDir::separator()) || qPathFragment.endsWith('/'))
  {
    qPathFragment.chop(1);
  }

  QFileInfo fileInfo(qFilePath);
  QDir dir = fileInfo.absoluteDir();

  // Manually append the path fragment
  QString newPath = dir.absolutePath() + "/" + qPathFragment + "/" + fileInfo.fileName();

  // Convert QString back to std::string
  return newPath.toStdString();
}

bool moveFileWithDirectories(const std::string& sourcePath, const std::string& destinationPath)
{
  // Convert std::string to QString
  QString qSourcePath = QString::fromStdString(sourcePath);
  QString qDestinationPath = QString::fromStdString(destinationPath);

  // Create necessary directories for the destination path
  QDir dir;
  dir.mkpath(QFileInfo(qDestinationPath).absolutePath());

  // Move the file
  QFile file(qSourcePath);
  if (!file.rename(qDestinationPath))
  {
    // Handle error, e.g., log the error message file.errorString()
    return false;
  }

  return true;
}

void MainController::moveDeleteMarked()
{
  const auto to_delete = model_->database_manager_->getDeleteDecisionFilenames();

  std::size_t removed = 0;

  for (const auto& source_file : to_delete)
  {
    const auto node = model_->image_group_->lookup(source_file);

    if (node)
    {
      if (model_->image_group_->remove(source_file))
      {
        removed++;
      }

      if (!node->full_raw_path.empty())
      {
        const auto dest_file = appendPathFragment(node->full_raw_path, settings_->delete_foler_name_.toStdString());
        moveFileWithDirectories(node->full_raw_path, dest_file);
      }

      const auto dest_file = appendPathFragment(node->full_path, settings_->delete_foler_name_.toStdString());
      moveFileWithDirectories(node->full_path, dest_file);

      model_->database_manager_->removeRowForPath(node->full_path);
    }
  }
  if (removed > 0)
  {
    model_->image_group_->onFileListLoadComplete();
    updateDecisionCounts();
  }
}

void MainController::executeTool(int i)
{
  const auto node = model_->image_group_->lookup(current_image_full_path_.toStdString());
  if (!node)
  {
    return;
  }

  const auto& tool = settings_->tool[i];

  QString command = tool.command;

  if (command.contains("%r"))
  {
    // then we need a raw file
    if (node->full_raw_path.empty())
    {
      view_->statusBar()->showMessage(QString("Cannot start tool, \"%1\" needs a raw file").arg(tool.name), 5000);
      return;
    }
  }

  command.replace("%p", QString("%1").arg(QString::fromStdString(node->full_path)));
  command.replace("%r", QString("%1").arg(QString::fromStdString(node->full_raw_path)));

  command.replace("\\", "/");

  // Start the process detached
  if (!QProcess::startDetached(command))
  {
    view_->statusBar()->showMessage(QString("Failed to run: %1").arg(command), 5000);
  }
}

void MainController::vote(int direction)
{
  if (const auto& ptr = model_->image_group_->getNodeAtIndex(current_focus_index_); ptr)
  {
    if (decisionShift(ptr->decision, direction))
    {
      model_->database_manager_->setDecision(ptr->full_path, ptr->decision);
      view_->ui->graphicsView->setDecision(ptr->decision);
      view_->ui->treeView->doItemsLayout();
      updateDecisionCounts();
    }
  }
}

int MainController::predictNextNode(int step) const
{
  if (previous_focus_index_ < current_focus_index_)
  {
    return current_focus_index_ + step;
  }
  return current_focus_index_ - step;
}
