#include "snapdecision/categorydisplaywidget.h"
#include "qnamespace.h"

#include <QPainter>
#include <algorithm>
#include <array>
#include <cmath>

template <std::size_t N>
void ensureMinimum(std::array<double, N>& values, const double min_finite)
{
  if (N == 0) return;

  double total_excess = 0.0;
  double total_value = 0.0;

  // Increase values to min_finite and calculate the total excess required
  for (double& value : values)
  {
    if (value > 0.0 && value < min_finite)
    {
      total_excess += min_finite - value;
      value = min_finite;
    }
    total_value += value;
  }

  // Check if redistribution is necessary
  if (total_excess > 0)
  {
    // Calculate proportions and perform redistribution
    double excess_distributed = 0.0;
    for (int i = 0; i < N - 1; ++i)
    {
      if (values[i] > min_finite)
      {
        double proportion = values[i] / total_value;
        double reduction = proportion * total_excess;
        values[i] -= reduction;
        excess_distributed += reduction;
      }
    }

    // Distribute any remaining excess from the last element to the first
    for (int i = N - 1; i >= 0; --i)
    {
      if (values[i] > min_finite && total_excess > excess_distributed)
      {
        double remaining_excess = total_excess - excess_distributed;
        double available_space = values[i] - min_finite;
        double reduction = std::min(remaining_excess, available_space);
        values[i] -= reduction;
        excess_distributed += reduction;

        if (total_excess <= excess_distributed)
        {
          break;
        }
      }
    }
  }
}

template <std::size_t N>
std::array<int, N> roundToIntegers(const std::array<double, N>& values)
{
  std::array<int, N> roundedValues;
  double totalOriginalSum = 0.0;
  int totalRoundedSum = 0;

  // First, round each value normally
  for (int i = 0; i < N; ++i)
  {
    roundedValues[i] = std::round(values[i]);
    totalOriginalSum += values[i];
    totalRoundedSum += roundedValues[i];
  }

  // Calculate the rounding error
  const int error = std::round(totalOriginalSum) - totalRoundedSum;

  // Distribute the error across the elements
  for (int i = 0; i < std::abs(error); ++i)
  {
    // Adjust the element by 1 to correct the error
    roundedValues[i % N] += (error > 0) ? 1 : -1;
  }

  return roundedValues;
}

template <std::size_t N>
std::array<int, N> splitWidth(const std::array<int, N>& input_values, int overall_width, int min_width)
{
  int total_input = 0;
  for (const auto input_value : input_values)
  {
    total_input += input_value;
  }

  if (total_input == 0)
  {
    return { 0, 0, 0 };
  }

  std::array<double, N> split_widths;

  for (std::size_t i = 0; i < N; i++)
  {
    split_widths[i] = static_cast<double>(static_cast<double>(overall_width * input_values[i]) / total_input);
  }

  ensureMinimum(split_widths, min_width);

  return roundToIntegers(split_widths);
}

CategoryDisplayWidget::CategoryDisplayWidget(QWidget* parent) : QFrame(parent)
{
  color_delete_ = QColor::fromString("#fd4949");
  color_keep_ = QColor::fromString("#2fe74a");
  color_gold_ = QColor::fromString("#e2bc00");
  color_unclassified_ = QColor::fromString("#2988ce");

  setMinimumWidth(100);
  setMaximumWidth(300);
}

void CategoryDisplayWidget::setCounts(int deleteCount, int unclassifiedCount, int keep_count, int superkeep_count)
{
  delete_count_ = deleteCount;
  unclassified_count_ = unclassifiedCount;
  keep_count_ = keep_count;
  superkeep_count_ = superkeep_count;

  update();
}

void CategoryDisplayWidget::paintEvent(QPaintEvent* event)
{
  QFrame::paintEvent(event);

  QPainter painter(this);

  // Set the text color
  QPen textPen(Qt::white);
  painter.setPen(textPen);

  const int frame_width = frameWidth();  // Get the width of the frame's border
  const int h = height() - 2 * frame_width;

  // Calculate font size based on the bar height
  int fontSize = static_cast<int>(h * 0.8);
  QFont font = painter.font();
  font.setPixelSize(fontSize);
  font.setBold(true);
  painter.setFont(font);

  const auto& [wd, wu, wk, wsk] = splitWidth<4>({ delete_count_, unclassified_count_, keep_count_, superkeep_count_ }, width(), 20);

  width_delete_ = wd;
  width_unclassified_ = wu;
  width_keep_ = wk;
  width_superkeep_ = wsk;

  if (wd + wu + wk + wsk > 0)
  {
    if (delete_count_ && delete_active_)
    {
      // Draw the 'delete' section
      QRect deleteRect(frame_width, frame_width, wd - frame_width, h);
      painter.fillRect(deleteRect, color_delete_);

      painter.setPen(QPen(Qt::white));
      painter.drawText(deleteRect, Qt::AlignCenter, QString::number(delete_count_));
    }

    if (delete_count_ && !delete_active_)
    {
      // Draw the 'delete' section
      QRect deleteRect(frame_width, frame_width, wd - frame_width, h);

      // Create a QPixmap and draw diagonal stripes on it
      QPixmap pixmap(20, 20);  // Adjust size as needed for the pattern
      pixmap.fill(color_delete_.darker(150));
      QPainter pixmapPainter(&pixmap);
      pixmapPainter.setPen(QPen(color_delete_, 3));  // Adjust pen width for stripe thickness

      // Draw diagonal lines
      for (int i = -pixmap.height(); i < pixmap.width(); i += 10)
      {  // Adjust step for stripe density
        pixmapPainter.drawLine(i, 0, i + pixmap.height(), pixmap.height());
      }

      // Create a brush with the pixmap
      QBrush brush(pixmap);
      painter.fillRect(deleteRect, brush);
      painter.setPen(QPen(Qt::black));
      painter.drawText(deleteRect, Qt::AlignCenter, QString::number(delete_count_));
    }

    if (unclassified_count_ && unclassified_active_)
    {
      QRect unclassifiedRect(wd, frame_width, wu, h);
      painter.fillRect(unclassifiedRect, color_unclassified_);
      painter.setPen(QPen(Qt::white));
      painter.drawText(unclassifiedRect, Qt::AlignCenter, QString::number(unclassified_count_));
    }

    if (unclassified_count_ && !unclassified_active_)
    {
      QRect unclassifiedRect(wd, frame_width, wu, h);

      // Create a QPixmap and draw diagonal stripes on it
      QPixmap pixmap(20, 20);  // Adjust size as needed for the pattern
      pixmap.fill(color_unclassified_.darker(150));
      QPainter pixmapPainter(&pixmap);
      pixmapPainter.setPen(QPen(color_unclassified_, 3));  // Adjust pen width for stripe thickness

      // Draw diagonal lines
      for (int i = -pixmap.height(); i < pixmap.width(); i += 10)
      {  // Adjust step for stripe density
        pixmapPainter.drawLine(i, 0, i + pixmap.height(), pixmap.height());
      }

      // Create a brush with the pixmap
      QBrush brush(pixmap);
      painter.fillRect(unclassifiedRect, brush);
      painter.setPen(QPen(Qt::black));
      painter.drawText(unclassifiedRect, Qt::AlignCenter, QString::number(unclassified_count_));
    }

    if (keep_count_ && keep_active_)
    {
      QRect keepRect(wd + wu, frame_width, wk - frame_width, h);
      painter.fillRect(keepRect, color_keep_);
      painter.setPen(QPen(Qt::white));
      painter.drawText(keepRect, Qt::AlignCenter, QString::number(keep_count_));
    }

    if (keep_count_ && !keep_active_)
    {
      QRect keepRect(wd + wu, frame_width, wk - frame_width, h);

      // Create a QPixmap and draw diagonal stripes on it
      QPixmap pixmap(20, 20);  // Adjust size as needed for the pattern
      pixmap.fill(color_keep_.darker(150));
      QPainter pixmapPainter(&pixmap);
      pixmapPainter.setPen(QPen(color_keep_, 3));  // Adjust pen width for stripe thickness

      // Draw diagonal lines
      for (int i = -pixmap.height(); i < pixmap.width(); i += 10)
      {  // Adjust step for stripe density
        pixmapPainter.drawLine(i, 0, i + pixmap.height(), pixmap.height());
      }

      // Create a brush with the pixmap
      QBrush brush(pixmap);
      painter.fillRect(keepRect, brush);
      painter.setPen(QPen(Qt::black));
      painter.drawText(keepRect, Qt::AlignCenter, QString::number(keep_count_));
    }

    if (superkeep_count_ && superkeep_active_)
    {
      QRect superkeepRect(wd + wu + wk, frame_width, wsk - frame_width, h);
      painter.fillRect(superkeepRect, color_gold_);
      painter.setPen(QPen(Qt::white));
      painter.drawText(superkeepRect, Qt::AlignCenter, QString::number(superkeep_count_));
    }

    if (superkeep_count_ && !superkeep_active_)
    {
      QRect keepRect(wd + wu + wk, frame_width, wsk - frame_width, h);

      // Create a QPixmap and draw diagonal stripes on it
      QPixmap pixmap(20, 20);  // Adjust size as needed for the pattern
      pixmap.fill(color_keep_.darker(150));
      QPainter pixmapPainter(&pixmap);
      pixmapPainter.setPen(QPen(color_gold_, 3));  // Adjust pen width for stripe thickness

      // Draw diagonal lines
      for (int i = -pixmap.height(); i < pixmap.width(); i += 10)
      {  // Adjust step for stripe density
        pixmapPainter.drawLine(i, 0, i + pixmap.height(), pixmap.height());
      }

      // Create a brush with the pixmap
      QBrush brush(pixmap);
      painter.fillRect(keepRect, brush);
      painter.setPen(QPen(Qt::black));
      painter.drawText(keepRect, Qt::AlignCenter, QString::number(superkeep_count_));
    }
  }
  else
  {
    QRect r(frame_width, frame_width, width() - 2 * frame_width, h);

    painter.fillRect(r, Qt::gray);
    painter.drawText(r, Qt::AlignCenter, "No files loaded");
  }
}

void CategoryDisplayWidget::mousePressEvent(QMouseEvent* event)
{
  int x = event->position().x();

  if (x < width_delete_)
  {
    setState(0, !delete_active_);
    return;
  }

  if ((x - width_delete_) < width_unclassified_)
  {
    setState(1, !unclassified_active_);
    return;
  }

  if ((x - width_delete_ - width_unclassified_) < width_keep_)
  {
    setState(2, !keep_active_);
    return;
  }

  if ((x - width_delete_ - width_unclassified_ - width_keep_) < width_superkeep_)
  {
    setState(3, !superkeep_active_);
    return;
  }
}

void CategoryDisplayWidget::setState(int which, bool new_value)
{
  if (which == 0)
  {
    delete_active_ = new_value;

    emit activeChange(DecisionType::Delete, delete_active_);
  }

  if (which == 1)
  {
    unclassified_active_ = new_value;
    emit activeChange(DecisionType::Unclassified, unclassified_active_);
  }

  if (which == 2)
  {
    keep_active_ = new_value;
    emit activeChange(DecisionType::Keep, keep_active_);
  }

  if (which == 3)
  {
    superkeep_active_ = new_value;
    emit activeChange(DecisionType::SuperKeep, superkeep_active_);
  }

  update();
}
