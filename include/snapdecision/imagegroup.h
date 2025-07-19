#pragma once

#include <QObject>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "snapdecision/imagecache.h"
#include "snapdecision/imagedescriptionnode.h"
#include "snapdecision/settings.h"
#include "snapdecision/taskqueue.h"

class ImageGroup : public QObject
{
  Q_OBJECT;

public:
  using Ptr = std::shared_ptr<ImageGroup>;

  ImageGroup();

  void loadFiles(const std::vector<std::string>& filenames, const ImageCache::Ptr& image_cache,
                 const TaskQueue::Ptr& task_queue, const DatabaseManager::Ptr& database_manager,
                 const DiagnosticFunction& diagnostic_function);

  ImageDescriptionNode::Ptr getNodeAtIndex(int index) const;

  ImageDescriptionNode::Ptr findNode(
      const std::function<int()>& index_generator,
      const std::function<bool(const ImageDescriptionNode::Ptr&)>& predicate) const;

  std::optional<int> getIndexClosestTo(int index, ImageDescriptionNode* ptr) const;

  std::vector<ImageDescriptionNode::Ptr> flat_list_;
  ImageDescriptionNode::Ptr tree_root_;

  GetSettings get_settings_;

  ImageDescriptionNode::Ptr& lookup(const std::string& full_path);

  bool remove(const std::string& full_path);

signals:
  void fileListLoadComplete();
  void treeBuildComplete();

public slots:
  void onFileListLoadComplete();

private:
  void adjustWorkLeft(int delta_work);

  std::map<std::string, ImageDescriptionNode::Ptr> map_;

  std::mutex mutex_;
  std::atomic<int> work_left{ 0 };
};
