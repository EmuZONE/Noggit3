// ModelInstance.h is part of Noggit3, licensed via GNU General Publiicense (version 3).
// Bernd Lörwald <bloerwald+noggit@googlemail.com>
// Tigurius <bstigurius@googlemail.com>

#ifndef MODELINSTANCE_H
#define MODELINSTANCE_H

#include <math/vector_3d.h>
#include <math/quaternion.h>

class Frustum;
class Model;
class World;

struct ENTRY_MDDF;

namespace noggit
{
  namespace mpq
  {
    class file;
  }
}

class ModelInstance
{
public:
  Model* model;

private:
  unsigned int nameID;
public:

  ::math::vector_3d pos;
  ::math::vector_3d dir;

  ::math::quaternion _wmo_doodad_rotation;

  //! \todo  Get this out and do somehow else.
  unsigned int d1;

  float w, sc;

  ::math::vector_3d lcol;
  ::math::vector_3d ldir;

  ModelInstance( World*, Model *m );
  ModelInstance( World*, Model *m, noggit::mpq::file* f );
  ModelInstance( World*, Model *m, ENTRY_MDDF *d );

  //! \todo This constructor should actually be one of a subclass.
  ModelInstance ( World* world
                , Model* model
                , const ::math::vector_3d& position
                , const ::math::quaternion& rotation
                , const float& scale
                , const ::math::vector_3d& lighting_color
                );
  ~ModelInstance();

  void draw (bool draw_fog, const Frustum& frustum);
  void draw_for_selection (const Frustum& frustum);

  void drawMapTile();
//  void drawHighlight();
  void draw2 ( const ::math::vector_3d& ofs
             , const float rot
             , const Frustum& frustum);
  void draw2Select ( const ::math::vector_3d& ofs
                   , const float rot
                   , const Frustum& frustum
                   );

  void resetDirection();

private:
  void draw_selection_indicator() const;

  World* _world;
};

#endif // MODELINSTANCE_H
