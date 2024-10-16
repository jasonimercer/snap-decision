#include "snapdecision/imagecache.h"

#include <QImage>
#include <QImageReader>
#include <chrono>
#include <iostream>

#include "snapdecision/libjpegturbo_loader.h"
#include "snapdecision/utils.h"

static std::size_t calculatePixmapMemoryUsage(const QPixmap& pixmap)
{
  if (pixmap.isNull())
  {
    return 0;
  }

  const int width = pixmap.width();
  const int height = pixmap.height();
  const int depth = pixmap.depth();

  return static_cast<std::size_t>(width * height * depth / 8);
}

ImageCacheHandle::ImageCacheHandle(std::weak_ptr<ImageCache> image_cache, const std::string& image_path)
  : image_cache_(image_cache), image_path_(image_path)
{
}

static QPixmap loadPixmap(const std::string& image_path)
{
  auto start = std::chrono::high_resolution_clock::now();

  if (QImage img = libjpegturboOpen(QString::fromStdString(image_path)); !img.isNull())
  {
    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
    // std::cout << image_path << " 1: " << duration.count() << " microseconds" << std::endl;

    return QPixmap::fromImage(img);
  }

  QImageReader img_reader(QString::fromStdString(image_path));

  if (!img_reader.canRead())
  {
    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
    // std::cout << image_path << " 2: " << duration.count() << " microseconds" << std::endl;

    return QPixmap();
  }

  img_reader.setAutoTransform(true);
  QImage img = img_reader.read();
  auto stop = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
  // std::cout << image_path << " 3: " << duration.count() << " microseconds" << std::endl;
  return QPixmap::fromImage(img);
}

QPixmap ImageCacheHandle::blockingImage()
{
  QPixmap return_value;
  {
    std::lock_guard lock(mutex_);

    if (!pixmap_.isNull())
    {
      touch();
      return pixmap_;
    }

    pixmap_ = loadPixmap(image_path_);
    memory_ = calculatePixmapMemoryUsage(pixmap_);
    state_ = State::Complete;
    touch();

    if (pixmap_.isNull())
    {
      ImageCache::getDiagFunction(image_cache_)(LogLevel::Error, "Failed to load image: " + image_path_);
    }
    return_value = pixmap_;
  }

  if (auto cache = image_cache_.lock())
  {
    // want to manage cache without holding mutex_
    cache->manageCache();
  }

  return return_value;
}

QPixmap ImageCacheHandle::image()
{
  std::lock_guard lock(mutex_);

  if (!pixmap_.isNull())
  {
    touch();
  }

  return pixmap_;
}

void ImageCacheHandle::scheduleImage()
{
  std::lock_guard lock(mutex_);

  if (state_ == State::Queued)
  {
    return;
  }

  if (const auto cache = image_cache_.lock())
  {
    state_ = State::Queued;
    cache->scheduleImage(image_path_);
  }
  else
  {
    makeDefaultDiagnosticFunction()(LogLevel::Error, "Cannot schedule an image when there is no backing cache");
  }
}

ImageCacheHandle::State ImageCacheHandle::getState() const
{
  return state_;
}

void ImageCacheHandle::touch()
{
  last_touch_ = getCurrentTimeMilliseconds();
}

std::string ImageCacheHandle::imagePath() const
{
  return image_path_;
}

std::shared_ptr<ImageCache> ImageCacheHandle::cache() const
{
  return image_cache_.lock();
}

std::size_t ImageCacheHandle::memory() const
{
  return memory_;
}

std::size_t ImageCacheHandle::lastUsage() const
{
  return last_touch_;
}

void ImageCacheHandle::unload()
{
  std::lock_guard lock(mutex_);

  pixmap_ = QPixmap();
  state_ = State::Unloaded;
  memory_ = 0;
}

ImageCache::ImageCache(const TaskQueue::Ptr& task_queue, DiagnosticFunction diag_function)
  : diag_func_(diag_function), task_queue_(task_queue)
{
}

ImageCacheHandle::Ptr ImageCache::getHandle(const std::string& image_path)
{
  std::lock_guard lock(cache_mutex_);

  if (auto ptr = cache_[image_path])
  {
    return ptr;
  }

  cache_[image_path] = std::make_shared<ImageCacheHandle>(shared_from_this(), image_path);
  return cache_[image_path];
}

ImageCacheHandle::Ptr ImageCache::getImage(const std::string& image_path)
{
  std::lock_guard lock(cache_mutex_);

  scheduleImage(image_path);
  return cache_[image_path];
}

ImageCacheHandle::Ptr ImageCache::immediateGetImage(const std::string& image_path)
{
  std::lock_guard lock(cache_mutex_);

  blockingLoadToCache(image_path);
  return cache_[image_path];
}

ImageCacheHandle::Ptr ImageCache::scheduleImage(const std::string& image_path)
{
  std::lock_guard lock(cache_mutex_);

  if (!cache_[image_path])
  {
    cache_[image_path] = std::make_shared<ImageCacheHandle>(shared_from_this(), image_path);
  }

  task_queue_->submit([this, image_path](double&) { blockingLoadToCache(image_path); });

  return cache_[image_path];
}

CurrentMaxCount ImageCache::getMemoryUsage() const
{
  std::lock_guard lock(cache_mutex_);

  std::size_t i = 0;
  for (const auto& [k, v] : cache_)
  {
    if (v && v->getState() == ImageCacheHandle::State::Complete)
      i++;
  }

  return { totalMemoryUsage(), max_memory_usage_, i };
}

std::size_t ImageCache::totalMemoryUsage() const
{
  std::lock_guard lock(cache_mutex_);

  std::size_t total_memory_usage = 0;

  for (const auto& [key, value] : cache_)
  {
    total_memory_usage += value ? value->memory() : 0;
  }

  return total_memory_usage;
}

void ImageCache::setMaxMemoryUsage(std::size_t max_memory_usage)
{
  max_memory_usage_ = max_memory_usage;
}

DiagnosticFunction ImageCache::getDiagFunction(const ImageCache::WeakPtr& wp)
{
  if (const auto ptr = wp.lock())
  {
    return ptr->diagFunction();
  }

  return makeDefaultDiagnosticFunction();
}

DiagnosticFunction ImageCache::diagFunction() const
{
  if (diag_func_)
  {
    return diag_func_;
  }

  return makeDefaultDiagnosticFunction();
}

void ImageCache::blockingLoadToCache(const std::string& image_path)
{
  if (image_path.empty())
  {
    diag_func_(LogLevel::Error, " image_path is empty");
    return;
  }

  auto ptr = [this, &image_path]()
  {
    std::lock_guard lock(cache_mutex_);

    if (!cache_[image_path])
    {
      cache_[image_path] = std::make_shared<ImageCacheHandle>(shared_from_this(), image_path);
    }

    return cache_[image_path];
  }();

  ptr->blockingImage();

  // manageCache();
}

void ImageCache::manageCache()
{
  CurrentMaxCount usage;
  {
    std::lock_guard lock(cache_mutex_);

    while (totalMemoryUsage() > max_memory_usage_ && !cache_.empty())
    {
      // find the oldest with data and remove it
      auto comp = [](const auto& a, const auto& b)
      { return a.second->memory() > 0 && (b.second->memory() == 0 || a.second->lastUsage() < b.second->lastUsage()); };

      auto it = std::min_element(cache_.begin(), cache_.end(), comp);

      it->second->unload();

      // cache_.erase(it);
    }
    usage = getMemoryUsage();
  }

  signal_emitter.emitMemoryUsageChanged(usage);
}

void ImageCache::updateHitMiss(std::size_t hit_inc, std::size_t miss_inc)
{
  hit_count_ += hit_inc;
  miss_count_ += miss_inc;

  signal_emitter.emitHitMissUpdate({ hit_count_, miss_count_ });
}

void ImageCacheSupport::SignalEmitter::emitMemoryUsageChanged(CurrentMaxCount cmc)
{
  emit memoryUsageChanged(cmc);
}

void ImageCacheSupport::SignalEmitter::emitHitMissUpdate(CountPair cp)
{
  emit hitMissUpdate(cp);
}
