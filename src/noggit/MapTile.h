// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <math/ray.hpp>
#include <noggit/MapChunk.h>
#include <noggit/MapHeaders.h>
#include <noggit/Selection.h>
#include <noggit/TileWater.hpp>
#include <noggit/tile_index.hpp>
#include <noggit/tool_enums.hpp>
#include <opengl/shader.fwd.hpp>
#include <noggit/Misc.h>

#include <map>
#include <string>
#include <vector>

namespace math
{
  class frustum;
  struct vector_3d;
}

class World;

class MapTile : public AsyncObject
{

public:
	MapTile(int x0, int z0, const std::string& pFilename, bool pBigAlpha, bool pLoadModels, World*);
  ~MapTile();

  void finishLoading();

  //! \todo on destruction, unload ModelInstances and WMOInstances on this tile:
  // a) either keep up the information what tiles the instances are on at all times
  //    (even while moving), to then check if all tiles it was on were unloaded, or
  // b) do the reference count lazily by iterating over all instances and checking
  //    what MapTiles they span. if any of those tiles is still loaded, keep it,
  //    otherwise remove it.
  //
  // I think b) is easier. It only requires
  // `std::set<C2iVector> XInstance::spanning_tiles() const` followed by
  // `if_none (isTileLoaded (x, y)): unload instance`, which is way easier than
  // constantly updating the reference counters.
  // Note that both approaches do not cover the issue that the instance might not
  // be saved to any tile, thus the movement might have been lost.

	//! \brief Get the maximum height of terrain on this map tile.
	float getMaxHeight();

  void convert_alphamap(bool to_big_alpha);

  //! \brief Get chunk for sub offset x,z.
  MapChunk* getChunk(unsigned int x, unsigned int z);
  //! \todo map_index style iterators
  std::vector<MapChunk*> chunks_in_range (math::vector_3d const& pos, float radius) const;

  const tile_index index;
  float xbase, zbase;

  int changed;

  void draw ( math::frustum const& frustum
            , opengl::scoped::use_program& mcnk_shader
            , const float& cull_distance
            , const math::vector_3d& camera
            , bool show_unpaintable_chunks
            , bool draw_paintability_overlay
            , bool draw_chunk_flag_overlay
            , bool draw_areaid_overlay
            , std::map<int, misc::random_color>& area_id_colors
            , boost::optional<selection_type> selection
            , int animtime
            , display_mode display
            );
  void intersect (math::ray const&, selection_result*) const;
  void drawWater ( math::frustum const& frustum
                 , const float& cull_distance
                 , const math::vector_3d& camera
                 , liquid_render& render
                 , opengl::scoped::use_program& water_shader
                 , int animtime
                 , int layer
                 , display_mode display
                 );

  void drawMFBO (opengl::scoped::use_program&);

  bool GetVertex(float x, float z, math::vector_3d *V);

  void saveTile(bool saveAllModels, World*);
	void CropWater();

  bool isTile(int pX, int pZ);

  virtual async_priority loading_priority() const
  {
    return async_priority::high;
  }

  bool has_model(uint32_t uid) const
  {
    return std::find(uids.begin(), uids.end(), uid) != uids.end();
  }

  void remove_model(uint32_t uid);
  void add_model(uint32_t uid) { uids.push_back(uid); }

  TileWater Water;
private:

  // MFBO:
  math::vector_3d mMinimumValues[3 * 3];
  math::vector_3d mMaximumValues[3 * 3];

  // MHDR:
  int mFlags;
  bool mBigAlpha;

  // Data to be loaded and later unloaded.
  std::vector<std::string> mTextureFilenames;
  std::vector<std::string> mModelFilenames;
  std::vector<std::string> mWMOFilenames;
  
  std::vector<uint32_t> uids;

  std::unique_ptr<MapChunk> mChunks[16][16];
  std::vector<TileWater*> chunksLiquids; //map chunks liquids for old style water render!!! (Not MH2O)

  bool _load_models;
  World* _world;

  friend class MapChunk;
  friend class TextureSet;
};
