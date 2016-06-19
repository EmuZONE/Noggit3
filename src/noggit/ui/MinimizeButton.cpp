// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/ui/MinimizeButton.h>

#include <string>

UIMinimizeButton::UIMinimizeButton(float pWidth, std::function<void()> on_hide)
  : UIButton(pWidth - 29.0f, 1.0f, 30.0f, 30.0f, "Interface\\Buttons\\UI-Panel-MinimizeButton-Up.blp", "Interface\\Buttons\\UI-Panel-MinimizeButton-Down.blp")
  , _on_hide (on_hide)
{
}

UIFrame::Ptr UIMinimizeButton::processLeftClick(float /*mx*/, float /*my*/)
{
  clicked = true;
  if (parent())
  {
    parent()->hide();
    _on_hide();
  }
  return this;
}