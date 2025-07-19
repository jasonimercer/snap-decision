#include "snapdecision/imagetreeview.h"

#include "snapdecision/imagedescriptionnode.h"
#include "snapdecision/imagetreemodel.h"

ImageTreeView::ImageTreeView(QWidget* parent) : QTreeView(parent)
{
}

void ImageTreeView::updateHides()
{
  if (auto* image_model = dynamic_cast<ImageTreeModel*>(model()); image_model)
  {
    iterateChildHide(QModelIndex(), image_model);
  }
}

void ImageTreeView::iterateChildHide(const QModelIndex& parent, ImageTreeModel* model)
{
  const auto node_visible = [this](ImageDescriptionNode* n) -> bool { return isVisible(n->decision); };

  int rowCount = model->rowCount(parent);

  for (int row = 0; row < rowCount; ++row)
  {
    QModelIndex currentIndex = model->index(row, 0, parent);

    bool visible = true;
    if (auto* node = model->nodeFromIndex(currentIndex))
    {
      if (const auto c = node->leafConsensus(node_visible); c.has_value())
      {
        visible = c.value();
      }
    }

    setRowHidden(row, parent, !visible);

    if (model->hasChildren(currentIndex))
    {
      iterateChildHide(currentIndex, model);
    }
  }
}

QModelIndex ImageTreeView::getLastIndex() const
{
  QModelIndex lastIndex = model()->index(model()->rowCount() - 1, 0);
  if (lastIndex.isValid())
  {
    // Get the row count of the last parent
    int childRowCount = model()->rowCount(lastIndex);

    if (childRowCount > 0)
    {
      // Get the last child of the last parent
      lastIndex = model()->index(childRowCount - 1, 0, lastIndex);
    }
  }
  return lastIndex;
}

void ImageTreeView::navigate(int direction)
{
  if (direction == 0)
  {
    return;
  }

  auto* image_model = dynamic_cast<ImageTreeModel*>(model());

  if (!image_model)
  {
    qDebug() << "Failed to get model to navigate";
    return;
  }

  auto isItemNavigable = [this, &image_model](const QModelIndex& index)
  {
    const auto* node = image_model->nodeFromIndex(index);

    if (!node)
    {
      return false;
    }

    if (node->node_type != NodeType::Image)
    {
      return false;
    }

    return isVisible(node->decision);
  };

  auto findNavigableIndex = [this, &isItemNavigable](const QModelIndex& start_index, bool search_up)
  {
    const int maxIterations = 10;  // may need to skip over decorators / bursts / locations
    QModelIndex index = start_index;
    int iterations = 0;

    do
    {
      index = search_up ? indexAbove(index) : indexBelow(index);
      iterations++;
    } while (!isItemNavigable(index) && iterations < maxIterations);

    return index;
  };

  QModelIndex currentIndex = this->currentIndex();

  if (direction > 0)
  {
    QModelIndex nextIndex = findNavigableIndex(currentIndex, false);
    if (!nextIndex.isValid())
    {
      // Wrap to the first item
      nextIndex = findNavigableIndex(model()->index(0, 0), false);
    }
    if (nextIndex.isValid())
    {
      setCurrentIndex(nextIndex);
      navigate(direction - 1);
    }
  }

  if (direction < 0)
  {
    QModelIndex prevIndex = findNavigableIndex(currentIndex, true);
    if (!prevIndex.isValid())
    {
      // Wrap to the last item
      prevIndex = getLastIndex();
      if (!isItemNavigable(prevIndex))
      {
        prevIndex = findNavigableIndex(prevIndex, true);
      }
    }
    if (prevIndex.isValid())
    {
      setCurrentIndex(prevIndex);
      navigate(direction + 1);
    }
  }
}

void ImageTreeView::keyPressEvent(QKeyEvent* event)
{
  if (!event)
  {
    return;
  }

  bool event_used = false;

  if (key_event_function_)
  {
    event_used = key_event_function_(event);
  }

  if (!event_used)
  {
    QTreeView::keyPressEvent(event);
  }
}

bool ImageTreeView::isVisible(DecisionType decision_type) const
{
  switch (decision_type)
  {
  case DecisionType::SuperKeep:
    return superkeep_visible_;
  case DecisionType::Keep:
    return keep_visible_;
    case DecisionType::Unclassified:
      return unclassified_visible_;
    case DecisionType::Delete:
      return delete_visible_;
    default:
      return true;
  }
}

void ImageTreeView::setDecisionVisible(DecisionType d, bool visible)
{
  switch (d)
  {
    case DecisionType::SuperKeep:
      superkeep_visible_ = visible;
      break;
    case DecisionType::Keep:
      keep_visible_ = visible;
      break;
    case DecisionType::Unclassified:
      unclassified_visible_ = visible;
      break;
    case DecisionType::Delete:
      delete_visible_ = visible;
      break;
    default:
      break;
  }

  updateHides();
}
