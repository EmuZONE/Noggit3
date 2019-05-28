// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

// #define USEBLSFILES

#include <math/vector_2d.hpp>
#include <math/vector_3d.hpp>
#include <noggit/MPQ.h>
#include <noggit/TextureManager.h>
#include <noggit/liquid_render.hpp>
#include <opengl/scoped.hpp>

#include <boost/optional.hpp>

#include <memory>

struct WMOMaterial {
  int32_t flags;
  int32_t specular;
  int32_t transparent; // Blending: 0 for opaque, 1 for transparent
  int32_t texture_offset_1; // Start position for the first texture filename in the MOTX data block
  uint32_t col1; // color
  int32_t d3; // flag
  int32_t texture_offset_2; // Start position for the second texture filename in the MOTX data block
  uint32_t col2; // color
  int32_t d4; // flag
  uint32_t col3;
  float f2;
  float diffColor[3];
  uint32_t texture1; // this is the first texture object. of course only in RAM.
  uint32_t texture2; // this is the second texture object.
};

struct WMOLiquidHeader {
  int32_t X, Y, A, B;
  math::vector_3d pos;
  int16_t type;
};

struct LiquidVertex {
  unsigned char c[4];
  float h;
};

class wmo_liquid
{
public:
  wmo_liquid(MPQFile* f, WMOLiquidHeader const& header, WMOMaterial const& mat, bool indoor);
  wmo_liquid(wmo_liquid const& other);

  void draw ( math::matrix_4x4 const& model_view
            , math::matrix_4x4 const& projection
            , math::matrix_4x4 const& transform
            , math::vector_4d const& ocean_color_light
            , math::vector_4d const& ocean_color_dark
            , math::vector_4d const& river_color_light
            , math::vector_4d const& river_color_dark
            , liquid_render& render
            , int animtime
            );

  void upload(opengl::scoped::use_program& water_shader);

private:
  int initGeometry(MPQFile* f);

  math::vector_3d pos;
  float texRepeats;
  bool mTransparency;
  int xtiles, ytiles;

  std::vector<float> depths;
  std::vector<math::vector_2d> tex_coords;
  std::vector<math::vector_3d> vertices;
  std::vector<std::uint16_t> indices;

  int _indices_count;

  bool _uploaded = false;

  opengl::scoped::deferred_upload_buffers<4> _buffer;
  GLuint const& _indices_buffer = _buffer[0];
  GLuint const& _vertices_buffer = _buffer[1];
  GLuint const& _depth_buffer = _buffer[2];
  GLuint const& _tex_coord_buffer = _buffer[3];
  opengl::scoped::deferred_upload_vertex_arrays<1> _vertex_array;
  GLuint const& _vao = _vertex_array[0];
};
