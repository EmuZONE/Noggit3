// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/Log.h>
#include <noggit/Model.h>
#include <noggit/ModelInstance.h>
#include <noggit/TextureManager.h> // TextureManager, Texture
#include <noggit/World.h>
#include <opengl/matrix.hpp>
#include <opengl/scoped.hpp>
#include <opengl/shader.hpp>

#include <algorithm>
#include <cassert>
#include <map>
#include <sstream>
#include <string>

Model::Model(const std::string& filename)
  : _filename(filename)
  , _finished_upload(false)
{
  memset(&header, 0, sizeof(ModelHeader));

  finished = false;

  //! \note hack: we currently would never load them otherwise
  finishLoading();
}

void Model::finishLoading()
{
  MPQFile f(_filename);

  if (f.isEof())
  {
    LogError << "Error loading file \"" << _filename << "\". Aborting to load model." << std::endl;
    finished = true;
    return;
  }

  memcpy(&header, f.getBuffer(), sizeof(ModelHeader));

  // blend mode override
  if (header.Flags & 8)
  {
    uint32_t ofs_blend_override, n_blend_override;
    uint16_t blend;

    f.seek(sizeof(ModelHeader));
    
    f.read(&n_blend_override, 4);
    f.read(&ofs_blend_override, 4);

    f.seek(ofs_blend_override);    

    for (int i = 0; i < n_blend_override; ++i)
    {
      f.read(&blend, 2);
      blend_override.push_back(blend);
    }

    f.seek(0);
  }


  _vertex_box_points = misc::box_points ( misc::transform_model_box_coords(header.bounding_box_min)
                                        , misc::transform_model_box_coords(header.bounding_box_max)
                                        );

  animated = isAnimated(f);  // isAnimated will set animGeometry and animTextures

  trans = 1.0f;
  anim = 0;

  rad = header.bounding_box_radius;

  if (header.nGlobalSequences)
  {
    _global_sequences.resize (header.nGlobalSequences);
    memcpy(_global_sequences.data(), (f.getBuffer() + header.ofsGlobalSequences), header.nGlobalSequences * 4);
  }

  //! \todo  This takes a biiiiiit long. Have a look at this.
  initCommon(f);

  if (animated)
  {
    initAnimated (f);
  }

  f.close();

  finished = true;
}

Model::~Model()
{
  LogDebug << "Unloading model \"" << _filename << "\"." << std::endl;

  _textures.clear();
  _textureFilenames.clear();

  gl.deleteVertexArray (1, &_vao);
  gl.deleteBuffers (1, &_transform_buffer);
  gl.deleteBuffers (1, &_vertices_buffer);

  gl.deleteVertexArray (1, &_box_vao);
  gl.deleteBuffers (1, &_box_vbo);
}


bool Model::isAnimated(const MPQFile& f)
{
  // see if we have any animated bones
  ModelBoneDef const* bo = reinterpret_cast<ModelBoneDef const*>(f.getBuffer() + header.ofsBones);

  animGeometry = false;
  animBones = false;
  mPerInstanceAnimation = false;

  ModelVertex const* verts = reinterpret_cast<ModelVertex const*>(f.getBuffer() + header.ofsVertices);
  for (size_t i = 0; i<header.nVertices && !animGeometry; ++i) {
    for (size_t b = 0; b<4; b++) {
      if (verts[i].weights[b]>0) {
        ModelBoneDef const& bb = bo[verts[i].bones[b]];
        if (bb.translation.type || bb.rotation.type || bb.scaling.type || (bb.flags & 8)) {
          if (bb.flags & 8) {
            // if we have billboarding, the model will need per-instance animation
            mPerInstanceAnimation = true;
          }
          animGeometry = true;
          break;
        }
      }
    }
  }

  if (animGeometry || header.nParticleEmitters || header.nRibbonEmitters || header.nLights || header.nCameras)
  {
    animBones = true;
  }
  else
  {
    for (size_t i = 0; i<header.nBones; ++i)
    {
      ModelBoneDef const& bb = bo[i];
      if (bb.translation.type || bb.rotation.type || bb.scaling.type)
      {
        animBones = true;
        break;
      }
    }
  }

  animTextures = header.nTexAnims > 0;

  // animated colors
  if (header.nColors)
  {
    ModelColorDef const* cols = reinterpret_cast<ModelColorDef const*>(f.getBuffer() + header.ofsColors);
    for (size_t i = 0; i<header.nColors; ++i)
    {
      if (cols[i].color.type != 0 || cols[i].opacity.type != 0)
      {
        return true;
      }
    }
  }

  // animated opacity
  if (header.nTransparency)
  {
    ModelTransDef const* trs = reinterpret_cast<ModelTransDef const*>(f.getBuffer() + header.ofsTransparency);
    for (size_t i = 0; i<header.nTransparency; ++i)
    {
      if (trs[i].trans.type != 0)
      {
        return true;
      }
    }
  }

  // guess not...
  return animGeometry || animTextures || animBones;
}


math::vector_3d fixCoordSystem(math::vector_3d v)
{
  return math::vector_3d(v.x, v.z, -v.y);
}

namespace
{
  math::vector_3d fixCoordSystem2(math::vector_3d v)
  {
    return math::vector_3d(v.x, v.z, v.y);
  }

  math::quaternion fixCoordSystemQuat(math::quaternion v)
  {
    return math::quaternion(-v.x, -v.z, v.y, v.w);
  }
}


void Model::initCommon(const MPQFile& f)
{
  // vertices, normals, texcoords
  ModelVertex const* vertices = reinterpret_cast<ModelVertex const*> (f.getBuffer() + header.ofsVertices);

  _vertices.resize (header.nVertices);
  _vertices_parameters.resize (header.nVertices);

  for (size_t i (0); i < header.nVertices; ++i)
  {
    _vertices[i].position = fixCoordSystem (vertices[i].pos);
    _vertices[i].normal = fixCoordSystem (vertices[i].normal);
    memcpy(_vertices[i].texcoords, vertices[i].texcoords, 2 * sizeof(::math::vector_2d));

    memcpy (_vertices_parameters[i].bones, vertices[i].bones, 4 * sizeof (uint8_t));
    memcpy (_vertices_parameters[i].weights, vertices[i].weights, 4 * sizeof (uint8_t));
  }

  if (!animGeometry)
  {
    _current_vertices.swap (_vertices);
  }

  // textures
  ModelTextureDef const* texdef = reinterpret_cast<ModelTextureDef const*>(f.getBuffer() + header.ofsTextures);
  _textureFilenames.resize(header.nTextures);
  _specialTextures.resize(header.nTextures);

  for (size_t i = 0; i < header.nTextures; ++i)
  {
    if (texdef[i].type == 0)
    {
      _specialTextures[i] = -1;
      _textureFilenames[i] = std::string(f.getBuffer() + texdef[i].nameOfs, texdef[i].nameLen);
    }
    else
    {
#ifndef NO_REPLACIBLE_TEXTURES_HACK
      _specialTextures[i] = -1;
      _textureFilenames[i] = "tileset/generic/black.blp";
#else
      //! \note special texture - only on characters and such... Noggit should not even render these.
      //! \todo Check if this is actually correct. Or just remove it.

      _specialTextures[i] = texdef[i].type;

      if (texdef[i].type == 3)
      {
        _textureFilenames[i] = "Item\\ObjectComponents\\Weapon\\ArmorReflect4.BLP";
        // a fix for weapons with type-3 textures.
        _replaceTextures.emplace (texdef[i].type, _textureFilenames[i]);
      }
#endif
    }
  }

  // init colors
  if (header.nColors) {
    ModelColorDef const* colorDefs = reinterpret_cast<ModelColorDef const*>(f.getBuffer() + header.ofsColors);
    for (size_t i = 0; i < header.nColors; ++i)
    {
      _colors.emplace_back (f, colorDefs[i], _global_sequences.data());
    }
  }

  // init transparency
  int16_t const* transLookup = reinterpret_cast<int16_t const*>(f.getBuffer() + header.ofsTransparencyLookup);
  _transparency_lookup = std::vector<int16_t>(transLookup, transLookup + header.nTransparencyLookup);

  if (header.nTransparency) {
    ModelTransDef const* trDefs = reinterpret_cast<ModelTransDef const*>(f.getBuffer() + header.ofsTransparency);
    for (size_t i = 0; i < header.nTransparency; ++i)
    {
      _transparency.emplace_back (f, trDefs[i], _global_sequences.data());
    }
  }


  // just use the first LOD/view

  if (header.nViews > 0) {
    // indices - allocate space, too
    std::string lodname = _filename.substr(0, _filename.length() - 3);
    lodname.append("00.skin");
    MPQFile g(lodname.c_str());
    if (g.isEof()) {
      LogError << "loading skinfile " << lodname << std::endl;
      g.close();
      return;
    }
    ModelView const* view = reinterpret_cast<ModelView const*>(g.getBuffer());

    uint16_t const* indexLookup = reinterpret_cast<uint16_t const*>(g.getBuffer() + view->ofs_index);
    uint16_t const* triangles = reinterpret_cast<uint16_t const*>(g.getBuffer() + view->ofs_triangle);

    _indices.resize (view->n_triangle);

    for (size_t i (0); i < _indices.size(); ++i) {
      _indices[i] = indexLookup[triangles[i]];
    }

    // render ops
    ModelGeoset const* model_geosets = reinterpret_cast<ModelGeoset const*>(g.getBuffer() + view->ofs_submesh);
    ModelTexUnit const* texture_unit = reinterpret_cast<ModelTexUnit const*>(g.getBuffer() + view->ofs_texture_unit);
    
    uint16_t const* texlookup = reinterpret_cast<uint16_t const*>(f.getBuffer() + header.ofsTexLookup);
    _texture_lookup = std::vector<uint16_t>(texlookup, texlookup + header.nTexLookup);

    uint16_t const* texanimlookup = reinterpret_cast<uint16_t const*>(f.getBuffer() + header.ofsTexAnimLookup);


    int16_t const* texunitlookup = reinterpret_cast<int16_t const*>(f.getBuffer() + header.ofsTexUnitLookup);
    _texture_unit_lookup = std::vector<uint16_t>(texunitlookup, texunitlookup + header.nTexUnitLookup);

    showGeosets.resize (view->n_submesh);
    for (size_t i = 0; i<view->n_submesh; ++i) 
    {
      showGeosets[i] = true;
    }

    
    ModelRenderFlags const* renderFlags = reinterpret_cast<ModelRenderFlags const*>(f.getBuffer() + header.ofsRenderFlags);
    _render_flags = std::vector<ModelRenderFlags>(renderFlags, renderFlags + header.nRenderFlags);

    for (size_t j = 0; j<view->n_texture_unit; j++) 
    {
      size_t geoset = texture_unit[j].submesh;

      ModelRenderPass new_pass(texture_unit[j], this);
      new_pass.ordering_thingy = model_geosets[geoset].BoundingBox[0].x;

      new_pass.index_start = model_geosets[geoset].istart;
      new_pass.index_count = model_geosets[geoset].icount;
      new_pass.vertex_start = model_geosets[geoset].vstart;
      new_pass.vertex_end = new_pass.vertex_start + model_geosets[geoset].vcount;
      
      _render_passes.push_back(new_pass);
    }

    g.close();

    fix_shader_id_blend_override();
    fix_shader_id_layer();
    compute_pixel_shader_ids();
    
    // transparent parts come later
    std::sort(_render_passes.begin(), _render_passes.end());
  }  
}

void Model::fix_shader_id_blend_override()
{
  for (auto& pass : _render_passes)
  {
    int shader = 0;
    bool blend_mode_override = (header.Flags & 8);

    if (!blend_mode_override)
    {
      shader = _render_flags[pass.renderflag_index].blend ? 1 : 0;

      if (pass.texture_count > 2)
      {
        shader |= 8;
      }

      shader <<= 4;

      if (pass.texture_count == 1)
      {
        shader |= 0x4000;
      }
    }
    else
    {
      uint16_t runtime_shader_val[2] = { 0, 0 };

      for (int i = 0; i < pass.texture_count; ++i)
      {
        uint16_t override_blend = blend_override[pass.shader_id + i];
        uint16_t texture_unit_lookup = _texture_unit_lookup[pass.texture_coord_combo_index + i];

        if (i == 0 && _render_flags[pass.renderflag_index].blend == 0)
        {
          override_blend = 0;
        }

        runtime_shader_val[i] = override_blend;

        if (texture_unit_lookup == -1)
        {
          runtime_shader_val[i] |= 0x8;
        }

        if (texture_unit_lookup == 1 && i + 1 == pass.texture_count)
        {
          shader |= 0x4000;
        }        
      }

      shader |= (runtime_shader_val[1] & 0xFFFF) | ((runtime_shader_val[0] << 4) & 0xFFFF);
    }

    pass.shader_id = shader;
  }
}

void Model::fix_shader_id_layer()
{
  ModelRenderPass* first_pass = nullptr;
  bool need_reducing = false;
  uint16_t previous_render_flag = -1, some_flags = 0;

  for (auto& pass : _render_passes)
  {
    if (pass.renderflag_index == previous_render_flag)
    {
      need_reducing = true;
      continue;
    }

    previous_render_flag = pass.renderflag_index;

    uint8_t lower_bits = pass.shader_id & 0x7;

    if (pass.material_layer == 0)
    {
      if (pass.texture_count >= 1 && _render_flags[pass.renderflag_index].blend == 0)
      {
        pass.shader_id &= 0xFF8F;
      }

      first_pass = &pass;
    }

    bool xor_unlit = ((_render_flags[pass.renderflag_index].flags.unlit ^ _render_flags[first_pass->renderflag_index].flags.unlit) & 1) == 0;

    if ((some_flags & 0xFF) == 1)
    {
      if ( (_render_flags[pass.renderflag_index].blend == 1 || _render_flags[pass.renderflag_index].blend == 2) 
        && pass.texture_count == 1 
        && xor_unlit
        && pass.texture_combo_index == first_pass->texture_combo_index
         )
      {
        if (_transparency_lookup[pass.transparency_combo_index] == _transparency_lookup[first_pass->transparency_combo_index])
        {
          pass.shader_id = 0x8000;
          first_pass->shader_id = 0x8001;

          some_flags = (some_flags & 0xFF00) | 3;
        }
      }

      some_flags = (some_flags & 0xFF00);
    }

    if ((some_flags & 0xFF) < 2) 
    {
      if ((_render_flags[pass.renderflag_index].blend == 0) && (pass.texture_count == 2) && ((lower_bits == 4) || (lower_bits == 6))) 
      {
        if ((_texture_unit_lookup[pass.texture_coord_combo_index] == 0) && (_texture_unit_lookup[pass.texture_coord_combo_index + 1] > 2))
        {
          some_flags = (some_flags & 0xFF00) | 1;
        }
      }
    }
  
    if ((some_flags >> 8) != 0) 
    {
      if ((some_flags >> 8) == 1) 
      {
        if ((_render_flags[pass.renderflag_index].blend != 4) && (_render_flags[pass.renderflag_index].blend != 6) || (pass.texture_count != 1) || (_texture_unit_lookup[pass.texture_coord_combo_index] <= 2))
        {
          some_flags &= 0xFF00;
        }
        else  if (_transparency_lookup[pass.transparency_combo_index] == _transparency_lookup[first_pass->transparency_combo_index])
        {
          pass.shader_id = 0x8000;
          first_pass->shader_id = _render_flags[pass.renderflag_index].blend != 4 ? 0xE : 0x8002;

          //TODO: Implement packing of textures (see https://wowdev.wiki/M2/.skin/WotLK_shader_selection )

          some_flags = (some_flags & 0xFF) | (2 << 8);
          continue;
        }
      }
      else 
      {
        if ((some_flags >> 8) != 2) 
        {
          continue;
        }

        if  ( (_render_flags[pass.renderflag_index].blend != 2) && (_render_flags[pass.renderflag_index].blend != 1)
           || (pass.texture_count != 1)
           || xor_unlit
           || ((pass.texture_combo_index & 0xff) != (first_pass->texture_combo_index & 0xff))
            ) 
        {
          some_flags &= 0xFF00;
        }
        else  if (_transparency_lookup[pass.transparency_combo_index] == _transparency_lookup[first_pass->transparency_combo_index]) 
        {
          pass.shader_id = 0x8000;
          first_pass->shader_id = ((first_pass->shader_id == 0x8002 ? 2 : 0) - 0x7FFF) & 0xFFFF;
          some_flags = (some_flags & 0xFF) | (3 << 8);
          continue;
        }
      }
      some_flags = (some_flags & 0xFF);
    }

    if ((_render_flags[pass.renderflag_index].blend == 0) && (pass.texture_count == 1) && (_texture_unit_lookup[pass.texture_coord_combo_index] == 0)) 
    {
      some_flags = (some_flags & 0xFF) | (1 << 8);
    }
  }

  if (need_reducing)
  {
    previous_render_flag = -1;
    for (int i = 0; i < _render_passes.size(); ++i) 
    {
      auto& pass = _render_passes[i];
      uint16_t renderflag_index = pass.renderflag_index;

      if (renderflag_index == previous_render_flag) 
      {
        pass.shader_id = _render_passes[i - 1].shader_id;
        pass.texture_count = _render_passes[i - 1].texture_count;
        pass.texture_combo_index = _render_passes[i - 1].texture_combo_index;
        pass.texture_coord_combo_index = _render_passes[i - 1].texture_coord_combo_index;
      }
      else 
      {
        previous_render_flag = renderflag_index;
      }
    }
  }
}


ModelRenderPass::ModelRenderPass(ModelTexUnit const& tex_unit, Model* m)
  : ModelTexUnit(tex_unit)
  , blend_mode(m->_render_flags[renderflag_index].blend)
{
}

enum RenderFlags
{
  RENDERFLAGS_UNLIT = 1,
  RENDERFLAGS_UNFOGGED = 2,
  RENDERFLAGS_TWOSIDED = 4,
  RENDERFLAGS_BILLBOARD = 8,
  RENDERFLAGS_ZBUFFERED = 16
};

bool ModelRenderPass::prepare_draw(opengl::scoped::use_program& m2_shader, Model *m)
{
  if (!m->showGeosets[submesh])
  {
    return false;
  }

  // COLOUR
  // Get the colour and transparency and check that we should even render
  math::vector_4d opacity_color = math::vector_4d(1.0f, 1.0f, 1.0f, m->trans); // ??
  math::vector_4d emissive_color = math::vector_4d(0.0f, 0.0f, 0.0f, 0.0f);

  auto const& renderflag(m->_render_flags[renderflag_index]);

  // emissive colors
  if (color_index != -1 && m->_colors[color_index].color.uses(0))
  {
    ::math::vector_3d c (m->_colors[color_index].color.getValue (0, m->animtime, m->_global_animtime));
    if (m->_colors[color_index].opacity.uses (m->anim))
    {
      opacity_color.w = m->_colors[color_index].opacity.getValue (m->anim, m->animtime, m->_global_animtime);
    }

    if (renderflag.flags.unlit)
    {
      opacity_color.x = c.x; opacity_color.y = c.y; opacity_color.z = c.z;
    }
    else
    {
      opacity_color.x = opacity_color.y = opacity_color.z = 0;
    }

    emissive_color = math::vector_4d(c, opacity_color.w);
    gl.materialfv(GL_FRONT, GL_EMISSION, emissive_color);
  }

  // opacity
  if (transparency_combo_index != -1)
  {
    if (m->_transparency[transparency_combo_index].trans.uses (0))
    {
      opacity_color.w = opacity_color.w * m->_transparency[transparency_combo_index].trans.getValue (0, m->animtime, m->_global_animtime);;
    }
  }

  // exit and return false before affecting the opengl render state
  if (!((opacity_color.w > 0) && (color_index == -1 || emissive_color.w > 0)))
  {
    return false;
  }
  
  switch (renderflag.blend)
  {
  default:
  case BM_OPAQUE:
    gl.disable(GL_BLEND);
    m2_shader.uniform("alpha_test", -1.f);    
    break;
  case BM_TRANSPARENT:
    gl.disable(GL_BLEND);
    m2_shader.uniform("alpha_test", (224.f / 255.f) * opacity_color.w);
    break;
  case BM_ALPHA_BLEND:
    gl.enable(GL_BLEND);
    gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m2_shader.uniform("alpha_test", -1.f);
    break;
  case BM_ADDITIVE:
    gl.enable(GL_BLEND);
    gl.blendFunc(GL_SRC_ALPHA, GL_ONE);
    m2_shader.uniform("alpha_test", -1.f);
    break;
  case BM_ADDITIVE_ALPHA:
    gl.enable(GL_BLEND);
    gl.blendFunc(GL_DST_COLOR, GL_ZERO);
    m2_shader.uniform("alpha_test", (1.f / 255.f) * opacity_color.w);
    break;
  case BM_MODULATE:
    gl.enable(GL_BLEND);
    gl.blendFunc(GL_DST_COLOR, GL_SRC_COLOR);
    m2_shader.uniform("alpha_test", (1.f / 255.f) * opacity_color.w);
    break;
  case BM_MODULATE2:
    gl.enable(GL_BLEND);
    gl.blendFunc(GL_DST_COLOR, GL_ONE);
    m2_shader.uniform("alpha_test", (1.f / 255.f) * opacity_color.w);
    break;
  }

  if (renderflag.flags.two_sided)
  {
    gl.disable(GL_CULL_FACE);
  }
  else
  {
    gl.enable(GL_CULL_FACE);
  }

  if (renderflag.flags.z_buffered)
  {
    gl.depthMask(GL_FALSE);
  }
  else
  {
    gl.depthMask(GL_TRUE);
  }

  if (texture_count > 1)
  {
    //bind_texture(1, m);
  }

  bind_texture(0, m);


  return true;
}
void ModelRenderPass::after_draw()
{
  gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void ModelRenderPass::bind_texture(size_t index, Model* m)
{
  opengl::texture::enable_texture(index);

  int tex = m->_texture_lookup[texture_combo_index + index];

  if (m->_specialTextures[tex] == -1)
  {
    m->_textures[tex]->bind();
  }
  else
  {
    m->_replaceTextures.at (m->_specialTextures[tex])->bind();
  }    
}


ModelPixelShader model_pixel_shader[] =
{
  ModelPixelShader::Combiners_Opaque_Mod2xNA_Alpha,
  ModelPixelShader::Combiners_Opaque_AddAlpha,
  ModelPixelShader::Combiners_Opaque_AddAlpha_Alpha,
  ModelPixelShader::Combiners_Opaque_Mod2xNA_Alpha_Add,
  ModelPixelShader::Combiners_Mod_AddAlpha,
  ModelPixelShader::Combiners_Opaque_AddAlpha,
  ModelPixelShader::Combiners_Mod_AddAlpha,
  ModelPixelShader::Combiners_Mod_AddAlpha_Alpha,
  ModelPixelShader::Combiners_Opaque_Alpha_Alpha,
  ModelPixelShader::Combiners_Opaque_Mod2xNA_Alpha_3s,
  ModelPixelShader::Combiners_Opaque_AddAlpha_Wgt,
  ModelPixelShader::Combiners_Mod_Add_Alpha,
  ModelPixelShader::Combiners_Opaque_ModNA_Alpha,
  ModelPixelShader::Combiners_Mod_AddAlpha_Wgt,
  ModelPixelShader::Combiners_Mod_AddAlpha_Wgt,
  ModelPixelShader::Combiners_Opaque_AddAlpha_Wgt,
  ModelPixelShader::Combiners_Opaque_Mod_Add_Wgt,
  ModelPixelShader::Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha,
  ModelPixelShader::Combiners_Mod_Dual_Crossfade,
  ModelPixelShader::Combiners_Mod_Depth,
  ModelPixelShader::Combiners_Mod_AddAlpha_Alpha,
  ModelPixelShader::Combiners_Mod_Mod,
  ModelPixelShader::Combiners_Mod_Masked_Dual_Crossfade,
  ModelPixelShader::Combiners_Opaque_Alpha,
  ModelPixelShader::Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha,
  ModelPixelShader::Combiners_Mod_Depth
};

// https://wowdev.wiki/M2/.skin#Pixel_shaders
ModelPixelShader M2GetPixelShaderID (uint16_t texture_count, uint16_t shader_id)
{
  if (shader_id & 0x8000)
  {
    uint16_t const shaderID (shader_id & (~0x8000));
    assert (shaderID < MODEL_PIXEL_SHADER_COUNT);
    return model_pixel_shader[shaderID];
  }
  else
  {
    if (texture_count == 1)
    {
      return shader_id & 0x70 ? ModelPixelShader::Combiners_Mod : ModelPixelShader::Combiners_Opaque;
    }
    else
    {
      const uint16_t lower (shader_id & 7);
      if (shader_id & 0x70)
      {
        return lower == 0 ? ModelPixelShader::Combiners_Mod_Opaque
          : lower == 3 ? ModelPixelShader::Combiners_Mod_Add
          : lower == 4 ? ModelPixelShader::Combiners_Mod_Mod2x
          : lower == 6 ? ModelPixelShader::Combiners_Mod_Mod2xNA
          : lower == 7 ? ModelPixelShader::Combiners_Mod_AddNA
          : ModelPixelShader::Combiners_Mod_Mod;
      }
      else
      {
        return lower == 0 ? ModelPixelShader::Combiners_Opaque_Opaque
          : lower == 3 ? ModelPixelShader::Combiners_Opaque_AddAlpha
          : lower == 4 ? ModelPixelShader::Combiners_Opaque_Mod2x
          : lower == 6 ? ModelPixelShader::Combiners_Opaque_Mod2xNA
          : lower == 7 ? ModelPixelShader::Combiners_Opaque_AddAlpha
          : ModelPixelShader::Combiners_Opaque_Mod;
      }
    }
  }
}

void Model::compute_pixel_shader_ids()
{
  for (auto& pass : _render_passes)
  {
    pass.pixel_shader = M2GetPixelShaderID(pass.texture_count, pass.shader_id);
  }
}

void Model::initAnimated(const MPQFile& f)
{
  std::vector<std::unique_ptr<MPQFile>> animation_files;

  if (header.nAnimations > 0) {
    _animations.resize (header.nAnimations);
    memcpy(_animations.data(), f.getBuffer() + header.ofsAnimations, header.nAnimations * sizeof(ModelAnimation));
    for (size_t i = 0; i < header.nAnimations; ++i)
    {
      //! \note Fix for world\kalimdor\diremaul\activedoodads\crystalcorrupter\corruptedcrystalshard.m2 having a zero length for its stand animation.
      _animations[i].length = std::max(_animations[i].length, 1U);
    }

    for (size_t i = 0; i < header.nAnimations; ++i)
    {
      std::string lodname = _filename.substr(0, _filename.length() - 3);
      std::stringstream tempname;
      tempname << lodname << _animations[i].animID << "-" << _animations[i].subAnimID << ".anim";
      if (MPQFile::exists(tempname.str()))
      {
        animation_files.push_back(std::make_unique<MPQFile>(tempname.str()));
      }
    }
  }

  if (animBones)
  {
    ModelBoneDef const* mb = reinterpret_cast<ModelBoneDef const*>(f.getBuffer () + header.ofsBones);
    for (size_t i = 0; i<header.nBones; ++i)
    {
      bones.emplace_back (f, mb[i], _global_sequences.data(), animation_files);
    }
    calcBones (anim, 0, 0);
  }  

  if (animTextures) {
    ModelTexAnimDef const* ta = reinterpret_cast<ModelTexAnimDef const*>(f.getBuffer() + header.ofsTexAnims);
    for (size_t i=0; i<header.nTexAnims; ++i) {
      _texture_animations.emplace_back (f, ta[i], _global_sequences.data());
    }
  }

  // particle systems
  if (header.nParticleEmitters) {
    ModelParticleEmitterDef const* pdefs = reinterpret_cast<ModelParticleEmitterDef const*>(f.getBuffer() + header.ofsParticleEmitters);
    for (size_t i = 0; i<header.nParticleEmitters; ++i) 
    {
      try
      {
        _particles.emplace_back (this, f, pdefs[i], _global_sequences.data());
      }
      catch (std::logic_error error)
      {
        LogError << "Loading particles for '" << _filename << "' " << error.what() << std::endl;
      }      
    }
  }

  // ribbons
  if (header.nRibbonEmitters) {
    ModelRibbonEmitterDef const* rdefs = reinterpret_cast<ModelRibbonEmitterDef const*>(f.getBuffer() + header.ofsRibbonEmitters);
    for (size_t i = 0; i<header.nRibbonEmitters; ++i) {
      _ribbons.emplace_back(this, f, rdefs[i], _global_sequences.data());
    }
  }

  // just use the first camera, meh
  if (header.nCameras>0) {
    ModelCameraDef const* camDefs = reinterpret_cast<ModelCameraDef const*>(f.getBuffer() + header.ofsCameras);
    cam = ModelCamera(f, camDefs[0], _global_sequences.data());
  }

  // init lights
  if (header.nLights) {
    ModelLightDef const* lDefs = reinterpret_cast<ModelLightDef const*>(f.getBuffer() + header.ofsLights);
    for (size_t i=0; i<header.nLights; ++i)
      _lights.emplace_back (f, lDefs[i], _global_sequences.data());
  }

  animcalc = false;
}

void Model::calcBones(int _anim, int time, int animtime)
{
  for (size_t i = 0; i<header.nBones; ++i) {
    bones[i].calc = false;
  }

  for (size_t i = 0; i<header.nBones; ++i) {
    bones[i].calcMatrix(bones.data(), _anim, time, animtime);
  }
}

void Model::animate(int _anim, int animtime_)
{
  this->anim = _anim;
  ModelAnimation &a = _animations[anim];

  if (_animations.empty())
    return;

  int t = animtime_;
  int tmax = a.length;
  t %= tmax;
  animtime = t;
  _global_animtime = animtime_;

  if (animBones) {
    //calcBones(anim, t, _global_animtime);
  }

  if (animGeometry) {
    // transform vertices
    _current_vertices.resize (header.nVertices);

    for (size_t i (0); i < header.nVertices; ++i)
    {
      model_vertex const& vertex (_vertices[i]);
      model_vertex_parameter const& param (_vertices_parameters[i]);

      ::math::vector_3d v(0,0,0), n(0,0,0);

      for (size_t b (0); b < 4; ++b)
      {
        if (param.weights[b] <= 0)
          continue;

        ::math::vector_3d tv = bones[param.bones[b]].mat * vertex.position;
        ::math::vector_3d tn = bones[param.bones[b]].mrot * vertex.normal;

        v += tv * (static_cast<float> (param.weights[b]) / 255.0f);
        n += tn * (static_cast<float> (param.weights[b]) / 255.0f);
      }

      _current_vertices[i].position = v;
      _current_vertices[i].normal = n.normalize();
      memcpy(_current_vertices[i].texcoords, vertex.texcoords, 2 * sizeof(::math::vector_2d));
    }

    opengl::scoped::buffer_binder<GL_ARRAY_BUFFER> const binder (_vertices_buffer);
    gl.bufferData (GL_ARRAY_BUFFER, _current_vertices.size() * sizeof (model_vertex), _current_vertices.data(), GL_STREAM_DRAW);
  }

  for (size_t i=0; i<header.nLights; ++i) {
    if (_lights[i].parent>=0) {
      _lights[i].tpos = bones[_lights[i].parent].mat * _lights[i].pos;
      _lights[i].tdir = bones[_lights[i].parent].mrot * _lights[i].dir;
    }
  }

  for (auto& particle : _particles)
  {
    // random time distribution for teh win ..?
    int pt = (t + static_cast<int>(tmax*particle.tofs)) % tmax;
    particle.setup(anim, pt, _global_animtime);
  }

  for (size_t i = 0; i<header.nRibbonEmitters; ++i) {
    _ribbons[i].setup(anim, t, _global_animtime);
  }

  if (animTextures) {
    for (size_t i=0; i<header.nTexAnims; ++i) {
      _texture_animations[i].calc(anim, t, animtime);
    }
  }
}

void TextureAnim::calc(int anim, int time, int animtime)
{
  if (trans.uses(anim)) {
    tval = trans.getValue(anim, time, animtime);
  }
  if (rot.uses(anim)) {
    rval = rot.getValue(anim, time, animtime);
  }
  if (scale.uses(anim)) {
    sval = scale.getValue(anim, time, animtime);
  }
}

void TextureAnim::setup(int anim)
{
  gl.loadIdentity();
  if (trans.uses(anim)) {
    gl.translatef(tval.x, tval.y, tval.z);
  }
  if (rot.uses(anim)) {
    gl.rotatef(rval.x, 0, 0, 1); // this is wrong, I have no idea what I'm doing here ;)
  }
  if (scale.uses(anim)) {
    gl.scalef(sval.x, sval.y, sval.z);
  }
}

ModelCamera::ModelCamera(const MPQFile& f, const ModelCameraDef &mcd, int *global)
  : pos (fixCoordSystem(mcd.pos))
  , target (fixCoordSystem(mcd.target))
  , nearclip (mcd.nearclip)
  , farclip (mcd.farclip)
  , fov (mcd.fov)
  , tPos (mcd.transPos, f, global)
  , tTarget (mcd.transTarget, f, global)
  , rot (mcd.rot, f, global)
{
  tPos.apply(fixCoordSystem);
  tTarget.apply(fixCoordSystem);
}

void ModelCamera::setup (float aspect_ratio, int time, int animtime)
{
  gl.matrixMode (GL_PROJECTION);
  gl.loadIdentity();
  opengl::matrix::perspective
    (math::radians (fov * 0.6f), aspect_ratio, nearclip, farclip);
  gl.matrixMode (GL_MODELVIEW);
  gl.loadIdentity();
  opengl::matrix::look_at ( pos + tPos.getValue( 0, time, animtime )
                          , target + tTarget.getValue( 0, time, animtime )
                          , {0.0f, 1.0f, 0.0f}
                          );
}

ModelColor::ModelColor(const MPQFile& f, const ModelColorDef &mcd, int *global)
  : color (mcd.color, f, global)
  , opacity(mcd.opacity, f, global)
{}

ModelTransparency::ModelTransparency(const MPQFile& f, const ModelTransDef &mcd, int *global)
  : trans (mcd.trans, f, global)
{}

ModelLight::ModelLight(const MPQFile& f, const ModelLightDef &mld, int *global)
  : type (mld.type)
  , parent (mld.bone)
  , pos (fixCoordSystem(mld.pos))
  , tpos (fixCoordSystem(mld.pos))
  , dir (::math::vector_3d(0,1,0))
  , tdir (::math::vector_3d(0,1,0)) // obviously wrong
  , diffColor (mld.color, f, global)
  , ambColor (mld.ambColor, f, global)
  , diffIntensity (mld.intensity, f, global)
  , ambIntensity (mld.ambIntensity, f, global)
{}

void ModelLight::setup(int time, opengl::light l, int animtime)
{
  math::vector_4d ambcol(ambColor.getValue(0, time, animtime) * ambIntensity.getValue(0, time, animtime), 1.0f);
  math::vector_4d diffcol(diffColor.getValue(0, time, animtime) * diffIntensity.getValue(0, time, animtime), 1.0f);
  math::vector_4d p;

  enum ModelLightTypes {
    MODELLIGHT_DIRECTIONAL = 0,
    MODELLIGHT_POINT
  };

  if (type == MODELLIGHT_DIRECTIONAL) {
    // directional
    p = math::vector_4d(tdir, 0.0f);
  }
  else if (type == MODELLIGHT_POINT) {
    // point
    p = math::vector_4d(tpos, 1.0f);
  }
  else {
    p = math::vector_4d(tpos, 1.0f);
    LogError << "Light type " << type << " is unknown." << std::endl;
  }
  //gLog("Light %d (%f,%f,%f) (%f,%f,%f) [%f,%f,%f]\n", l-GL_LIGHT4, ambcol.x, ambcol.y, ambcol.z, diffcol.x, diffcol.y, diffcol.z, p.x, p.y, p.z);
  gl.lightfv(l, GL_POSITION, p);
  gl.lightfv(l, GL_DIFFUSE, diffcol);
  gl.lightfv(l, GL_AMBIENT, ambcol);
  gl.enable(l);
}

TextureAnim::TextureAnim (const MPQFile& f, const ModelTexAnimDef &mta, int *global)
  : trans (mta.trans, f, global)
  , rot (mta.rot, f, global)
  , scale (mta.scale, f, global)
{}

namespace
{
  //! \todo other billboard types
  static const int MODELBONE_BILLBOARD = 8;
}

Bone::Bone( const MPQFile& f,
            const ModelBoneDef &b,
            int *global,
            const std::vector<std::unique_ptr<MPQFile>>& animation_files)
  : trans (b.translation, f, global, animation_files)
  , rot (b.rotation, f, global, animation_files)
  , scale (b.scaling, f, global, animation_files)
  , pivot (fixCoordSystem (b.pivot))
  , parent (b.parent)
  , billboard (b.flags & MODELBONE_BILLBOARD)
{
  trans.apply(fixCoordSystem);
  rot.apply(fixCoordSystemQuat);
  scale.apply(fixCoordSystem2);
}

void Bone::calcMatrix(Bone *allbones, int anim, int time, int animtime)
{
  if (calc) return;

  math::matrix_4x4 m {math::matrix_4x4::unit};
  math::quaternion q;

  if (rot.uses(anim) || scale.uses(anim) || trans.uses(anim) || billboard)
  {
    m = {math::matrix_4x4::translation, pivot};

    if (trans.uses(anim))
    {
      m *= math::matrix_4x4 (math::matrix_4x4::translation, trans.getValue (anim, time, animtime));
    }

    if (rot.uses(anim))
    {
      m *= math::matrix_4x4 (math::matrix_4x4::rotation, q = rot.getValue (anim, time, animtime));
    }

    if (scale.uses(anim))
    {
      m *= math::matrix_4x4 (math::matrix_4x4::scale, scale.getValue (anim, time, animtime));
    }

    if (billboard)
    {
      float modelview[16];
      gl.getFloatv(GL_MODELVIEW_MATRIX, modelview);

      math::vector_3d vRight (modelview[0], modelview[4], modelview[8]);
      math::vector_3d vUp (modelview[1], modelview[5], modelview[9]); // Spherical billboarding
      //math::vector_3d vUp = math::vector_3d(0,1,0); // Cylindrical billboarding
      vRight = vRight * -1;
      m (0, 2, vRight.x);
      m (1, 2, vRight.y);
      m (2, 2, vRight.z);
      m (0, 1, vUp.x);
      m (1, 1, vUp.y);
      m (2, 1, vUp.z);
    }

    m *= math::matrix_4x4 (math::matrix_4x4::translation, -pivot);
  }

  if (parent >= 0)
  {
    allbones[parent].calcMatrix (allbones, anim, time, animtime);
    mat = allbones[parent].mat * m;
  }
  else
  {
    mat = m;
  }

  // transform matrix for normal vectors ... ??
  if (rot.uses(anim))
  {
    if (parent >= 0)
    {
      mrot = allbones[parent].mrot * math::matrix_4x4 (math::matrix_4x4::rotation, q);
    }
    else
    {
      mrot = math::matrix_4x4 (math::matrix_4x4::rotation, q);
    }
  }
  else
  {
    mrot = math::matrix_4x4::unit;
  }

  calc = true;
}


void Model::draw (bool draw_fog, int animtime, bool draw_particles)
{
  return;

  if (!finishedLoading())
    return;

  if (!_finished_upload) {
    upload();
    return;
  }

  if (draw_fog)
    gl.enable(GL_FOG);
  else
    gl.disable(GL_FOG);

  if (animated && (!animcalc || mPerInstanceAnimation))
  {
    animate(0, animtime);
    animcalc = true;
  }

  lightsOn(GL_LIGHT4);

  // assume these client states are enabled: GL_VERTEX_ARRAY, GL_NORMAL_ARRAY, GL_TEXTURE_COORD_ARRAY
  opengl::scoped::buffer_binder<GL_ARRAY_BUFFER> const binder (_vertices_buffer);
  gl.vertexPointer (3, GL_FLOAT, sizeof (model_vertex), 0);
  gl.normalPointer (GL_FLOAT, sizeof (model_vertex), reinterpret_cast<void*> (sizeof (::math::vector_3d)));
  gl.texCoordPointer (2, GL_FLOAT, sizeof (model_vertex), reinterpret_cast<void*> (2 * sizeof (::math::vector_3d)));

  gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl.alphaFunc(GL_GREATER, 0.3f);

  for (ModelRenderPass& p : _render_passes)
  { 
    // we don't want to render completely transparent parts
    /*if (p.prepare_draw(m2_shader, this))
    {
      gl.drawRangeElements(GL_TRIANGLES, p.vertex_start, p.vertex_end, p.index_count, GL_UNSIGNED_SHORT, _indices.data() + p.index_start);
      p.after_draw();
    }*/
  }
  // done with all render ops

  gl.alphaFunc(GL_GREATER, 0.0f);
  gl.disable(GL_ALPHA_TEST);

  GLfloat czero[4] = { 0, 0, 0, 1 };
  gl.materialfv(GL_FRONT, GL_EMISSION, czero);
  gl.color4f(1, 1, 1, 1);
  gl.depthMask(GL_TRUE);

  lightsOff(GL_LIGHT4);


  // draw particle systems & ribbons
  if (draw_particles)
  {
    for (auto& particle : _particles)
    {
      particle.draw();
    }

    for (auto& ribbon : _ribbons)
    {
      ribbon.draw();
    }
  }
  
}

void Model::draw ( std::vector<ModelInstance*> instances
                 , opengl::scoped::use_program& m2_shader
                 , math::frustum const& frustum
                 , const float& cull_distance
                 , const math::vector_3d& camera
                 , bool draw_fog
                 , int animtime
                 , bool draw_particles
                 , bool all_boxes
                 , std::unordered_map<Model*, std::size_t>& visible_model_count
                 )
{
  if (!finishedLoading())
  {
    return;
  }

  if (!_finished_upload) 
  {
    upload();
    return;
  }

  if (animated && (!animcalc || mPerInstanceAnimation))
  {
    animate(0, animtime);
    animcalc = true;
  }

  std::vector<math::matrix_4x4> transform_matrix;

  for (ModelInstance* mi : instances)
  {
    if (mi->is_visible(frustum, cull_distance, camera))
    {
      transform_matrix.push_back(mi->transform_matrix_transposed());
    }    
  }

  if (transform_matrix.empty())
  {
    return;
  }

  // store the model count to draw the bounding boxes later
  if (all_boxes)
  {
    visible_model_count.emplace(this, transform_matrix.size());
  }

  opengl::scoped::vao_binder const _ (_vao);

  {
    opengl::scoped::buffer_binder<GL_ARRAY_BUFFER> const transform_binder (_transform_buffer);
    gl.bufferData(GL_ARRAY_BUFFER, transform_matrix.size() * sizeof(::math::matrix_4x4), transform_matrix.data(), GL_DYNAMIC_DRAW);
    m2_shader.attrib("transform", 0, 1);
  }
  
  {
    opengl::scoped::buffer_binder<GL_ARRAY_BUFFER> const binder (_vertices_buffer);
    m2_shader.attrib("pos", 3, GL_FLOAT, GL_FALSE, sizeof (model_vertex), 0);
    //m2_shader.attrib("normal", 3, GL_FLOAT, GL_FALSE, sizeof (model_vertex), reinterpret_cast<void*> (sizeof (::math::vector_3d)));
    m2_shader.attrib("texcoord1", 2, GL_FLOAT, GL_FALSE, sizeof (model_vertex), reinterpret_cast<void*> (2 * sizeof (::math::vector_3d)));
    //m2_shader.attrib("texcoord2", 2, GL_FLOAT, GL_FALSE, sizeof (model_vertex), reinterpret_cast<void*> (2 * sizeof (::math::vector_3d) + sizeof(::math::vector_2d)));
  }

  for (ModelRenderPass& p : _render_passes)
  {
    if (p.prepare_draw(m2_shader, this))
    {
      //m2_shader.uniform("pixel_shader", static_cast<GLint>(p.pixel_shader));
     // m2_shader.uniform("texture_count", static_cast<GLint>(p.texture_count));

      gl.drawElementsInstanced(GL_TRIANGLES, p.index_count, GL_UNSIGNED_SHORT, _indices.data() + p.index_start, transform_matrix.size());
      p.after_draw();
    }
  }

  gl.disable(GL_BLEND);
  gl.alphaFunc(GL_GREATER, 0.0f);
  gl.disable(GL_ALPHA_TEST);
  gl.enable(GL_CULL_FACE);

  GLfloat czero[4] = { 0, 0, 0, 1 };
  gl.materialfv(GL_FRONT, GL_EMISSION, czero);
  gl.color4f(1, 1, 1, 1);
  gl.depthMask(GL_TRUE);
}

void Model::draw_box (opengl::scoped::use_program& m2_box_shader, std::size_t box_count)
{
  static std::vector<uint16_t> const indices ({5, 7, 3, 2, 0, 1, 3, 1, 5, 4, 0, 4, 6, 2, 6, 7});

  opengl::scoped::vao_binder const _ (_box_vao);

  {
    opengl::scoped::buffer_binder<GL_ARRAY_BUFFER> const transform_binder (_transform_buffer);
    m2_box_shader.attrib("transform", 0, 1);
  }

  {
    opengl::scoped::buffer_binder<GL_ARRAY_BUFFER> const binder (_box_vbo);
    m2_box_shader.attrib("position", 3, GL_FLOAT, GL_FALSE, 0, 0);
  }

  gl.drawElementsInstanced (GL_LINE_STRIP, indices.size(), GL_UNSIGNED_SHORT, indices.data(), box_count);
}


std::vector<float> Model::intersect (math::ray const& ray, int animtime)
{
  std::vector<float> results;

  if (animated && (!animcalc || mPerInstanceAnimation))
  {
    animate (0, animtime);
    animcalc = true;
  }

  for (auto&& pass : _render_passes)
  {
    for (size_t i (pass.index_start); i < pass.index_start + pass.index_count; i += 3)
    {
      if ( auto distance
          = ray.intersect_triangle( _current_vertices[_indices[i + 0]].position,
                                    _current_vertices[_indices[i + 1]].position,
                                    _current_vertices[_indices[i + 2]].position)
          )
      {
        results.emplace_back (*distance);
      }
    }
  }

  return results;
}

void Model::lightsOn(opengl::light lbase)
{
  // setup lights
  for (unsigned int i=0, l=lbase; i<header.nLights; ++i) _lights[i].setup(animtime, l++, _global_animtime);
}

void Model::lightsOff(opengl::light lbase)
{
  for (unsigned int i = 0, l = lbase; i<header.nLights; ++i) gl.disable(l++);
}

void Model::upload()
{
  for (std::string texture : _textureFilenames)
    _textures.emplace_back(texture);

  gl.genVertexArrays (1, &_vao);
  gl.genBuffers (1, &_transform_buffer);
  gl.genBuffers (1, &_vertices_buffer);

  gl.genVertexArrays (1, &_box_vao);
  gl.genBuffers (1, &_box_vbo);

  if (!animGeometry)
  {
    opengl::scoped::buffer_binder<GL_ARRAY_BUFFER> const binder (_vertices_buffer);
    gl.bufferData (GL_ARRAY_BUFFER, _current_vertices.size() * sizeof (model_vertex), _current_vertices.data(), GL_STATIC_DRAW);
  }

  {
    opengl::scoped::buffer_binder<GL_ARRAY_BUFFER> const binder (_box_vbo);
    gl.bufferData (GL_ARRAY_BUFFER, _vertex_box_points.size() * sizeof (math::vector_3d), _vertex_box_points.data(), GL_STATIC_DRAW);
  }

  _finished_upload = true;
}

void Model::updateEmitters(float dt)
{
  if (finished)
  {
    for (auto& particle : _particles)
    {
      particle.update (dt);
    }
  }
}
