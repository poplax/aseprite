// Aseprite
// Copyright (C) 2001-2015  David Capello
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/ui/editor/pixels_movement.h"

#include "app/app.h"
#include "app/cmd/clear_mask.h"
#include "app/cmd/deselect_mask.h"
#include "app/cmd/set_mask.h"
#include "app/console.h"
#include "app/document.h"
#include "app/document_api.h"
#include "app/modules/gui.h"
#include "app/pref/preferences.h"
#include "app/settings/settings.h"
#include "app/snap_to_grid.h"
#include "app/ui/status_bar.h"
#include "app/ui_context.h"
#include "app/util/expand_cel_canvas.h"
#include "base/vector2d.h"
#include "doc/algorithm/flip_image.h"
#include "doc/algorithm/rotate.h"
#include "doc/algorithm/rotsprite.h"
#include "doc/cel.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/mask.h"
#include "doc/site.h"
#include "doc/sprite.h"
#include "gfx/region.h"
#include "render/render.h"

namespace app {

template<typename T>
static inline const base::Vector2d<double> point2Vector(const gfx::PointT<T>& pt) {
  return base::Vector2d<double>(pt.x, pt.y);
}

PixelsMovement::PixelsMovement(Context* context,
  Site site,
  const Image* moveThis, const gfx::Point& initialPos, int opacity,
  const char* operationName)
  : m_reader(context)
  , m_site(site)
  , m_document(static_cast<app::Document*>(site.document()))
  , m_sprite(site.sprite())
  , m_layer(site.layer())
  , m_transaction(context, operationName)
  , m_setMaskCmd(nullptr)
  , m_isDragging(false)
  , m_adjustPivot(false)
  , m_handle(NoHandle)
  , m_originalImage(Image::createCopy(moveThis))
  , m_maskColor(m_sprite->transparentColor())
{
  m_initialData = gfx::Transformation(gfx::Rect(initialPos, gfx::Size(moveThis->width(), moveThis->height())));
  m_currentData = m_initialData;

  ContextWriter writer(m_reader, 500);
  m_document->prepareExtraCel(m_sprite->bounds(), opacity);
  m_document->setExtraCelType(render::ExtraType::COMPOSITE);
  m_document->setExtraCelBlendMode(
    static_cast<LayerImage*>(m_layer)->getBlendMode());

  redrawExtraImage();

  m_initialMask = new Mask(*m_document->mask());
  m_currentMask = new Mask(*m_document->mask());

  UIContext::instance()->settings()->selection()->addObserver(this);
}

PixelsMovement::~PixelsMovement()
{
  UIContext::instance()->settings()->selection()->removeObserver(this);

  delete m_originalImage;
  delete m_initialMask;
  delete m_currentMask;
}

void PixelsMovement::flipImage(doc::algorithm::FlipType flipType)
{
  // Flip the image.
  doc::algorithm::flip_image(m_originalImage,
                                gfx::Rect(gfx::Point(0, 0),
                                          gfx::Size(m_originalImage->width(),
                                                    m_originalImage->height())),
                                flipType);

  // Flip the mask.
  doc::algorithm::flip_image(m_initialMask->bitmap(),
    gfx::Rect(gfx::Point(0, 0), m_initialMask->bounds().getSize()),
    flipType);

  {
    ContextWriter writer(m_reader, 500);

    // Regenerate the transformed (rotated, scaled, etc.) image and
    // mask.
    redrawExtraImage();
    redrawCurrentMask();

    m_document->setMask(m_currentMask);
    m_document->generateMaskBoundaries(m_currentMask);
    update_screen_for_document(m_document);
  }
}

void PixelsMovement::cutMask()
{
  {
    ContextWriter writer(m_reader, 500);
    if (writer.cel())
      m_transaction.execute(new cmd::ClearMask(writer.cel()));
  }

  copyMask();
}

void PixelsMovement::copyMask()
{
  // Hide the mask (do not deselect it, it will be moved them using
  // m_transaction.setMaskPosition)
  Mask emptyMask;
  {
    ContextWriter writer(m_reader, 500);
    m_document->generateMaskBoundaries(&emptyMask);
    update_screen_for_document(m_document);
  }
}

void PixelsMovement::catchImage(const gfx::Point& pos, HandleType handle)
{
  ASSERT(handle != NoHandle);

  m_catchPos = pos;
  m_isDragging = true;
  m_handle = handle;
}

void PixelsMovement::catchImageAgain(const gfx::Point& pos, HandleType handle)
{
  // Create a new Transaction to move the pixels to other position
  m_initialData = m_currentData;
  m_isDragging = true;
  m_catchPos = pos;
  m_handle = handle;

  // Hide the mask (do not deselect it, it will be moved them using
  // m_transaction.setMaskPosition)
  Mask emptyMask;
  {
    ContextWriter writer(m_reader, 500);
    m_document->generateMaskBoundaries(&emptyMask);
    update_screen_for_document(m_document);
  }
}

void PixelsMovement::maskImage(const Image* image)
{
  m_currentMask->replace(m_currentData.bounds());
  m_initialMask->copyFrom(m_currentMask);

  updateDocumentMask();

  update_screen_for_document(m_document);
}

void PixelsMovement::moveImage(const gfx::Point& pos, MoveModifier moveModifier)
{
  gfx::Transformation::Corners oldCorners;
  m_currentData.transformBox(oldCorners);

  ContextWriter writer(m_reader, 500);
  int x1, y1, x2, y2;

  x1 = m_initialData.bounds().x;
  y1 = m_initialData.bounds().y;
  x2 = m_initialData.bounds().x + m_initialData.bounds().w;
  y2 = m_initialData.bounds().y + m_initialData.bounds().h;

  bool updateBounds = false;
  int dx, dy;

  dx = int((pos.x - m_catchPos.x) *  cos(m_currentData.angle()) +
           (pos.y - m_catchPos.y) * -sin(m_currentData.angle()));
  dy = int((pos.x - m_catchPos.x) *  sin(m_currentData.angle()) +
           (pos.y - m_catchPos.y) *  cos(m_currentData.angle()));

  switch (m_handle) {

    case MoveHandle:
      x1 += dx;
      y1 += dy;
      x2 += dx;
      y2 += dy;
      updateBounds = true;

      if ((moveModifier & SnapToGridMovement) == SnapToGridMovement) {
        // Snap the x1,y1 point to the grid.
        gfx::Rect gridBounds = App::instance()
          ->preferences().document(m_document).grid.bounds();
        gfx::Point gridOffset(x1, y1);
        gridOffset = snap_to_grid(gridBounds, gridOffset);

        // Now we calculate the difference from x1,y1 point and we can
        // use it to adjust all coordinates (x1, y1, x2, y2).
        gridOffset -= gfx::Point(x1, y1);

        x1 += gridOffset.x;
        y1 += gridOffset.y;
        x2 += gridOffset.x;
        y2 += gridOffset.y;
      }
      else if ((moveModifier & LockAxisMovement) == LockAxisMovement) {
        if (ABS(dx) < ABS(dy)) {
          x1 -= dx;
          x2 -= dx;
        }
        else {
          y1 -= dy;
          y2 -= dy;
        }
      }
      break;

    case ScaleNWHandle:
      x1 = MIN(x1+dx, x2-1);
      y1 = MIN(y1+dy, y2-1);

      if ((moveModifier & MaintainAspectRatioMovement) == MaintainAspectRatioMovement) {
        if (1000 * (x2-x1) / getInitialImageSize().w >
            1000 * (y2-y1) / getInitialImageSize().h) {
          y1 = y2 - getInitialImageSize().h * (x2-x1) / getInitialImageSize().w;
        }
        else {
          x1 = x2 - getInitialImageSize().w * (y2-y1) / getInitialImageSize().h;
        }
      }

      updateBounds = true;
      break;

    case ScaleNHandle:
      y1 = MIN(y1+dy, y2-1);
      updateBounds = true;
      break;

    case ScaleNEHandle:
      x2 = MAX(x2+dx, x1+1);
      y1 = MIN(y1+dy, y2-1);

      if ((moveModifier & MaintainAspectRatioMovement) == MaintainAspectRatioMovement) {
        if (1000 * (x2-x1) / getInitialImageSize().w >
            1000 * (y2-y1) / getInitialImageSize().h) {
          y1 = y2 - getInitialImageSize().h * (x2-x1) / getInitialImageSize().w;
        }
        else {
          x2 = x1 + getInitialImageSize().w * (y2-y1) / getInitialImageSize().h;
        }
      }

      updateBounds = true;
      break;

    case ScaleWHandle:
      x1 = MIN(x1+dx, x2-1);
      updateBounds = true;
      break;

    case ScaleEHandle:
      x2 = MAX(x2+dx, x1+1);
      updateBounds = true;
      break;

    case ScaleSWHandle:
      x1 = MIN(x1+dx, x2-1);
      y2 = MAX(y2+dy, y1+1);

      if ((moveModifier & MaintainAspectRatioMovement) == MaintainAspectRatioMovement) {
        if (1000 * (x2-x1) / getInitialImageSize().w >
            1000 * (y2-y1) / getInitialImageSize().h) {
          y2 = y1 + getInitialImageSize().h * (x2-x1) / getInitialImageSize().w;
        }
        else {
          x1 = x2 - getInitialImageSize().w * (y2-y1) / getInitialImageSize().h;
        }
      }

      updateBounds = true;
      break;

    case ScaleSHandle:
      y2 = MAX(y2+dy, y1+1);
      updateBounds = true;
      break;

    case ScaleSEHandle:
      x2 = MAX(x2+dx, x1+1);
      y2 = MAX(y2+dy, y1+1);

      if ((moveModifier & MaintainAspectRatioMovement) == MaintainAspectRatioMovement) {
        if (1000 * (x2-x1) / getInitialImageSize().w >
            1000 * (y2-y1) / getInitialImageSize().h) {
          y2 = y1 + getInitialImageSize().h * (x2-x1) / getInitialImageSize().w;
        }
        else {
          x2 = x1 + getInitialImageSize().w * (y2-y1) / getInitialImageSize().h;
        }
      }

      updateBounds = true;
      break;

    case RotateNWHandle:
    case RotateNHandle:
    case RotateNEHandle:
    case RotateWHandle:
    case RotateEHandle:
    case RotateSWHandle:
    case RotateSHandle:
    case RotateSEHandle:
      {
        gfx::Point abs_initial_pivot = m_initialData.pivot();
        gfx::Point abs_pivot = m_currentData.pivot();

        double newAngle =
          m_initialData.angle()
          + atan2((double)(-pos.y + abs_pivot.y),
                  (double)(+pos.x - abs_pivot.x))
          - atan2((double)(-m_catchPos.y + abs_initial_pivot.y),
                  (double)(+m_catchPos.x - abs_initial_pivot.x));

        // Put the angle in -180 to 180 range.
        while (newAngle < -PI) newAngle += 2*PI;
        while (newAngle > PI) newAngle -= 2*PI;

        // Is the "angle snap" is activated, we've to snap the angle
        // to common (pixel art) angles.
        if ((moveModifier & AngleSnapMovement) == AngleSnapMovement) {
          // TODO make this configurable
          static const double keyAngles[] = {
            0.0, 26.565, 45.0, 63.435, 90.0, 116.565, 135.0, 153.435, -180.0,
            180.0, -153.435, -135.0, -116, -90.0, -63.435, -45.0, -26.565
          };

          double newAngleDegrees = 180.0 * newAngle / PI;

          int closest = 0;
          int last = sizeof(keyAngles) / sizeof(keyAngles[0]) - 1;
          for (int i=0; i<=last; ++i) {
            if (std::fabs(newAngleDegrees-keyAngles[closest]) >
                std::fabs(newAngleDegrees-keyAngles[i]))
              closest = i;
          }

          newAngle = PI * keyAngles[closest] / 180.0;
        }

        m_currentData.angle(newAngle);
      }
      break;

    case PivotHandle:
      {
        // Calculate the new position of the pivot
        gfx::Point newPivot(m_initialData.pivot().x + (pos.x - m_catchPos.x),
                            m_initialData.pivot().y + (pos.y - m_catchPos.y));

        m_currentData = m_initialData;
        m_currentData.displacePivotTo(newPivot);
      }
      break;
  }

  if (updateBounds) {
    m_currentData.bounds(gfx::Rect(x1, y1, x2 - x1, y2 - y1));
    m_adjustPivot = true;
  }

  redrawExtraImage();

  m_document->setTransformation(m_currentData);

  // Get the new transformed corners
  gfx::Transformation::Corners newCorners;
  m_currentData.transformBox(newCorners);

  // Create a union of all corners, and that will be the bounds to
  // redraw of the sprite.
  gfx::Rect fullBounds;
  for (int i=0; i<gfx::Transformation::Corners::NUM_OF_CORNERS; ++i) {
    fullBounds = fullBounds.createUnion(gfx::Rect((int)oldCorners[i].x, (int)oldCorners[i].y, 1, 1));
    fullBounds = fullBounds.createUnion(gfx::Rect((int)newCorners[i].x, (int)newCorners[i].y, 1, 1));
  }

  // If "fullBounds" is empty is because the cel was not moved
  if (!fullBounds.isEmpty()) {
    // Notify the modified region.
    m_document->notifySpritePixelsModified(m_sprite, gfx::Region(fullBounds));
  }
}

Image* PixelsMovement::getDraggedImageCopy(gfx::Point& origin)
{
  gfx::Transformation::Corners corners;
  m_currentData.transformBox(corners);

  gfx::Point leftTop(corners[0]);
  gfx::Point rightBottom(corners[0]);
  for (size_t i=1; i<corners.size(); ++i) {
    if (leftTop.x > corners[i].x) leftTop.x = (int)corners[i].x;
    if (leftTop.y > corners[i].y) leftTop.y = (int)corners[i].y;
    if (rightBottom.x < corners[i].x) rightBottom.x = (int)corners[i].x;
    if (rightBottom.y < corners[i].y) rightBottom.y = (int)corners[i].y;
  }

  int width = rightBottom.x - leftTop.x;
  int height = rightBottom.y - leftTop.y;
  base::UniquePtr<Image> image(Image::create(m_sprite->pixelFormat(), width, height));

  drawImage(image, leftTop);

  origin = leftTop;

  return image.release();
}

void PixelsMovement::stampImage()
{
  const Cel* cel = m_document->getExtraCel();
  const Image* image = m_document->getExtraCelImage();

  ASSERT(cel && image);

  {
    ContextWriter writer(m_reader, 500);
    {
      // Expand the canvas to paste the image in the fully visible
      // portion of sprite.
      ExpandCelCanvas expand(m_site,
        TiledMode::NONE, m_transaction,
        ExpandCelCanvas::None);

      // TODO can we reduce this region?
      gfx::Region modifiedRegion(expand.getDestCanvas()->bounds());
      expand.validateDestCanvas(modifiedRegion);

      render::composite_image(
        expand.getDestCanvas(), image,
        -expand.getCel()->x(),
        -expand.getCel()->y(),
        cel->opacity(), BLEND_MODE_NORMAL);

      expand.commit();
    }
    // TODO
    // m_transaction.commit();
  }
}

void PixelsMovement::dropImageTemporarily()
{
  m_isDragging = false;

  {
    ContextWriter writer(m_reader, 500);

    // TODO Add undo information so the user can undo each transformation step.

    // Displace the pivot to the new site:
    if (m_adjustPivot) {
      m_adjustPivot = false;

      // Get the a factor for the X/Y position of the initial pivot
      // position inside the initial non-rotated bounds.
      gfx::PointT<double> pivotPosFactor(m_initialData.pivot() - m_initialData.bounds().getOrigin());
      pivotPosFactor.x /= m_initialData.bounds().w;
      pivotPosFactor.y /= m_initialData.bounds().h;

      // Get the current transformed bounds.
      gfx::Transformation::Corners corners;
      m_currentData.transformBox(corners);

      // The new pivot will be located from the rotated left-top
      // corner a distance equal to the transformed bounds's
      // width/height multiplied with the previously calculated X/Y
      // factor.
      base::Vector2d<double> newPivot(corners.leftTop().x,
                                      corners.leftTop().y);
      newPivot += pivotPosFactor.x * point2Vector(corners.rightTop() - corners.leftTop());
      newPivot += pivotPosFactor.y * point2Vector(corners.leftBottom() - corners.leftTop());

      m_currentData.displacePivotTo(gfx::Point((int)newPivot.x, (int)newPivot.y));
    }

    redrawCurrentMask();
    updateDocumentMask();

    update_screen_for_document(m_document);
  }
}

void PixelsMovement::dropImage()
{
  m_isDragging = false;

  // Stamp the image in the current layer.
  stampImage();

  // This is the end of the whole undo transaction.
  m_transaction.commit();

  // Destroy the extra cel (this cel will be used by the drawing
  // cursor surely).
  ContextWriter writer(m_reader, 500);
  m_document->destroyExtraCel();
}

void PixelsMovement::discardImage(bool commit)
{
  m_isDragging = false;

  // Deselect the mask (here we don't stamp the image)
  m_transaction.execute(new cmd::DeselectMask(m_document));

  if (commit)
    m_transaction.commit();

  // Destroy the extra cel and regenerate the mask boundaries (we've
  // just deselect the mask).
  ContextWriter writer(m_reader, 500);
  m_document->destroyExtraCel();
  m_document->generateMaskBoundaries();
}

bool PixelsMovement::isDragging() const
{
  return m_isDragging;
}

gfx::Rect PixelsMovement::getImageBounds()
{
  const Cel* cel = m_document->getExtraCel();
  const Image* image = m_document->getExtraCelImage();

  ASSERT(cel != NULL);
  ASSERT(image != NULL);

  return gfx::Rect(cel->x(), cel->y(), image->width(), image->height());
}

gfx::Size PixelsMovement::getInitialImageSize() const
{
  return m_initialData.bounds().getSize();
}

void PixelsMovement::setMaskColor(color_t mask_color)
{
  ContextWriter writer(m_reader, 500);

  m_maskColor = mask_color;

  redrawExtraImage();
  update_screen_for_document(m_document);
}

void PixelsMovement::redrawExtraImage()
{
  // Draw the transformed pixels in the extra-cel which is the chunk
  // of pixels that the user is moving.
  drawImage(m_document->getExtraCelImage(), gfx::Point(0, 0));
}

void PixelsMovement::redrawCurrentMask()
{
  gfx::Transformation::Corners corners;
  m_currentData.transformBox(corners);

  // Transform mask

  m_currentMask->replace(m_sprite->bounds());
  m_currentMask->freeze();
  clear_image(m_currentMask->bitmap(), 0);
  drawParallelogram(m_currentMask->bitmap(), m_initialMask->bitmap(),
    corners, gfx::Point(0, 0));

  m_currentMask->unfreeze();
}

void PixelsMovement::drawImage(doc::Image* dst, const gfx::Point& pt)
{
  gfx::Transformation::Corners corners;
  m_currentData.transformBox(corners);

  dst->setMaskColor(m_sprite->transparentColor());
  clear_image(dst, dst->maskColor());

  m_originalImage->setMaskColor(m_maskColor);
  drawParallelogram(dst, m_originalImage, corners, pt);
}

void PixelsMovement::drawParallelogram(doc::Image* dst, doc::Image* src,
  const gfx::Transformation::Corners& corners,
  const gfx::Point& leftTop)
{
  RotationAlgorithm rotAlgo = UIContext::instance()->settings()->selection()->getRotationAlgorithm();

  // If the angle and the scale weren't modified, we should use the
  // fast rotation algorithm, as it's pixel-perfect match with the
  // original selection when just a translation is applied.
  if (m_currentData.angle() == 0.0 &&
      m_currentData.bounds().getSize() == src->size()) {
    rotAlgo = kFastRotationAlgorithm;
  }

retry:;      // In case that we don't have enough memory for RotSprite
             // we can try with the fast algorithm anyway.

  switch (rotAlgo) {

    case kFastRotationAlgorithm:
      doc::algorithm::parallelogram(dst, src,
        int(corners.leftTop().x-leftTop.x),
        int(corners.leftTop().y-leftTop.y),
        int(corners.rightTop().x-leftTop.x),
        int(corners.rightTop().y-leftTop.y),
        int(corners.rightBottom().x-leftTop.x),
        int(corners.rightBottom().y-leftTop.y),
        int(corners.leftBottom().x-leftTop.x),
        int(corners.leftBottom().y-leftTop.y));
      break;

    case kRotSpriteRotationAlgorithm:
      try {
        doc::algorithm::rotsprite_image(dst, src,
          int(corners.leftTop().x-leftTop.x),
          int(corners.leftTop().y-leftTop.y),
          int(corners.rightTop().x-leftTop.x),
          int(corners.rightTop().y-leftTop.y),
          int(corners.rightBottom().x-leftTop.x),
          int(corners.rightBottom().y-leftTop.y),
          int(corners.leftBottom().x-leftTop.x),
          int(corners.leftBottom().y-leftTop.y));
      }
      catch (const std::bad_alloc&) {
        StatusBar::instance()->showTip(1000,
          "Not enough memory for RotSprite");

        rotAlgo = kFastRotationAlgorithm;
        goto retry;
      }
      break;

  }
}

void PixelsMovement::onSetRotationAlgorithm(RotationAlgorithm algorithm)
{
  try {
    redrawExtraImage();
    redrawCurrentMask();
    updateDocumentMask();

    update_screen_for_document(m_document);
  }
  catch (const std::exception& ex) {
    Console::showException(ex);
  }
}

void PixelsMovement::updateDocumentMask()
{
  if (!m_setMaskCmd) {
    m_setMaskCmd = new cmd::SetMask(m_document, m_currentMask);
    m_transaction.execute(m_setMaskCmd);
  }
  else
    m_setMaskCmd->setNewMask(m_currentMask);

  m_document->generateMaskBoundaries(m_currentMask);
}

} // namespace app
