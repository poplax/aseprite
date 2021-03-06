// Aseprite
// Copyright (C) 2001-2015  David Capello
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/ui/editor/standby_state.h"

#include "app/app.h"
#include "app/color_picker.h"
#include "app/commands/commands.h"
#include "app/commands/params.h"
#include "app/ini_file.h"
#include "app/settings/settings.h"
#include "app/tools/ink.h"
#include "app/tools/pick_ink.h"
#include "app/tools/tool.h"
#include "app/ui/editor/drawing_state.h"
#include "app/ui/editor/editor.h"
#include "app/ui/editor/editor_customization_delegate.h"
#include "app/ui/editor/handle_type.h"
#include "app/ui/editor/moving_cel_state.h"
#include "app/ui/editor/moving_pixels_state.h"
#include "app/ui/editor/pixels_movement.h"
#include "app/ui/editor/scrolling_state.h"
#include "app/ui/editor/tool_loop_impl.h"
#include "app/ui/editor/transform_handles.h"
#include "app/ui/editor/zooming_state.h"
#include "app/ui/status_bar.h"
#include "app/ui_context.h"
#include "app/util/new_image_from_mask.h"
#include "doc/layer.h"
#include "doc/mask.h"
#include "doc/sprite.h"
#include "fixmath/fixmath.h"
#include "gfx/rect.h"
#include "ui/alert.h"
#include "ui/message.h"
#include "ui/system.h"
#include "ui/view.h"

namespace app {

using namespace ui;

static CursorType rotated_size_cursors[] = {
  kSizeECursor,
  kSizeNECursor,
  kSizeNCursor,
  kSizeNWCursor,
  kSizeWCursor,
  kSizeSWCursor,
  kSizeSCursor,
  kSizeSECursor
};

static CursorType rotated_rotate_cursors[] = {
  kRotateECursor,
  kRotateNECursor,
  kRotateNCursor,
  kRotateNWCursor,
  kRotateWCursor,
  kRotateSWCursor,
  kRotateSCursor,
  kRotateSECursor
};

#ifdef _MSC_VER
#pragma warning(disable:4355) // warning C4355: 'this' : used in base member initializer list
#endif

StandbyState::StandbyState()
  : m_decorator(new Decorator(this))
{
}

StandbyState::~StandbyState()
{
  delete m_decorator;
}

void StandbyState::onAfterChangeState(Editor* editor)
{
  editor->setDecorator(m_decorator);
}

void StandbyState::onCurrentToolChange(Editor* editor)
{
  //tools::Tool* currentTool = editor->getCurrentEditorTool();

  // If the user change from a selection tool to a non-selection tool,
  // or viceversa, we've to show or hide the transformation handles.
  // TODO Compare the ink (isSelection()) of the previous tool with
  // the new one.
  editor->invalidate();
}

void StandbyState::onQuickToolChange(Editor* editor)
{
  editor->invalidate();
}

bool StandbyState::checkForScroll(Editor* editor, MouseMessage* msg)
{
  tools::Ink* clickedInk = editor->getCurrentEditorInk();

  // Start scroll loop
  if (msg->middle() || clickedInk->isScrollMovement()) { // TODO msg->middle() should be customizable
    EditorStatePtr newState(new ScrollingState());
    editor->setState(newState);

    newState->onMouseDown(editor, msg);
    return true;
  }
  else
    return false;
}

bool StandbyState::checkForZoom(Editor* editor, MouseMessage* msg)
{
  tools::Ink* clickedInk = editor->getCurrentEditorInk();

  // Start scroll loop
  if (clickedInk->isZoom()) {
    EditorStatePtr newState(new ZoomingState());
    editor->setState(newState);

    newState->onMouseDown(editor, msg);
    return true;
  }
  else
    return false;
}

bool StandbyState::onMouseDown(Editor* editor, MouseMessage* msg)
{
  if (editor->hasCapture())
    return true;

  UIContext* context = UIContext::instance();
  tools::Ink* clickedInk = editor->getCurrentEditorInk();
  Site site;
  editor->getSite(&site);
  app::Document* document = static_cast<app::Document*>(site.document());
  Layer* layer = site.layer();

  // When an editor is clicked the current view is changed.
  context->setActiveView(editor->getDocumentView());

  // Start scroll loop
  if (checkForScroll(editor, msg) || checkForZoom(editor, msg))
    return true;

  // Move cel X,Y coordinates
  if (clickedInk->isCelMovement()) {
    // Handle "Auto Select Layer"
    if (editor->isAutoSelectLayer()) {
      gfx::Point cursor = editor->screenToEditor(msg->position());

      ColorPicker picker;
      picker.pickColor(site, cursor, ColorPicker::FromComposition);

      if (layer != picker.layer()) {
        layer = picker.layer();
        if (layer) {
          editor->setLayer(layer);
          editor->flashCurrentLayer();
        }
      }
    }

    if ((layer) &&
        (layer->type() == ObjectType::LayerImage)) {
      // TODO we should be able to move the `Background' with tiled mode
      if (layer->isBackground()) {
        StatusBar::instance()->showTip(1000,
          "The background layer cannot be moved");
      }
      else if (!layer->isVisible()) {
        StatusBar::instance()->showTip(1000,
          "Layer '%s' is hidden", layer->name().c_str());
      }
      else if (!layer->isMovable() || !layer->isEditable()) {
        StatusBar::instance()->showTip(1000,
          "Layer '%s' is locked", layer->name().c_str());
      }
      else if (!layer->cel(editor->frame())) {
        StatusBar::instance()->showTip(1000,
          "Cel is empty, nothing to move");
      }
      else {
        // Change to MovingCelState
        editor->setState(EditorStatePtr(new MovingCelState(editor, msg)));
      }
    }

    return true;
  }

  // Call the eyedropper command
  if (clickedInk->isEyedropper()) {
    callEyedropper(editor);
    return true;
  }

  if (clickedInk->isSelection()) {
    // Transform selected pixels
    if (editor->isActive() && document->isMaskVisible() && m_decorator->getTransformHandles(editor)) {
      TransformHandles* transfHandles = m_decorator->getTransformHandles(editor);

      // Get the handle covered by the mouse.
      HandleType handle = transfHandles->getHandleAtPoint(editor,
        msg->position(),
        document->getTransformation());

      if (handle != NoHandle) {
        int x, y, opacity;
        Image* image = site.image(&x, &y, &opacity);
        if (layer && image) {
          if (!layer->isEditable()) {
            StatusBar::instance()->showTip(1000,
              "Layer '%s' is locked", layer->name().c_str());
            return true;
          }

          // Change to MovingPixelsState
          transformSelection(editor, msg, handle);
        }
        return true;
      }
    }

    // Move selected pixels
    if (layer && editor->isInsideSelection() && msg->left()) {
      if (!layer->isEditable()) {
        StatusBar::instance()->showTip(1000,
          "Layer '%s' is locked", layer->name().c_str());
        return true;
      }

      // Change to MovingPixelsState
      transformSelection(editor, msg, MoveHandle);
      return true;
    }
  }

  // Start the Tool-Loop
  if (layer) {
    tools::ToolLoop* toolLoop = create_tool_loop(editor, context);
    if (toolLoop) {
      EditorStatePtr newState(new DrawingState(toolLoop));
      editor->setState(newState);

      static_cast<DrawingState*>(newState.get())
        ->initToolLoop(editor, msg);
    }
    return true;
  }

  return true;
}

bool StandbyState::onMouseUp(Editor* editor, MouseMessage* msg)
{
  editor->releaseMouse();
  return true;
}

bool StandbyState::onMouseMove(Editor* editor, MouseMessage* msg)
{
  // We control eyedropper tool from here. TODO move this to another place
  if (msg->left() || msg->right()) {
    tools::Ink* clickedInk = editor->getCurrentEditorInk();
    if (clickedInk->isEyedropper())
      callEyedropper(editor);
  }

  editor->moveDrawingCursor();
  editor->updateStatusBar();
  return true;
}

bool StandbyState::onSetCursor(Editor* editor)
{
  tools::Ink* ink = editor->getCurrentEditorInk();
  if (ink) {
    // If the current tool change selection (e.g. rectangular marquee, etc.)
    if (ink->isSelection()) {
      // See if the cursor is in some selection handle.
      if (m_decorator->onSetCursor(editor))
        return true;

      // Move pixels
      if (editor->isInsideSelection()) {
        EditorCustomizationDelegate* customization = editor->getCustomizationDelegate();

        editor->hideDrawingCursor();

        if (customization && customization->isCopySelectionKeyPressed())
          ui::set_mouse_cursor(kArrowPlusCursor);
        else
          ui::set_mouse_cursor(kMoveCursor);

        return true;
      }
    }
    else if (ink->isEyedropper()) {
      editor->hideDrawingCursor();
      ui::set_mouse_cursor(kEyedropperCursor);
      return true;
    }
    else if (ink->isZoom()) {
      editor->hideDrawingCursor();
      ui::set_mouse_cursor(kMagnifierCursor);
      return true;
    }
    else if (ink->isScrollMovement()) {
      editor->hideDrawingCursor();
      ui::set_mouse_cursor(kScrollCursor);
      return true;
    }
    else if (ink->isCelMovement()) {
      editor->hideDrawingCursor();
      ui::set_mouse_cursor(kMoveCursor);
      return true;
    }
    else if (ink->isSlice()) {
      ui::set_mouse_cursor(kNoCursor);
      editor->showDrawingCursor();
      return true;
    }
  }

  // Draw
  if (editor->canDraw()) {
    ui::set_mouse_cursor(kNoCursor);
    editor->showDrawingCursor();
  }
  // Forbidden
  else {
    editor->hideDrawingCursor();
    ui::set_mouse_cursor(kForbiddenCursor);
  }

  return true;
}

bool StandbyState::onKeyDown(Editor* editor, KeyMessage* msg)
{
  return false;
}

bool StandbyState::onKeyUp(Editor* editor, KeyMessage* msg)
{
  return false;
}

bool StandbyState::onUpdateStatusBar(Editor* editor)
{
  tools::Ink* ink = editor->getCurrentEditorInk();
  const Sprite* sprite = editor->sprite();
  gfx::Point spritePos = editor->screenToEditor(ui::get_mouse_position());

  if (!sprite) {
    StatusBar::instance()->clearText();
  }
  // For eye-dropper
  else if (ink->isEyedropper()) {
    bool grabAlpha = UIContext::instance()->settings()->getGrabAlpha();
    ColorPicker picker;
    picker.pickColor(editor->getSite(),
      spritePos,
      grabAlpha ?
      ColorPicker::FromActiveLayer:
      ColorPicker::FromComposition);

    char buf[256];
    sprintf(buf, "- Pos %d %d", spritePos.x, spritePos.y);

    StatusBar::instance()->showColor(0, buf, picker.color(), picker.alpha());
  }
  else {
    Mask* mask =
      (editor->document()->isMaskVisible() ?
       editor->document()->mask(): NULL);

    StatusBar::instance()->setStatusText(0,
      "Pos %d %d, Size %d %d, Frame %d [%d msecs]",
      spritePos.x, spritePos.y,
      (mask ? mask->bounds().w: sprite->width()),
      (mask ? mask->bounds().h: sprite->height()),
      editor->frame()+1,
      sprite->frameDuration(editor->frame()));
  }

  return true;
}

gfx::Transformation StandbyState::getTransformation(Editor* editor)
{
  return editor->document()->getTransformation();
}

void StandbyState::startSelectionTransformation(Editor* editor, const gfx::Point& move)
{
  transformSelection(editor, NULL, NoHandle);

  if (MovingPixelsState* movingPixels = dynamic_cast<MovingPixelsState*>(editor->getState().get()))
    movingPixels->translate(move);
}

void StandbyState::transformSelection(Editor* editor, MouseMessage* msg, HandleType handle)
{
  try {
    EditorCustomizationDelegate* customization = editor->getCustomizationDelegate();
    Document* document = editor->document();
    base::UniquePtr<Image> tmpImage(new_image_from_mask(editor->getSite()));
    gfx::Point origin = document->mask()->bounds().getOrigin();
    int opacity = 255;
    PixelsMovementPtr pixelsMovement(
      new PixelsMovement(UIContext::instance(),
        editor->getSite(),
        tmpImage, origin, opacity,
        "Transformation"));

    // If the Ctrl key is pressed start dragging a copy of the selection
    if (customization && customization->isCopySelectionKeyPressed())
      pixelsMovement->copyMask();
    else
      pixelsMovement->cutMask();

    editor->setState(EditorStatePtr(new MovingPixelsState(editor, msg, pixelsMovement, handle)));
  }
  catch (const LockedDocumentException&) {
    // Other editor is locking the document.

    // TODO steal the PixelsMovement of the other editor and use it for this one.
    StatusBar::instance()->showTip(1000, "The sprite is locked in other editor");
    ui::set_mouse_cursor(kForbiddenCursor);
  }
  catch (const std::bad_alloc&) {
    StatusBar::instance()->showTip(1000, "Not enough memory to transform the selection");
    ui::set_mouse_cursor(kForbiddenCursor);
  }
}

void StandbyState::callEyedropper(Editor* editor)
{
  tools::Ink* clickedInk = editor->getCurrentEditorInk();
  if (!clickedInk->isEyedropper())
    return;

  Command* eyedropper_cmd =
    CommandsModule::instance()->getCommandByName(CommandId::Eyedropper);
  bool fg = (static_cast<tools::PickInk*>(clickedInk)->target() == tools::PickInk::Fg);

  Params params;
  params.set("target", fg ? "foreground": "background");

  UIContext::instance()->executeCommand(eyedropper_cmd, params);
}

//////////////////////////////////////////////////////////////////////
// Decorator

StandbyState::Decorator::Decorator(StandbyState* standbyState)
  : m_transfHandles(NULL)
  , m_standbyState(standbyState)
{
}

StandbyState::Decorator::~Decorator()
{
  delete m_transfHandles;
}

TransformHandles* StandbyState::Decorator::getTransformHandles(Editor* editor)
{
  if (!m_transfHandles)
    m_transfHandles = new TransformHandles();

  return m_transfHandles;
}

bool StandbyState::Decorator::onSetCursor(Editor* editor)
{
  if (!editor->isActive() ||
      !editor->document()->isMaskVisible())
    return false;

  const gfx::Transformation transformation(m_standbyState->getTransformation(editor));
  TransformHandles* tr = getTransformHandles(editor);
  HandleType handle = tr->getHandleAtPoint(editor,
    ui::get_mouse_position(), transformation);

  CursorType newCursor = kArrowCursor;

  switch (handle) {
    case ScaleNWHandle:         newCursor = kSizeNWCursor; break;
    case ScaleNHandle:          newCursor = kSizeNCursor; break;
    case ScaleNEHandle:         newCursor = kSizeNECursor; break;
    case ScaleWHandle:          newCursor = kSizeWCursor; break;
    case ScaleEHandle:          newCursor = kSizeECursor; break;
    case ScaleSWHandle:         newCursor = kSizeSWCursor; break;
    case ScaleSHandle:          newCursor = kSizeSCursor; break;
    case ScaleSEHandle:         newCursor = kSizeSECursor; break;
    case RotateNWHandle:        newCursor = kRotateNWCursor; break;
    case RotateNHandle:         newCursor = kRotateNCursor; break;
    case RotateNEHandle:        newCursor = kRotateNECursor; break;
    case RotateWHandle:         newCursor = kRotateWCursor; break;
    case RotateEHandle:         newCursor = kRotateECursor; break;
    case RotateSWHandle:        newCursor = kRotateSWCursor; break;
    case RotateSHandle:         newCursor = kRotateSCursor; break;
    case RotateSEHandle:        newCursor = kRotateSECursor; break;
    case PivotHandle:           newCursor = kHandCursor; break;
    default:
      return false;
  }

  // Adjust the cursor depending the current transformation angle.
  fixmath::fixed angle = fixmath::ftofix(128.0 * transformation.angle() / PI);
  angle = fixmath::fixadd(angle, fixmath::itofix(16));
  angle &= (255<<16);
  angle >>= 16;
  angle /= 32;

  if (newCursor >= kSizeNCursor && newCursor <= kSizeNWCursor) {
    size_t num = sizeof(rotated_size_cursors) / sizeof(rotated_size_cursors[0]);
    size_t c;
    for (c=num-1; c>0; --c)
      if (rotated_size_cursors[c] == newCursor)
        break;

    newCursor = rotated_size_cursors[(c+angle) % num];
  }
  else if (newCursor >= kRotateNCursor && newCursor <= kRotateNWCursor) {
    size_t num = sizeof(rotated_rotate_cursors) / sizeof(rotated_rotate_cursors[0]);
    size_t c;
    for (c=num-1; c>0; --c)
      if (rotated_rotate_cursors[c] == newCursor)
        break;

    newCursor = rotated_rotate_cursors[(c+angle) % num];
  }

  // Hide the drawing cursor (just in case) and show the new system cursor.
  editor->hideDrawingCursor();
  ui::set_mouse_cursor(newCursor);
  return true;
}

void StandbyState::Decorator::preRenderDecorator(EditorPreRender* render)
{
  // Do nothing
}

void StandbyState::Decorator::postRenderDecorator(EditorPostRender* render)
{
  Editor* editor = render->getEditor();

  // Draw transformation handles (if the mask is visible and isn't frozen).
  if (editor->isActive() &&
      editor->editorFlags() & Editor::kShowMask &&
      editor->document()->isMaskVisible() &&
      !editor->document()->mask()->isFrozen()) {
    // And draw only when the user has a selection tool as active tool.
    tools::Ink* ink = editor->getCurrentEditorInk();

    if (ink->isSelection())
      getTransformHandles(editor)->drawHandles(editor,
        m_standbyState->getTransformation(editor));
  }
}

} // namespace app
