#include <QApplication>
#include <QImageReader>

#include "snapdecision/databasemanager.h"
#include "snapdecision/diagnostics.h"
#include "snapdecision/dnn.h"
#include "snapdecision/maincontroller.h"
#include "snapdecision/mainmodel.h"
#include "snapdecision/mainwindow.h"
#include "snapdecision/settings.h"
#include "snapdecision/taskqueue.h"

#include <functional>

std::function<void(QString)> diag_chain_;

void diag(LogLevel level, const std::string& message)
{
  const auto d = [](const std::string& m) {
    qDebug() << m.c_str() << "\n";
    if (diag_chain_)
    {
      diag_chain_(QString::fromStdString(m));
    }
  };

  switch (level)
  {
    case LogLevel::Error:
      d("ERROR: " + message);
      break;

    case LogLevel::Warn:
      d("WARN: " + message);
      break;

    case LogLevel::Info:
      d("INFO: " + message);
      break;
  }
}

void loadSettings(Settings& s)
{
  const Settings& d = Settings{};

  QSettings q("JasonIMercer", "SnapDecision");

  s.burst_threshold_ms_ = q.value("burst_ms", d.burst_threshold_ms_).toULongLong();
  s.location_theshold_ms_ = q.value("location_ms", d.location_theshold_ms_).toULongLong();

  s.delete_foler_name_ = q.value("delete_folder_name", d.delete_foler_name_).toString();

  s.cache_memory_mb_ = q.value("cache_memory_mb", d.cache_memory_mb_).toULongLong();

  s.show_debug_console_ = q.value("show_debug_console", d.show_debug_console_).toBool();

  const auto key = [&](const auto& k, auto member)
  { s.*member = QKeySequence{ q.value(k, (d.*member).toString()).toString() }; };

  key("key_next_image_", &Settings::key_next_image_);
  key("key_prev_image_", &Settings::key_prev_image_);
  key("key_keep_and_next_", &Settings::key_keep_and_next_);
  key("key_gold_and_next_", &Settings::key_gold_and_next_);
  key("key_delete_and_next_", &Settings::key_delete_and_next_);
  key("key_unclassified_to_delete_and_next_", &Settings::key_unclassified_to_delete_and_next_);
  key("key_unclassified_to_keep_and_next_", &Settings::key_unclassified_to_keep_and_next_);
  key("key_unclassified_to_gold_and_next_", &Settings::key_unclassified_to_gold_and_next_);

  key("key_pan_left_", &Settings::key_pan_left_);
  key("key_pan_right_", &Settings::key_pan_right_);
  key("key_pan_up_", &Settings::key_pan_up_);
  key("key_pan_down_", &Settings::key_pan_down_);

  key("key_reset_view_", &Settings::key_reset_view_);
  key("key_zoom_1_", &Settings::key_zoom_1_);
  key("key_zoom_2_", &Settings::key_zoom_2_);
  key("key_zoom_3_", &Settings::key_zoom_3_);
  key("key_zoom_4_", &Settings::key_zoom_4_);
  key("key_zoom_5_", &Settings::key_zoom_5_);

  key("key_vote_up_", &Settings::key_vote_up_);
  key("key_vote_down_", &Settings::key_vote_down_);

  key("key_preferences_", &Settings::key_preferences_);
  key("key_delete_marked_", &Settings::key_delete_marked_);

  for (int i = 0; i < 4; i++)
  {
    auto n = QString::number(i);
    s.tool[i].enabled = q.value("tool_enabled_" + n, d.tool[i].enabled).toBool();
    s.tool[i].name = q.value("tool_name_" + n, d.tool[i].name).toString();
    s.tool[i].command = q.value("tool_command_" + n, d.tool[i].command).toString();
    s.tool[i].sequence = q.value("tool_sequence_" + n, d.tool[i].sequence.toString()).toString();
  }
}

void storeSettings(const Settings& s)
{
  QSettings q("JasonIMercer", "SnapDecision");

  q.setValue("burst_ms", s.burst_threshold_ms_);
  q.setValue("location_ms", s.location_theshold_ms_);
  q.setValue("delete_folder_name", s.delete_foler_name_);
  q.setValue("cache_memory_mb", s.cache_memory_mb_);
  q.setValue("show_debug_console", s.show_debug_console_);

  const auto key = [&](const auto& k, auto member) { q.setValue(k, (s.*member).toString()); };

  key("key_next_image_", &Settings::key_next_image_);
  key("key_prev_image_", &Settings::key_prev_image_);
  key("key_keep_and_next_", &Settings::key_keep_and_next_);
  key("key_gold_and_next_", &Settings::key_gold_and_next_);
  key("key_delete_and_next_", &Settings::key_delete_and_next_);
  key("key_unclassified_to_delete_and_next_", &Settings::key_unclassified_to_delete_and_next_);
  key("key_unclassified_to_keep_and_next_", &Settings::key_unclassified_to_keep_and_next_);
  key("key_unclassified_to_gold_and_next_", &Settings::key_unclassified_to_gold_and_next_);

  key("key_pan_left_", &Settings::key_pan_left_);
  key("key_pan_right_", &Settings::key_pan_right_);
  key("key_pan_up_", &Settings::key_pan_up_);
  key("key_pan_down_", &Settings::key_pan_down_);

  key("key_reset_view_", &Settings::key_reset_view_);
  key("key_zoom_1_", &Settings::key_zoom_1_);
  key("key_zoom_2_", &Settings::key_zoom_2_);
  key("key_zoom_3_", &Settings::key_zoom_3_);
  key("key_zoom_4_", &Settings::key_zoom_4_);
  key("key_zoom_5_", &Settings::key_zoom_5_);

  key("key_vote_up_", &Settings::key_vote_up_);
  key("key_vote_down_", &Settings::key_vote_down_);

  key("key_preferences_", &Settings::key_preferences_);
  key("key_delete_marked_", &Settings::key_delete_marked_);

  for (int i = 0; i < 4; i++)
  {
    auto n = QString::number(i);
    q.setValue("tool_enabled_" + n, s.tool[i].enabled);
    q.setValue("tool_name_" + n, s.tool[i].name);
    q.setValue("tool_command_" + n, s.tool[i].command);
    q.setValue("tool_sequence_" + n, s.tool[i].sequence.toString());
  }
}

int main(int argc, char* argv[])
{
  QApplication a(argc, argv);

  QStringList args = a.arguments();

  QImageReader::setAllocationLimit(1024);

  if (false)
  {
    DatabaseManager::unitTest();
    return 0;
  }

  a.setWindowIcon(QIcon(":/images/icon2.png"));

  Settings settings;

  loadSettings(settings);

  MainModel m;
  m.task_queue_ = std::make_shared<TaskQueue>();
  m.database_manager_ = std::make_shared<DatabaseManager>(diag);
  m.image_cache_ = std::make_shared<ImageCache>(m.task_queue_, diag);
  m.image_cache_->setMaxMemoryUsage(settings.cache_memory_mb_ * 1000000);
  m.image_tree_model_ = std::make_shared<ImageTreeModel>();
  m.image_group_ = std::make_shared<ImageGroup>();
  m.diagnostic_function_ = diag;

  MainWindow w;
  MainController c(&m, &w, &settings);

  diag_chain_ = [&c](const QString& message){
    c.debug(message);
  };

  if (args.size() > 1)
  {
    c.loadResource(args.at(1));
  }

  w.show();
  int return_value = a.exec();

  storeSettings(settings);

  return return_value;
}
