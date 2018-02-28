// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/Animated.h> // Animation::M2Value
#include <noggit/Model.h>
#include <noggit/TextureManager.h>

#include <list>
#include <memory>
#include <vector>

class Bone;
class Model;
class ParticleSystem;
class RibbonEmitter;

struct Particle {
  math::vector_3d pos, speed, down, origin, dir;
  math::vector_3d  corners[4];
  //math::vector_3d tpos;
  float size, life, maxlife;
  unsigned int tile;
  math::vector_4d color;
};

typedef std::list<Particle> ParticleList;

class ParticleEmitter {
public:
  explicit ParticleEmitter() {}
  virtual ~ParticleEmitter() {}
  virtual Particle newParticle(ParticleSystem* sys, int anim, int time, int animtime, float w, float l, float spd, float var, float spr, float spr2) = 0;
};

class PlaneParticleEmitter : public ParticleEmitter {
public:
  explicit PlaneParticleEmitter() {}
  Particle newParticle(ParticleSystem* sys, int anim, int time, int animtime, float w, float l, float spd, float var, float spr, float spr2);
};

class SphereParticleEmitter : public ParticleEmitter {
public:
  explicit SphereParticleEmitter() {}
  Particle newParticle(ParticleSystem* sys, int anim, int time, int animtime, float w, float l, float spd, float var, float spr, float spr2);
};

struct TexCoordSet {
  math::vector_2d tc[4];
};

class ParticleSystem {
  Model *model;
  std::unique_ptr<ParticleEmitter> emitter;
  Animation::M2Value<float> speed, variation, spread, lat, gravity, lifespan, rate, areal, areaw, deacceleration;
  Animation::M2Value<uint8_t> enabled;
  math::vector_4d colors[3];
  float sizes[3];
  float mid, slowdown;
  math::vector_3d pos;
  uint16_t _texture_id;
  ParticleList particles;
  int blend, order, type;
  int manim, mtime;
  int manimtime;
  int rows, cols;
  std::vector<TexCoordSet> tiles;
  void initTile(math::vector_2d *tc, int num);
  bool billboard;

  float rem;
  //bool transform;

  // unknown parameters omitted for now ...
  Bone *parent;
  int32_t flags;

public:
  float tofs;

  ParticleSystem(Model*, const MPQFile& f, const ModelParticleEmitterDef &mta, int *globals);
  void update(float dt);

  void setup(int anim, int time, int animtime);
  void draw();

  friend class PlaneParticleEmitter;
  friend class SphereParticleEmitter;
};


struct RibbonSegment {
  math::vector_3d pos, up, back;
  float len, len0;
  RibbonSegment (::math::vector_3d pos_, float len_)
    : pos (pos_)
    , len (len_)
  {}
};

class RibbonEmitter {
  Model *model;

  Animation::M2Value<math::vector_3d> color;
  Animation::M2Value<float, int16_t> opacity;
  Animation::M2Value<float> above, below;

  Bone *parent;

  math::vector_3d pos;

  int manim, mtime;
  int seglen;
  float length;

  math::vector_3d tpos;
  math::vector_4d tcolor;
  float tabove, tbelow;

  std::vector<uint16_t> _texture_ids;
  std::vector<uint16_t> _material_ids;

  std::list<RibbonSegment> segs;

public:
  RibbonEmitter(Model*, const MPQFile &f, ModelRibbonEmitterDef const& mta, int *globals);
  void setup(int anim, int time, int animtime);
  void draw();
};
