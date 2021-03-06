// Aseprite
// Copyright (C) 2001-2015  David Capello
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifndef APP_UI_CONTEXT_BAR_H_INCLUDED
#define APP_UI_CONTEXT_BAR_H_INCLUDED
#pragma once

#include "app/settings/settings_observers.h"
#include "app/ui/context_bar_observer.h"
#include "base/observable.h"
#include "doc/brush.h"
#include "doc/brushes.h"
#include "ui/box.h"

#include <vector>

namespace ui {
  class Box;
  class Button;
  class Label;
}

namespace tools {
  class Tool;
}

namespace app {

  class IBrushSettings;
  class IToolSettings;

  class ContextBar : public ui::Box,
                     public ToolSettingsObserver,
                     public base::Observable<ContextBarObserver> {
  public:
    ContextBar();
    ~ContextBar();

    void updateFromTool(tools::Tool* tool);
    void updateForMovingPixels();
    void updateSelectionMode(SelectionMode mode);
    void updateAutoSelectLayer(bool state);

    void setActiveBrush(const doc::BrushRef& brush);
    doc::BrushRef activeBrush(tools::Tool* tool = nullptr) const;
    void discardActiveBrush();

    // Adds a new brush and returns the slot number where the brush
    // is now available.
    int addBrush(const doc::BrushRef& brush);
    void removeBrush(int slot);
    void removeAllBrushes();
    void setActiveBrushBySlot(int slot);
    doc::Brushes getBrushes();

    static doc::BrushRef createBrushFromSettings(
      IBrushSettings* brushSettings = nullptr);

  protected:
    bool onProcessMessage(ui::Message* msg) override;
    void onPreferredSize(ui::PreferredSizeEvent& ev) override;

    // ToolSettingsObserver impl
    void onSetOpacity(int newOpacity) override;

  private:
    void onBrushSizeChange();
    void onBrushAngleChange();
    void onCurrentToolChange();
    void onDropPixels(ContextBarObserver::DropAction action);

    struct BrushSlot {
      // True if the user locked the brush using the shortcut key to
      // access it.
      bool locked;

      // Can be null if the user deletes the brush.
      doc::BrushRef brush;

      BrushSlot(const doc::BrushRef& brush)
        : locked(false), brush(brush) {
      }
    };

    typedef std::vector<BrushSlot> BrushSlots;

    class BrushTypeField;
    class BrushAngleField;
    class BrushSizeField;
    class ToleranceField;
    class ContiguousField;
    class InkTypeField;
    class InkOpacityField;
    class SprayWidthField;
    class SpraySpeedField;
    class SelectionModeField;
    class TransparentColorField;
    class RotAlgorithmField;
    class FreehandAlgorithmField;
    class BrushPatternField;
    class GrabAlphaField;
    class DropPixelsField;
    class AutoSelectLayerField;

    IToolSettings* m_toolSettings;
    BrushTypeField* m_brushType;
    BrushAngleField* m_brushAngle;
    BrushSizeField* m_brushSize;
    ui::Label* m_toleranceLabel;
    ToleranceField* m_tolerance;
    ContiguousField* m_contiguous;
    InkTypeField* m_inkType;
    ui::Label* m_opacityLabel;
    InkOpacityField* m_inkOpacity;
    GrabAlphaField* m_grabAlpha;
    AutoSelectLayerField* m_autoSelectLayer;
    ui::Box* m_freehandBox;
    FreehandAlgorithmField* m_freehandAlgo;
    BrushPatternField* m_brushPatternField;
    ui::Box* m_sprayBox;
    SprayWidthField* m_sprayWidth;
    SpraySpeedField* m_spraySpeed;
    ui::Box* m_selectionOptionsBox;
    SelectionModeField* m_selectionMode;
    TransparentColorField* m_transparentColor;
    RotAlgorithmField* m_rotAlgo;
    DropPixelsField* m_dropPixels;
    doc::BrushRef m_activeBrush;
    BrushSlots m_brushes;
  };

} // namespace app

#endif
