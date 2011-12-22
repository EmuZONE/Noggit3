#include "UITextureSwitcher.h"

#include "World.h"
#include "Selection.h"
#include "MapChunk.h"
#include "UITexture.h"
#include "UITexturingGUI.h"

void textureSwitcherClick (UIFrame* f, int id)
{
  // redirect to sender object.
  (static_cast<UITextureSwitcher*> (f->parent()))->setTexture (id);
}

UITextureSwitcher::UITextureSwitcher (World* world, float x, float y)
  : UICloseWindow (x, y, 130, 140, "Texture", true)
  , _world (world)
{
  const int textureSize (110);

  _texture = new UITexture ( 10
                           , 25
                           , textureSize
                           , textureSize
                           , "tileset\\generic\\black.blp"
                           );

  addChild (_texture);
}

void UITextureSwitcher::getTextures (nameEntry* lSelection)
{
  assert (lSelection);
  show();
}

void UITextureSwitcher::setTexture (size_t id)
{
  assert (id < 4);
  /*
  _world->overwriteTextureAtCurrentChunk ( xPos
                                         , zPos
                                         , _texture
                                         , UITexturingGUI::getSelectedTexture()
                                         );
  */
}

void UITextureSwitcher::setPosition (float x, float y)
{
  xPos = x;
  zPos = y;
}

