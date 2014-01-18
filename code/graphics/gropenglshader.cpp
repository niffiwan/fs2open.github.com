/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/


#include "globalincs/pstypes.h"
#include "globalincs/def_files.h"

#include "graphics/2d.h"
#include "lighting/lighting.h"
#include "graphics/grinternal.h"
#include "graphics/gropengl.h"
#include "graphics/gropenglextension.h"
#include "graphics/gropengltexture.h"
#include "graphics/gropengllight.h"
#include "graphics/gropengltnl.h"
#include "graphics/gropengldraw.h"
#include "graphics/gropenglshader.h"
#include "graphics/gropenglpostprocessing.h"
#include "graphics/gropenglstate.h"

#include "math/vecmat.h"
#include "render/3d.h"
#include "cmdline/cmdline.h"
#include "mod_table/mod_table.h"

#include <sstream>

using namespace opengl::shader;

static char *GLshader_info_log = NULL;
static const int GLshader_info_log_size = 8192;
GLuint Framebuffer_fallback_texture_id = 0;

static AnimatedShader Effect_num = ANIMATED_SHADER_LOADOUTSELECT_FS1;
static float Anim_timer = 0.0f;

struct opengl_shader_uniform_reference_t {
	int flag;

	int num_uniforms;
	const char* uniforms[MAX_SHADER_UNIFORMS];

	int num_attributes;
	const char* attributes[MAX_SDR_ATTRIBUTES];

	const char* name;
};

/**
 * Static lookup reference for main shader uniforms
 * When adding a new SDR_ flag, list all associated uniforms and attributes here
 */
static opengl_shader_uniform_reference_t GL_Uniform_Reference_Main[] = {
	{ SDR_FLAG_LIGHT,		1, {"n_lights"}, 0, { NULL }, "Lighting" },
	{ SDR_FLAG_FOG,			0, { NULL }, 0, { NULL }, "Fog Effect" },
	{ SDR_FLAG_DIFFUSE_MAP, 5, {"sBasemap", "desaturate", "desaturate_r", "desaturate_g", "desaturate_b"}, 0, { NULL }, "Diffuse Mapping"},
	{ SDR_FLAG_GLOW_MAP,	1, {"sGlowmap"}, 0, { NULL }, "Glow Mapping" },
	{ SDR_FLAG_SPEC_MAP,	1, {"sSpecmap"}, 0, { NULL }, "Specular Mapping" },
	{ SDR_FLAG_NORMAL_MAP,	1, {"sNormalmap"}, 0, { NULL }, "Normal Mapping" },
	{ SDR_FLAG_HEIGHT_MAP,	1, {"sHeightmap"}, 0, { NULL }, "Parallax Mapping" },
	{ SDR_FLAG_ENV_MAP,		3, {"sEnvmap", "alpha_spec", "envMatrix"}, 0, { NULL }, "Environment Mapping" },
	{ SDR_FLAG_ANIMATED,	5, {"sFramebuffer", "effect_num", "anim_timer", "vpwidth", "vpheight"}, 0, { NULL }, "Animated Effects" },
	{ SDR_FLAG_MISC_MAP,	1, {"sMiscmap"}, 0, { NULL }, "Utility mapping" },
	{ SDR_FLAG_TEAMCOLOR,	2, {"stripe_color", "base_color"}, 0, { NULL }, "Team Colors" },
	{ SDR_FLAG_THRUSTER,	1, {"thruster_scale"}, 0, { NULL }, "Thruster scaling" }
};

static const int Main_shader_flag_references = sizeof(GL_Uniform_Reference_Main) / sizeof(opengl_shader_uniform_reference_t);

/**
 * Static lookup referene for particle shader uniforms
 */
static opengl_shader_uniform_reference_t GL_Uniform_Reference_Particle[] = {
	{ (SDR_FLAG_SOFT_QUAD | SDR_FLAG_DISTORTION), 6, {"baseMap", "window_width", "window_height", "distMap", "frameBuffer", "use_offset"}, 1, { "offset_in" }, "Distorted Particles" },
	{ (SDR_FLAG_SOFT_QUAD),	6, {"baseMap", "depthMap", "window_width", "window_height", "nearZ", "farZ"}, 1, { "radius_in" }, "Depth-blended Particles" }
};

static const int Particle_shader_flag_references = sizeof(GL_Uniform_Reference_Particle) / sizeof(opengl_shader_uniform_reference_t);

using namespace opengl;
using namespace opengl::shader;

/**
 * Given a set of flags, determine whether a shader with these flags exists within the GL_shader vector. If no shader with the requested flags exists, attempt to compile one.
 *
 * @param flags	Integer variable, holding a combination of SDR_* flags
 * @return 		Index into GL_shader, referencing a valid shader, or -1 if shader compilation failed
 */
int gr_opengl_maybe_create_shader(int flags)
{
	if (Use_GLSL < 2)
		return -1;

	const SCP_vector<Shader>& shaders = GL_state.Shader.getShaderVector();

	SCP_vector<Shader>::const_iterator iter;
	int idx = 0;

	for (iter = shaders.begin(); iter != shaders.end(); ++iter) {
		if (iter->getPrimaryFlags() == flags) {
			return idx;
		}

		++idx;
	}

	// If we are here, it means we need to compile a new shader
	compile_main_shader(flags);
	if (shaders.back().getPrimaryFlags() == flags)
		return (int) shaders.size() - 1;

	// If even that has failed, bail
	return -1;
}

namespace opengl
{
	namespace shader
	{
		/**
		 * Go through GL_shader and call glDeleteObject() for all created shaders, then clear GL_shader
		 */
		void shutdown()
		{
			if ( !Use_GLSL ) {
				return;
			}

			if (GLshader_info_log != NULL) {
				vm_free(GLshader_info_log);
				GLshader_info_log = NULL;
			}
		}

		/**
		 * Compiles a new shader, and creates an shader_t that will be put into the GL_shader vector
		 * if compilation is successful.
		 * This function is used for main (i.e. model rendering) and particle shaders, post processing shaders use their own infrastructure
		 *
		 * @param flags		Combination of SDR_* flags
		 */
		void compile_main_shader(int flags) {
			mprintf(("Compiling new shader:\n"));

			Shader shader("Main shader");
			shader.addPrimaryFlag(flags);
			bool in_error = false;

			// choose appropriate files
			SCP_string vert_name;
			SCP_string frag_name;

			if (flags & SDR_FLAG_SOFT_QUAD) {
				vert_name.assign("soft-v.sdr");
				frag_name.assign("soft-f.sdr");
			} else {
				vert_name.assign("main-v.sdr");
				frag_name.assign("main-f.sdr");
			}

			if (!shader.loadShaderFile(Shader::VERTEX_SHADER, vert_name))
			{
				in_error = true;
			}

			if (!shader.loadShaderFile(Shader::FRAGMENT_SHADER, frag_name))
			{
				in_error = true;
			}

			if (!in_error)
			{
				if (!shader.linkProgram())
				{
					in_error = true;
				}
			}

			if (!in_error)
			{
				GL_state.Shader.enableShader(shader);

				mprintf(("Shader features:\n"));

				//Init all the uniforms
				if (shader.getPrimaryFlags() & SDR_FLAG_SOFT_QUAD) {
					for (int j = 0; j < Particle_shader_flag_references; j++) {
						if (shader.getPrimaryFlags() == GL_Uniform_Reference_Particle[j].flag) {
							int k;

							// Equality check needed because the combination of SDR_FLAG_SOFT_QUAD and SDR_FLAG_DISTORTION define something very different
							// than just SDR_FLAG_SOFT_QUAD alone
							for (k = 0; k < GL_Uniform_Reference_Particle[j].num_uniforms; k++) {
								shader.addUniform(GL_Uniform_Reference_Particle[j].uniforms[k]);
							}

							for (k = 0; k < GL_Uniform_Reference_Particle[j].num_attributes; k++) {
								shader.addAttribute(GL_Uniform_Reference_Particle[j].attributes[k]);
							}

							mprintf(("   %s\n", GL_Uniform_Reference_Particle[j].name));
						}
					}
				}
				else {
					for (int j = 0; j < Main_shader_flag_references; j++) {
						if (shader.getPrimaryFlags() & GL_Uniform_Reference_Main[j].flag) {
							if (GL_Uniform_Reference_Main[j].num_uniforms > 0) {
								for (int k = 0; k < GL_Uniform_Reference_Main[j].num_uniforms; k++) {
									shader.addUniform(GL_Uniform_Reference_Main[j].uniforms[k]);
								}
							}

							if (GL_Uniform_Reference_Main[j].num_attributes > 0) {
								for (int k = 0; k < GL_Uniform_Reference_Main[j].num_attributes; k++) {
									shader.addAttribute(GL_Uniform_Reference_Main[j].attributes[k]);
								}
							}

							mprintf(("   %s\n", GL_Uniform_Reference_Main[j].name));
						}
					}
				}

				GL_state.Shader.disableShader();

				GL_state.Shader.addShader(shader);
			}
			else
			{
				shader.releaseResources();

				// shut off relevant usage things ...
				bool dealt_with = false;

				if (flags & SDR_FLAG_HEIGHT_MAP) {
					mprintf(("  Shader in_error!  Disabling height maps!\n"));
					Cmdline_height = 0;
					dealt_with = true;
				}

				if (flags & SDR_FLAG_NORMAL_MAP) {
					mprintf(("  Shader in_error!  Disabling normal maps and height maps!\n"));
					Cmdline_height = 0;
					Cmdline_normal = 0;
					dealt_with = true;
				}

				if (!dealt_with) {
					if (flags == 0) {
						mprintf(("  Shader in_error!  Disabling GLSL!\n"));

						Use_GLSL = 0;
						Cmdline_height = 0;
						Cmdline_normal = 0;

						// Clear resources of shader manager
						GL_state.Shader.shutdown();
					} else {
						// We died on a lighting shader, probably due to instruction count.
						// Drop down to a special var that will use fixed-function rendering
						// but still allow for post-processing to work
						mprintf(("  Shader in_error!  Disabling GLSL model rendering!\n"));
						Use_GLSL = 1;
						Cmdline_height = 0;
						Cmdline_normal = 0;
					}
				}
			}
		}

		/**
		 * Initializes the shader system. Creates a 1x1 texture that can be used as a fallback texture when framebuffer support is missing.
		 * Also compiles the shaders used for particle rendering.
		 */
		void init()
		{
			if ( !Use_GLSL ) {
				return;
			}

			glGenTextures(1,&Framebuffer_fallback_texture_id);
			GL_state.Texture.SetActiveUnit(0);
			GL_state.Texture.SetTarget(GL_TEXTURE_2D);
			GL_state.Texture.Enable(Framebuffer_fallback_texture_id);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
			GLuint pixels[4] = {0,0,0,0};
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, &pixels);

			if (Cmdline_no_glsl_model_rendering) {
				Use_GLSL = 1;
			}

			// Compile the particle shaders, since these are most definitely going to be used
			compile_main_shader(SDR_FLAG_SOFT_QUAD);
			compile_main_shader(SDR_FLAG_SOFT_QUAD | SDR_FLAG_DISTORTION);

			mprintf(("\n"));
		}

		/**
		 * Retrieve the compilation log for a given shader object, and store it in the GLshader_info_log global variable
		 *
		 * @param shader_object		OpenGL handle of a shader object
		 */
		void check_info_log(GLhandleARB shader_object)
		{
			if (GLshader_info_log == NULL) {
				GLshader_info_log = (char *) vm_malloc(GLshader_info_log_size);
			}

			memset(GLshader_info_log, 0, GLshader_info_log_size);

			vglGetInfoLogARB(shader_object, GLshader_info_log_size-1, 0, GLshader_info_log);
		}

		/**
		 * Sets the currently active animated effect.
		 *
		 * @param effect	Effect ID, needs to be implemented and checked for in the shader
		 */
		void set_animated_effect(AnimatedShader effect)
		{
			Assert(effect > -1);
			Effect_num = effect;
		}

		/**
		 * Returns the currently active animated effect ID.
		 *
		 * @return		Currently active effect ID
		 */
		AnimatedShader get_animated_effect()
		{
			return Effect_num;
		}

		/**
		 * Set the timer for animated effects.
		 *
		 * @param timer		Timer value to be passed to the shader
		 */
		void set_animated_timer(float timer)
		{
			Anim_timer = timer;
		}

		/**
		 * Get the timer for animated effects.
		 */
		float get_animated_timer()
		{
			return Anim_timer;
		}

		Shader::~Shader()
		{
		}

		void Shader::releaseResources()
		{
			for (int i = 0; i < MAX_SHADER_TYPES; i++)
			{
				if (programs[i] != 0)
				{
					vglDeleteObjectARB(programs[i]);
					programs[i] = 0;
				}
			}

			if (shaderHandle != 0)
			{
				vglDeleteObjectARB(this->shaderHandle);
				this->shaderHandle = 0;
			}

			uniforms.clear();
			attributes.clear();

			flags = 0;
			flags2 = 0;
		}

		void Shader::addPrimaryFlag(int flags)
		{
			this->flags |= flags;
		}

		void Shader::addSecondaryFlag(int flags)
		{
			this->flags2 |= flags;
		}

		/**
		* Pass a GLSL shader source to OpenGL and compile it into a usable shader object.
		* Prints compilation errors (if any) to the log.
		* Note that this will only compile shaders into objects, linking them into executables happens later
		*
		* @param shader_source		GLSL sourcecode for the shader
		* @param shader_type		OpenGL ID for the type of shader being used, like GL_FRAGMENT_SHADER_ARB, GL_VERTEX_SHADER_ARB
		* @return 					OpenGL handle for the compiled shader object
		*/
		GLhandleARB compile_object(const GLcharARB *shader_source, GLenum shader_type)
		{
			GLhandleARB shader_object = 0;
			GLint status = 0;

			shader_object = vglCreateShaderObjectARB(shader_type);

			vglShaderSourceARB(shader_object, 1, &shader_source, NULL);
			vglCompileShaderARB(shader_object);

			// check if the compile was successful
			vglGetObjectParameterivARB(shader_object, GL_OBJECT_COMPILE_STATUS_ARB, &status);

			check_info_log(shader_object);

			// we failed, bail out now...
			if (status == 0) {
				// basic error check
				mprintf(("%s shader failed to compile:\n%s\n", (shader_type == GL_VERTEX_SHADER_ARB) ? "Vertex" : "Fragment", GLshader_info_log));

				// this really shouldn't exist, but just in case
				if (shader_object) {
					vglDeleteObjectARB(shader_object);
				}

				return 0;
			}

			// we succeeded, maybe output warnings too
			if (strlen(GLshader_info_log) > 5) {
				nprintf(("SHADER-DEBUG", "%s shader compiled with warnings:\n%s\n", (shader_type == GL_VERTEX_SHADER_ARB) ? "Vertex" : "Fragment", GLshader_info_log));
			}

			return shader_object;
		}

		SCP_string addSourceDefinitions(const SCP_string& source, int flags)
		{
			SCP_stringstream ss;

			if (Use_GLSL >= 4) {
				ss << "#define SHADER_MODEL 4\n";
			}
			else if (Use_GLSL == 3) {
				ss << "#define SHADER_MODEL 3\n";
			}
			else {
				ss << "#define SHADER_MODEL 2\n";
			}

			if (flags & SDR_FLAG_DIFFUSE_MAP) {
				ss << "#define FLAG_DIFFUSE_MAP\n";
			}

			if (flags & SDR_FLAG_ENV_MAP) {
				ss << "#define FLAG_ENV_MAP\n";
			}

			if (flags & SDR_FLAG_FOG) {
				ss << "#define FLAG_FOG\n";
			}

			if (flags & SDR_FLAG_GLOW_MAP) {
				ss << "#define FLAG_GLOW_MAP\n";
			}

			if (flags & SDR_FLAG_HEIGHT_MAP) {
				ss << "#define FLAG_HEIGHT_MAP\n";
			}

			if (flags & SDR_FLAG_LIGHT) {
				ss << "#define FLAG_LIGHT\n";
			}

			if (flags & SDR_FLAG_NORMAL_MAP) {
				ss << "#define FLAG_NORMAL_MAP\n";
			}

			if (flags & SDR_FLAG_SPEC_MAP) {
				ss << "#define FLAG_SPEC_MAP\n";
			}

			if (flags & SDR_FLAG_ANIMATED) {
				ss << "#define FLAG_ANIMATED\n";
			}

			if (flags & SDR_FLAG_DISTORTION) {
				ss << "#define FLAG_DISTORTION\n";
			}

			if (flags & SDR_FLAG_MISC_MAP) {
				ss << "#define FLAG_MISC_MAP\n";
			}

			if (flags & SDR_FLAG_TEAMCOLOR) {
				ss << "#define FLAG_TEAMCOLOR\n";
			}

			if (flags & SDR_FLAG_THRUSTER) {
				ss << "#define FLAG_THRUSTER\n";
			}

			ss << source;

			return ss.str();
		}

		bool Shader::loadShaderSource(Shader::ShaderType shaderType, const SCP_string& source)
		{
			GLenum type;
			switch (shaderType)
			{
			case FRAGMENT_SHADER:
				type = GL_FRAGMENT_SHADER_ARB;
				break;
			case VERTEX_SHADER:
				type = GL_VERTEX_SHADER_ARB;
				break;
			case GEOMETRY_SHADER:
				type = GL_GEOMETRY_SHADER_ARB;
				break;
			default:
				Error(LOCATION, "Unknown shader type %d! Get a coder!", (int) shaderType);
				type = GL_INVALID_ENUM;
			}

			GLhandleARB program = compile_object(source.c_str(), type);

			if (program != 0)
			{
				this->programs[shaderType] = program;
				return true;
			}
			else
			{
				return false;
			}
		}

		bool Shader::loadShaderFile(ShaderType shaderType, const SCP_string& filename)
		{
			SCP_string source;
			bool fromFile = false;

			if (Enable_external_shaders) {
				CFILE *cf_shader = cfopen(filename.c_str(), "rt", CFILE_NORMAL, CF_TYPE_EFFECTS);

				if (cf_shader != NULL) {
					source.resize(cfilelength(cf_shader));
				
					cfread(&source[0], 1, (int) source.size(), cf_shader);
					cfclose(cf_shader);

					fromFile = true;
				}
			}

			if (!fromFile)
			{
				//If we're still here, proceed with internals
				mprintf(("   Loading built-in default shader for: %s\n", filename.c_str()));
				source.assign(defaults_get_file(filename.c_str()));
			}

			SCP_string flaggedSource = addSourceDefinitions(source, flags);
		
			return loadShaderSource(shaderType, flaggedSource);
		}

		bool Shader::linkProgram()
		{
			GLint status = 0;

			shaderHandle = vglCreateProgramObjectARB();

			for (int i = 0; i < MAX_SHADER_TYPES; i++)
			{
				if (programs[i] != 0)
				{
					vglAttachObjectARB(shaderHandle, programs[i]);
				}
			}

			vglLinkProgramARB(shaderHandle);

			// check if the link was successful
			vglGetObjectParameterivARB(shaderHandle, GL_OBJECT_LINK_STATUS_ARB, &status);

			check_info_log(shaderHandle);

			for (int i = 0; i < MAX_SHADER_TYPES; i++)
			{
				if (programs[i] != 0)
				{
					vglDeleteObjectARB(programs[i]);
					programs[i] = 0;
				}
			}

			// we failed, bail out now...
			if (status == 0) {
				mprintf(("Shader failed to link:\n%s\n", GLshader_info_log));

				if (shaderHandle) {
					vglDeleteObjectARB(shaderHandle);
					shaderHandle = 0;
				}

				return false;
			}

			// we succeeded, maybe output warnings too
			if (strlen(GLshader_info_log) > 5) {
				nprintf(("SHADER-DEBUG", "Shader linked with warnings:\n%s\n", GLshader_info_log));
			}

			return true;
		}

		bool Shader::addUniform(const SCP_string& name)
		{
			Assertion(shaderHandle != 0, "Tried to add uniform '%s' to invalid or unlinked shader!", name.c_str());

			GLint location = vglGetUniformLocationARB(shaderHandle, name.c_str());

			if (location < 0) {
				return false;
			}

			Uniform uniform(this, name, location);

			uniforms[name] = uniform;

			return true;
		}

		Attribute& Shader::addAttribute(const SCP_string& name)
		{
			Assertion(shaderHandle != 0, "Tried to add attribute '%s' to invalid or unlinked shader!", name.c_str());

			GLint location = vglGetAttribLocationARB(shaderHandle, name.c_str());

			Assertion(location >= 0, "Failed to find attribute '%s' for shader '%s'!", name.c_str(), this->name.c_str());

			Attribute attrib(this, name, location);

			attributes[name] = attrib;

			return getAttribute(name);
		}

		void Uniform::setValue3f(float x, float y, float z)
		{
			vglUniform3fARB(location, x, y, z);
		}

		void Uniform::setValue2f(float x, float y)
		{
			vglUniform2fARB(location, x, y);
		}

		template<>
		void Uniform::setValue<int>(const int& value)
		{
			if (!valid) return;

			vglUniform1iARB(location, value);
		}

		template<>
		void Uniform::setValue<float>(const float& value)
		{
			if (!valid) return;

			vglUniform1fARB(location, value);
		}

		template<>
		void Uniform::setValue<vec2d>(const vec2d& value)
		{
			if (!valid) return;

			vglUniform2fARB(location, value.x, value.y);
		}

		template<>
		void Uniform::setValue<vec3d>(const vec3d& value)
		{
			if (!valid) return;

			vglUniform3fARB(location, value.xyz.x, value.xyz.y, value.xyz.z);
		}

		template<>
		void Uniform::setValue<matrix4>(const matrix4& value)
		{
			if (!valid) return;

			vglUniformMatrix4fvARB(location, 1, GL_FALSE, value.data);
		}

		Uniform invalidUniform;

		Shader& ShaderState::addShader(Shader& newShader)
		{
			shaders.push_back(newShader);

			return getShader(shaders.size() - 1);
		}

		void ShaderState::shutdown()
		{
			if (currentShader != NULL)
			{
				disableShader();
			}

			SCP_vector<Shader>::iterator iter;
			for (iter = shaders.begin(); iter != shaders.end(); ++iter)
			{
				iter->releaseResources();
			}

			shaders.clear();
		}

		void ShaderState::enableShader(Shader& shader)
		{
			vglUseProgramObjectARB(shader.getHandle());
			currentShader = &shader;

	#ifndef NDEBUG
			if (opengl_check_for_errors("shader_set_current()")) {
				vglValidateProgramARB(currentShader->getHandle());

				GLint obj_status = 0;
				vglGetObjectParameterivARB(currentShader->getHandle(), GL_OBJECT_VALIDATE_STATUS_ARB, &obj_status);

				if (!obj_status) {
					check_info_log(currentShader->getHandle());

					mprintf(("VALIDATE INFO-LOG:\n"));

					if (strlen(GLshader_info_log) > 5) {
						mprintf(("%s\n", GLshader_info_log));
					}
					else {
						mprintf(("<EMPTY>\n"));
					}
				}
			}
	#endif
		}

		void ShaderState::disableShader()
		{
			if (currentShader != NULL)
			{
				vglUseProgramObjectARB(0);
				currentShader = NULL;
			}
		}
	}
}
