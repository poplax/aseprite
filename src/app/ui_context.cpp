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
#include "app/document.h"
#include "app/modules/editors.h"
#include "app/pref/preferences.h"
#include "app/settings/ui_settings_impl.h"
#include "app/ui/color_bar.h"
#include "app/ui/document_view.h"
#include "app/ui/editor/editor.h"
#include "app/ui/main_window.h"
#include "app/ui/preview_editor.h"
#include "app/ui/status_bar.h"
#include "app/ui/timeline.h"
#include "app/ui/workspace.h"
#include "app/ui/workspace_tabs.h"
#include "app/ui_context.h"
#include "base/mutex.h"
#include "doc/site.h"
#include "doc/sprite.h"

namespace app {

UIContext* UIContext::m_instance = nullptr;

UIContext::UIContext()
  : Context(new UISettingsImpl)
  , m_lastSelectedDoc(nullptr)
  , m_lastSelectedView(nullptr)
{
  documents().addObserver(&App::instance()->preferences());

  ASSERT(m_instance == NULL);
  m_instance = this;
}

UIContext::~UIContext()
{
  ASSERT(m_instance == this);
  m_instance = NULL;

  documents().removeObserver(&App::instance()->preferences());

  // The context must be empty at this point. (It's to check if the UI
  // is working correctly, i.e. closing all files when the user can
  // take any action about it.)
  ASSERT(documents().empty());
}

bool UIContext::isUiAvailable() const
{
  return App::instance()->isGui();
}

DocumentView* UIContext::activeView() const
{
  if (!isUiAvailable())
    return NULL;

  Workspace* workspace = App::instance()->getMainWindow()->getWorkspace();
  WorkspaceView* view = workspace->activeView();
  if (DocumentView* docView = dynamic_cast<DocumentView*>(view))
    return docView;
  else
    return NULL;
}

void UIContext::setActiveView(DocumentView* docView)
{
  // Do nothing cases: 1) the view is already selected, or 2) the view
  // is the a preview.
  if (m_lastSelectedView == docView ||
      (docView && docView->isPreview()))
    return;

  MainWindow* mainWin = App::instance()->getMainWindow();
  if (docView) {
    mainWin->getTabsBar()->selectTab(docView);

    if (mainWin->getWorkspace()->activeView() != docView)
      mainWin->getWorkspace()->setActiveView(docView);
  }

  current_editor = (docView ? docView->getEditor(): NULL);

  if (current_editor)
    current_editor->requestFocus();

  mainWin->getPreviewEditor()->updateUsingEditor(current_editor);
  mainWin->getTimeline()->updateUsingEditor(current_editor);
  StatusBar::instance()->updateUsingEditor(current_editor);

  // Change the image-type of color bar.
  ColorBar::instance()->setPixelFormat(app_get_current_pixel_format());

  // Restore the palette of the selected document.
  app_refresh_screen();

  // Change the main frame title.
  App::instance()->updateDisplayTitleBar();

  m_lastSelectedView = docView;
}

void UIContext::setActiveDocument(Document* document)
{
  m_lastSelectedDoc = document;

  DocumentView* docView = getFirstDocumentView(document);
  if (docView)    // The view can be null if we are in --batch mode
    setActiveView(docView);
}

DocumentView* UIContext::getFirstDocumentView(Document* document) const
{
  MainWindow* mainWindow = App::instance()->getMainWindow();
  if (!mainWindow) // Main window can be null if we are in --batch mode
    return nullptr;

  Workspace* workspace = mainWindow->getWorkspace();

  for (WorkspaceView* view : *workspace) {
    if (DocumentView* docView = dynamic_cast<DocumentView*>(view)) {
      if (docView->getDocument() == document) {
        return docView;
      }
    }
  }

  return nullptr;
}

Editor* UIContext::activeEditor()
{
  DocumentView* view = activeView();
  if (view)
    return view->getEditor();
  else
    return NULL;
}

void UIContext::onAddDocument(doc::Document* doc)
{
  m_lastSelectedDoc = static_cast<app::Document*>(doc);

  // We don't create views in batch mode.
  if (!App::instance()->isGui())
    return;

  // Add a new view for this document
  DocumentView* view = new DocumentView(m_lastSelectedDoc, DocumentView::Normal);

  // Add a tab with the new view for the document
  App::instance()->getMainWindow()->getWorkspace()->addView(view);

  setActiveView(view);
  view->getEditor()->setDefaultScroll();
}

void UIContext::onRemoveDocument(doc::Document* doc)
{
  if (doc == m_lastSelectedDoc)
    m_lastSelectedDoc = nullptr;

  // We don't destroy views in batch mode.
  if (isUiAvailable()) {
    Workspace* workspace = App::instance()->getMainWindow()->getWorkspace();
    DocumentViews docViews;

    // Collect all views related to the document.
    for (WorkspaceView* view : *workspace) {
      if (DocumentView* docView = dynamic_cast<DocumentView*>(view)) {
        if (docView->getDocument() == doc) {
          docViews.push_back(docView);
        }
      }
    }

    for (DocumentView* docView : docViews) {
      workspace->removeView(docView);
      delete docView;
    }
  }
}

void UIContext::onGetActiveSite(Site* site) const
{
  DocumentView* view = activeView();
  if (view) {
    view->getSite(site);
  }
  // Default/dummy site (maybe for batch/command line mode)
  else if (Document* doc = m_lastSelectedDoc) {
    site->document(doc);
    site->sprite(doc->sprite());
    site->layer(doc->sprite()->indexToLayer(LayerIndex(0)));
    site->frame(0);
  }
}

} // namespace app
