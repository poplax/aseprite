// Aseprite
// Copyright (C) 2001-2015  David Capello
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/app.h"
#include "app/commands/command.h"
#include "app/context.h"
#include "app/document.h"
#include "app/find_widget.h"
#include "app/load_widget.h"
#include "app/modules/editors.h"
#include "app/pref/preferences.h"
#include "app/ui/status_bar.h"
#include "app/ui_context.h"
#include "ui/window.h"

namespace app {

using namespace ui;
using namespace gfx;

class ShowGridCommand : public Command {
public:
  ShowGridCommand()
    : Command("ShowGrid",
              "Show Grid",
              CmdUIOnlyFlag) {
  }

  Command* clone() const override { return new ShowGridCommand(*this); }

protected:
  bool onChecked(Context* ctx) {
    DocumentPreferences& docPref = App::instance()->preferences().document(ctx->activeDocument());
    return docPref.grid.visible();
  }

  void onExecute(Context* ctx) {
    DocumentPreferences& docPref = App::instance()->preferences().document(ctx->activeDocument());
    docPref.grid.visible(!docPref.grid.visible());
  }
};

class ShowPixelGridCommand : public Command {
public:
  ShowPixelGridCommand()
    : Command("ShowPixelGrid",
              "Show Pixel Grid",
              CmdUIOnlyFlag) {
  }

  Command* clone() const override { return new ShowPixelGridCommand(*this); }

protected:
  bool onChecked(Context* ctx) {
    DocumentPreferences& docPref = App::instance()->preferences().document(ctx->activeDocument());
    return docPref.pixelGrid.visible();
  }

  void onExecute(Context* ctx) {
    DocumentPreferences& docPref = App::instance()->preferences().document(ctx->activeDocument());
    docPref.pixelGrid.visible(!docPref.pixelGrid.visible());
  }
};

class SnapToGridCommand : public Command {
public:
  SnapToGridCommand()
    : Command("SnapToGrid",
              "Snap to Grid",
              CmdUIOnlyFlag) {
  }

  Command* clone() const override { return new SnapToGridCommand(*this); }

protected:
  bool onChecked(Context* ctx) {
    DocumentPreferences& docPref = App::instance()->preferences().document(ctx->activeDocument());
    return docPref.grid.snap();
  }

  void onExecute(Context* ctx) {
    DocumentPreferences& docPref = App::instance()->preferences().document(ctx->activeDocument());
    docPref.grid.snap(!docPref.grid.snap());

    char buf[512];
    sprintf(buf, "Snap to grid: %s",
      (docPref.grid.snap() ? "On": "Off"));

    StatusBar::instance()->setStatusText(250, buf);
  }
};

class GridSettingsCommand : public Command {
public:
  GridSettingsCommand();
  Command* clone() const override { return new GridSettingsCommand(*this); }

protected:
  bool onEnabled(Context* context);
  void onExecute(Context* context);
};

GridSettingsCommand::GridSettingsCommand()
  : Command("GridSettings",
            "Grid Settings",
            CmdUIOnlyFlag)
{
}

bool GridSettingsCommand::onEnabled(Context* context)
{
  return true;
}

void GridSettingsCommand::onExecute(Context* context)
{
  base::UniquePtr<Window> window(app::load_widget<Window>("grid_settings.xml", "grid_settings"));
  Widget* button_ok = app::find_widget<Widget>(window, "ok");
  Widget* grid_x = app::find_widget<Widget>(window, "grid_x");
  Widget* grid_y = app::find_widget<Widget>(window, "grid_y");
  Widget* grid_w = app::find_widget<Widget>(window, "grid_w");
  Widget* grid_h = app::find_widget<Widget>(window, "grid_h");

  DocumentPreferences& docPref = App::instance()->preferences().document(context->activeDocument());
  Rect bounds = docPref.grid.bounds();

  grid_x->setTextf("%d", bounds.x);
  grid_y->setTextf("%d", bounds.y);
  grid_w->setTextf("%d", bounds.w);
  grid_h->setTextf("%d", bounds.h);

  window->openWindowInForeground();

  if (window->getKiller() == button_ok) {
    bounds.x = grid_x->getTextInt();
    bounds.y = grid_y->getTextInt();
    bounds.w = grid_w->getTextInt();
    bounds.h = grid_h->getTextInt();
    bounds.w = MAX(bounds.w, 1);
    bounds.h = MAX(bounds.h, 1);

    docPref.grid.bounds(bounds);
  }
}

Command* CommandFactory::createShowGridCommand()
{
  return new ShowGridCommand;
}

Command* CommandFactory::createShowPixelGridCommand()
{
  return new ShowPixelGridCommand;
}

Command* CommandFactory::createSnapToGridCommand()
{
  return new SnapToGridCommand;
}

Command* CommandFactory::createGridSettingsCommand()
{
  return new GridSettingsCommand;
}

} // namespace app
