// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once


#include <noggit/MPQ.h>
#include <noggit/TextureManager.h>
#include <opengl/shader.hpp>

#include <string>
#include <vector>
#include <memory>

class liquid_render
{
public:
  liquid_render() = default;
  void prepare_draw ( opengl::scoped::use_program& water_shader
                    , int liquid_id
                    , int animtime
                    , bool wmo = false
                    );

  opengl::program const& shader_program() const
  {
    return program;
  }

private:
  void add_liquid_id(int liquid);

  int _current_liquid_id = -1;
  int _current_anim_time = 0;

  opengl::program program
    { { GL_VERTEX_SHADER
      , R"code(
#version 330 core

in vec4 position;
in vec2 tex_coord;
in float depth;

uniform mat4 model_view;
uniform mat4 projection;
uniform mat4 transform;

uniform int use_transform = int(0);

out float depth_;
out vec2 tex_coord_;

void main()
{
  depth_ = depth;
  tex_coord_ = tex_coord;

  if(use_transform == 1)
  {
    gl_Position = projection * model_view * transform * position;
  }
  else
  {
    gl_Position = projection * model_view * position;
  }
}
)code"
      }
    , { GL_FRAGMENT_SHADER
      , R"code(
#version 330 core

uniform sampler2D texture;
uniform vec4 ocean_color_light;
uniform vec4 ocean_color_dark;
uniform vec4 river_color_light;
uniform vec4 river_color_dark;
uniform float tex_repeat;

uniform int type;

in float depth_;
in vec2 tex_coord_;

out vec4 out_color;

void main()
{
  vec4 texel = texture2D (texture, tex_coord_ / tex_repeat);
  // lava || slime
  if(type == 2 || type == 3)
  {
    out_color = texel;
  }
  else
  {
    vec4 lerp = (type == 1)
              ? mix (ocean_color_light, ocean_color_dark, depth_) 
              : mix (river_color_light, river_color_dark, depth_);
              
    vec4 tResult = clamp (texel + lerp, 0.0, 1.0); //clamp shouldn't be needed
    vec4 oColor = clamp (texel + tResult, 0.0, 1.0);
    out_color = vec4 (oColor.rgb, lerp.a);
  }  
}
)code"
      }
    };

  std::map<int, int> _liquid_id_types;
  std::map<int, std::vector<scoped_blp_texture_reference>> _textures_by_liquid_id;
};
