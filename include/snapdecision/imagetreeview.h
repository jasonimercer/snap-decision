#pragma once

#include <QKeyEvent>
#include <QTreeView>

#include "snapdecision/types.h"

class ImageTreeModel;

class ImageTreeView : public QTreeView
{
  Q_OBJECT

public:
  ImageTreeView(QWidget* parent = nullptr);

  void navigate(int direction);

  bool isVisible(DecisionType decision_type) const;

  keyEventFunction key_event_function_;

public slots:
  void setDecisionVisible(DecisionType d, bool visible);
  void updateHides();

protected:
  QModelIndex getLastIndex() const;
  void keyPressEvent(QKeyEvent* event) override;
  void iterateChildHide(const QModelIndex &parent, ImageTreeModel* model);

  bool superkeep_visible_{ true };
  bool keep_visible_{ true };
  bool unclassified_visible_{ true };
  bool delete_visible_{ true };
};
