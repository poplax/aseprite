// Aseprite
// Copyright (C) 2001-2015  David Capello
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/ui/drop_down_button.h"

#include "app/modules/gui.h"
#include "app/ui/skin/button_icon_impl.h"
#include "app/ui/skin/skin_property.h"
#include "app/ui/skin/skin_theme.h"
#include "ui/theme.h"

namespace app {

using namespace app::skin;
using namespace ui;

DropDownButton::DropDownButton(const char* text)
  : HBox()
  , m_button(new Button(text))
  , m_dropDown(new Button(""))
{
  setup_look(m_button, LeftButtonLook);
  setup_look(m_dropDown, RightButtonLook);

  m_button->setExpansive(true);
  m_button->setAlign(JI_LEFT | JI_MIDDLE);
  m_button->Click.connect(&DropDownButton::onButtonClick, this);
  m_dropDown->Click.connect(&DropDownButton::onDropDownButtonClick, this);

  addChild(m_button);
  addChild(m_dropDown);

  child_spacing = 0;

  m_dropDown->setIconInterface
    (new ButtonIconImpl(static_cast<SkinTheme*>(m_dropDown->getTheme()),
                        PART_COMBOBOX_ARROW_DOWN,
                        PART_COMBOBOX_ARROW_DOWN_SELECTED,
                        PART_COMBOBOX_ARROW_DOWN_DISABLED,
                        JI_CENTER | JI_MIDDLE));
}

void DropDownButton::onButtonClick(Event& ev)
{
  // Fire "Click" signal.
  Click();
}

void DropDownButton::onDropDownButtonClick(Event& ev)
{
  DropDownClick();
}

} // namespace app
