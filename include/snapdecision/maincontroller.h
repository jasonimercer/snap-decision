#pragma once

#include <QObject>

#include "mainmodel.h"
#include "mainwindow.h"
#include "snapdecision/imagegroup.h"
#include "snapdecision/settings.h"
#include "types.h"

// MainController.h - Coordinates between the Model and the View
class MainController : public QObject
{
  Q_OBJECT;

public:
  MainController(MainModel* model, MainWindow* view, Settings* settings);

  void debug(const QString& txt);

public slots:  // Slot declaration
  void treeBuildComplete();
  void loadResource(const QString& path);
  void focusOnNode(const QString& image_name);
  void memoryUsageChanged(CurrentMaxCount cmc);
  void updateDecisionCounts();
  void moveDeleteMarked();

private:
  void executeTool(int i);
  void removeAllDecisions();
  void showInExplorer(const QString& path);

  void voteAdjust(const ImageDescriptionNode::Ptr& ptr, int direction);
  void voteSet(const ImageDescriptionNode::Ptr& ptr, DecisionType decision);

  ImageDescriptionNode::Ptr currentNode();
  DecisionType currentDecision();

  bool keyPressed(QKeyEvent* event);
  void setSettings(const Settings& s);

  MainModel* model_;
  MainWindow* view_;
  Settings* settings_;


  int previous_focus_index_{ -1 };
  int current_focus_index_{ -1 };
  QString resource_;
  QString current_image_full_path_;

  int predictNextNode(int step) const;

  void setupConnections();
};
