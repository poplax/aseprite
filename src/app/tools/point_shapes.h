// Aseprite
// Copyright (C) 2001-2015  David Capello
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

namespace app {
namespace tools {

class NonePointShape : public PointShape {
public:
  void transformPoint(ToolLoop* loop, int x, int y) override {
    // Do nothing
  }

  void getModifiedArea(ToolLoop* loop, int x, int y, Rect& area) override {
    // Do nothing
  }
};

class PixelPointShape : public PointShape {
public:
  void transformPoint(ToolLoop* loop, int x, int y) override {
    doInkHline(x, y, x, loop);
  }

  void getModifiedArea(ToolLoop* loop, int x, int y, Rect& area) override {
    area = Rect(x, y, 1, 1);
  }
};

class BrushPointShape : public PointShape {
  Brush* m_brush;
  base::SharedPtr<CompressedImage> m_compressedImage;
  bool m_firstPoint;

public:

  void preparePointShape(ToolLoop* loop) override {
    m_brush = loop->getBrush();
    m_compressedImage.reset(new CompressedImage(m_brush->image(), false));
    m_firstPoint = true;
  }

  void transformPoint(ToolLoop* loop, int x, int y) override {
    int h = m_brush->bounds().h;

    x += m_brush->bounds().x;
    y += m_brush->bounds().y;

    if (m_firstPoint) {
      m_firstPoint = false;
      if (m_brush->type() == kImageBrushType) {
        if (m_brush->pattern() == BrushPattern::ALIGNED_TO_DST ||
            m_brush->pattern() == BrushPattern::PAINT_BRUSH) {
          m_brush->setPatternOrigin(gfx::Point(x, y)-loop->getOffset());
        }
      }
    }
    else {
      if (m_brush->type() == kImageBrushType &&
          m_brush->pattern() == BrushPattern::PAINT_BRUSH) {
        m_brush->setPatternOrigin(gfx::Point(x, y)-loop->getOffset());
      }
    }

    for (auto scanline : *m_compressedImage) {
      int u = x+scanline.x;
      doInkHline(u, y+scanline.y, u+scanline.w-1, loop);
    }
  }

  void getModifiedArea(ToolLoop* loop, int x, int y, Rect& area) override {
    area = m_brush->bounds();
    area.x += x;
    area.y += y;
  }

};

class FloodFillPointShape : public PointShape {
public:
  bool isFloodFill() override { return true; }

  void transformPoint(ToolLoop* loop, int x, int y) override {
    doc::algorithm::floodfill(
      const_cast<Image*>(loop->getSrcImage()), x, y,
      paintBounds(loop, x, y),
      loop->getTolerance(),
      loop->getContiguous(),
      loop, (AlgoHLine)doInkHline);
  }

  void getModifiedArea(ToolLoop* loop, int x, int y, Rect& area) override {
    area = paintBounds(loop, x, y);
  }

private:
  gfx::Rect paintBounds(ToolLoop* loop, int x, int y) {
    gfx::Point offset = loop->getOffset();
    gfx::Rect bounds(
      offset.x, offset.y,
      loop->sprite()->width(), loop->sprite()->height());

    bounds = bounds.createIntersect(loop->getSrcImage()->bounds());

    // Limit the flood-fill to the current tile if the grid is visible.
    if (loop->getGridVisible()) {
      gfx::Rect grid = loop->getGridBounds();
      div_t d, dx, dy;

      dx = div(grid.x+loop->getOffset().x, grid.w);
      dy = div(grid.y+loop->getOffset().y, grid.h);

      d = div(x-dx.rem, grid.w);
      x = dx.rem + d.quot*grid.w;

      d = div(y-dy.rem, grid.h);
      y = dy.rem + d.quot*grid.h;

      bounds = bounds.createIntersect(gfx::Rect(x, y, grid.w, grid.h));
    }

    return bounds;
  }
};

class SprayPointShape : public PointShape {
  BrushPointShape m_subPointShape;

public:

  bool isSpray() override { return true; }

  void preparePointShape(ToolLoop* loop) override {
    m_subPointShape.preparePointShape(loop);
  }

  void transformPoint(ToolLoop* loop, int x, int y) override {
    int spray_width = loop->getSprayWidth();
    int spray_speed = loop->getSpraySpeed();
    int c, u, v, times = (spray_width*spray_width/4) * spray_speed / 100;

    // In Windows, rand() has a RAND_MAX too small
#if RAND_MAX <= 0xffff
    fixmath::fixed angle, radius;

    for (c=0; c<times; c++) {
      angle = fixmath::itofix(rand() * 256 / RAND_MAX);
      radius = fixmath::itofix(rand() * (spray_width*10) / RAND_MAX) / 10;
      u = fixmath::fixtoi(fixmath::fixmul(radius, fixmath::fixcos(angle)));
      v = fixmath::fixtoi(fixmath::fixmul(radius, fixmath::fixsin(angle)));

      m_subPointShape.transformPoint(loop, x+u, y+v);
    }
#else
    fixmath::fixed angle, radius;

    for (c=0; c<times; c++) {
      angle = rand();
      radius = rand() % fixmath::itofix(spray_width);
      u = fixmath::fixtoi(fixmath::fixmul(radius, fixmath::fixcos(angle)));
      v = fixmath::fixtoi(fixmath::fixmul(radius, fixmath::fixsin(angle)));
      m_subPointShape.transformPoint(loop, x+u, y+v);
    }
#endif
  }

  void getModifiedArea(ToolLoop* loop, int x, int y, Rect& area) override {
    int spray_width = loop->getSprayWidth();
    Point p1(x-spray_width, y-spray_width);
    Point p2(x+spray_width, y+spray_width);

    Rect area1;
    Rect area2;
    m_subPointShape.getModifiedArea(loop, p1.x, p1.y, area1);
    m_subPointShape.getModifiedArea(loop, p2.x, p2.y, area2);

    area = area1.createUnion(area2);
  }
};

} // namespace tools
} // namespace app
