#include "snapdecision/imagegroup.h"
#include "snapdecision/utils.h"

#include <optional>

ImageGroup::ImageGroup()
{
  connect(this, &ImageGroup::fileListLoadComplete, this, &ImageGroup::onFileListLoadComplete);
}

static ImageDescriptionNode::Ptr buildTree(const std::vector<ImageDescriptionNode::Ptr>& images, TimeMs sceneThreshold,
                                           TimeMs locationThreshold)
{
  auto root = std::make_shared<ImageDescriptionNode>(NodeType::Root);

  ImageDescriptionNode::Ptr currentLocation = nullptr;
  ImageDescriptionNode::Ptr currentScene = nullptr;

  TimeMs lastTimestamp = 0;

  for (size_t i = 0; i < images.size(); ++i)
  {
    const auto& img = images[i];

    // Check if a new Location should be started
    if (!currentLocation || img->time_ms - lastTimestamp > locationThreshold)
    {
      currentLocation = std::make_shared<ImageDescriptionNode>(NodeType::Location);
      root->children.push_back(currentLocation);
      currentLocation->parent = root;  // Set parent
      currentScene = nullptr;          // Reset current scene
    }

    // Check if a new Scene should be started
    if (!currentScene || img->time_ms - lastTimestamp > sceneThreshold)
    {
      currentScene = std::make_shared<ImageDescriptionNode>(NodeType::Scene);
      currentLocation->children.push_back(currentScene);
      currentScene->parent = currentLocation;  // Set parent
    }

    // Add Image node
    img->parent = currentScene;  // Set parent
    currentScene->children.push_back(img);

    lastTimestamp = img->time_ms;
  }

  return root;
}

static void simplifyTree(const ImageDescriptionNode::Ptr& node)
{
  if (!node)
    return;

  auto& children = node->children;

  // Traverse the tree and simplify it recursively
  for (auto it = children.begin(); it != children.end();)
  {
    auto& child = *it;
    simplifyTree(child);  // Recursive call

    // Check if the child is a Scene node with only one child
    if (child->node_type == NodeType::Scene && child->children.size() == 1)
    {
      auto grandchild = child->children.front();
      grandchild->parent = node;  // Update the parent pointer of the grandchild
      *it = grandchild;           // Replace the child with its own child
      continue;                   // Skip the increment as the iterator is already at the next element
    }

    ++it;  // Move to the next child
  }
}

void ImageGroup::loadFiles(const std::vector<std::string>& filenames, const ImageCache::Ptr& image_cache,
                           const TaskQueue::Ptr& task_queue, const DatabaseManager::Ptr& database_manager,
                           const DiagnosticFunction& diagnostic_function)
{
  const auto on_finish = [this]() { this->adjustWorkLeft(-1); };

  database_manager->close();

  adjustWorkLeft(filenames.size());

  flat_list_.clear();
  for (const auto& filename : filenames)
  {
    const auto node = buildImageDescriptionNode(filename, image_cache, task_queue, database_manager, on_finish);
    if (node)
    {
      flat_list_.push_back(node);
    }
  }

  map_.clear();
  for (const auto& node : flat_list_)
  {
    lookup(node->full_path) = node;
  }
}

ImageDescriptionNode::Ptr ImageGroup::getNodeAtIndex(int index) const
{
  if (flat_list_.empty())
  {
    return nullptr;
  }

  while (index < 0)
  {
    index += static_cast<int>(10 * flat_list_.size());
  }

  return flat_list_[index % flat_list_.size()];
}

ImageDescriptionNode::Ptr ImageGroup::findNode(const std::function<int ()>& index_generator, const std::function<bool (const ImageDescriptionNode::Ptr&)>& predicate) const
{
  for (std::size_t i = 0; i < flat_list_.size(); i++)
  {
    auto n = getNodeAtIndex(index_generator());
    if (predicate(n))
    {
      return n;
    }
  }
  return nullptr;
}

std::optional<int> ImageGroup::getIndexClosestTo(int index, ImageDescriptionNode* ptr) const
{
  if (flat_list_.empty() || !ptr)
  {
    return std::nullopt;
  }

  for (int i = 0; i < static_cast<int>(flat_list_.size()) / 2 + 1; i++)
  {
    if (const auto p = getNodeAtIndex(index + i); p && p.get() == ptr)
    {
      return index + i;
    }
    if (const auto p = getNodeAtIndex(index - i); p && p.get() == ptr)
    {
      return index - i;
    }
  }
  return std::nullopt;
}

ImageDescriptionNode::Ptr& ImageGroup::lookup(const std::string& full_path)
{
  return map_[extractFilename(full_path)];
}

bool ImageGroup::remove(const std::string& full_path)
{
  const auto node = lookup(full_path);

  if (!node)
  {
    return false;
  }

  std::erase(flat_list_, node);
  lookup(full_path) = nullptr;

  return true;
}

void ImageGroup::onFileListLoadComplete()
{
  std::sort(flat_list_.begin(), flat_list_.end(), [](const auto& a, const auto& b) { return a->time_ms < b->time_ms; });

  const Settings& settings = get_settings_();

  tree_root_ = buildTree(flat_list_, settings.burst_threshold_ms_, settings.location_theshold_ms_);

  simplifyTree(tree_root_);

  emit treeBuildComplete();
}

void ImageGroup::adjustWorkLeft(int delta_work)
{
  std::lock_guard lock(mutex_);

  work_left += delta_work;

  if (work_left == 0)
  {
    emit fileListLoadComplete();
  }
}
