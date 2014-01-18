
#include "graphics/gropengl.h"
#include "graphics/gropenglextension.h"
#include "graphics/gropenglpostprocessing.h"
#include "graphics/gropenglshader.h"
#include "graphics/gropenglstate.h"
#include "graphics/gropengldraw.h"

#include "io/timer.h"
#include "nebula/neb.h"
#include "parse/parselo.h"
#include "cmdline/cmdline.h"
#include "mod_table/mod_table.h"
#include "globalincs/def_files.h"
#include "ship/ship.h"
#include "freespace2/freespace.h"
#include "lighting/lighting.h"

using namespace opengl;
using namespace opengl::shader;

extern bool PostProcessing_override;
extern int opengl_check_framebuffer();

//Needed to track where the FXAA shaders are
size_t fxaa_shader_id;
//In case we don't find the shaders at all, this override is needed
bool fxaa_unavailable = false;
int Fxaa_preset_last_frame;
bool zbuffer_saved = false;

// lightshaft parameters
bool ls_on = false;
bool ls_force_off = false;
float ls_density = 0.5f;
float ls_weight = 0.02f;
float ls_falloff = 1.0f;
float ls_intensity = 0.5f;
float ls_cpintensity = 0.5f * 50 * 0.02f;
int ls_samplenum = 50;

#define SDR_POST_FLAG_MAIN		(1<<0)
#define SDR_POST_FLAG_BRIGHT	(1<<1)
#define SDR_POST_FLAG_BLUR		(1<<2)
#define SDR_POST_FLAG_PASS1		(1<<3)
#define SDR_POST_FLAG_PASS2		(1<<4)
#define SDR_POST_FLAG_LIGHTSHAFT (1<<5)

static SCP_vector<Shader> GL_post_shader;

struct opengl_shader_file_t {
	const char *vert;
	const char *frag;

	int flags;

	int num_uniforms;
	const char* uniforms[MAX_SHADER_UNIFORMS];

	int num_attributes;
	const char* attributes[MAX_SDR_ATTRIBUTES];
};

// NOTE: The order of this list *must* be preserved!  Additional shaders can be
//       added, but the first 7 are used with magic numbers so their position
//       is assumed to never change.
static opengl_shader_file_t GL_post_shader_files[] = {
	// NOTE: the main post-processing shader has any number of uniforms, but
	//       these few should always be present
	{ "post-v.sdr", "post-f.sdr", SDR_POST_FLAG_MAIN,
		5, { "tex", "depth_tex", "timer", "bloomed", "bloom_intensity" }, 0, { NULL } },

	{ "post-v.sdr", "blur-f.sdr", SDR_POST_FLAG_BLUR | SDR_POST_FLAG_PASS1,
		2, { "tex", "bsize" }, 0, { NULL } },

	{ "post-v.sdr", "blur-f.sdr", SDR_POST_FLAG_BLUR | SDR_POST_FLAG_PASS2,
		2, { "tex", "bsize" }, 0, { NULL } },

	{ "post-v.sdr", "brightpass-f.sdr", SDR_POST_FLAG_BRIGHT,
		1, { "tex" }, 0, { NULL } },

	{ "fxaa-v.sdr", "fxaa-f.sdr", 0, 
		3, { "tex0", "rt_w", "rt_h"}, 0, { NULL } },

	{ "post-v.sdr", "fxaapre-f.sdr", 0,
		1, { "tex"}, 0, { NULL } },

	{ "post-v.sdr", "ls-f.sdr", SDR_POST_FLAG_LIGHTSHAFT,
		8, { "scene", "cockpit", "sun_pos", "weight", "intensity", "falloff", "density", "cp_intensity" }, 0, { NULL } }
};

static const unsigned int Num_post_shader_files = sizeof(GL_post_shader_files) / sizeof(opengl_shader_file_t);

typedef struct post_effect_t {
	SCP_string name;
	SCP_string uniform_name;
	SCP_string define_name;

	float intensity;
	float default_intensity;
	float div;
	float add;

	bool always_on;

	post_effect_t() :
		intensity(0.0f), default_intensity(0.0f), div(1.0f), add(0.0f),
		always_on(false)
	{
	}
} post_effect_t;

SCP_vector<post_effect_t> Post_effects;

static int Post_initialized = 0;

bool Post_in_frame = false;

static int Post_active_shader_index = 0;

static GLuint Post_framebuffer_id[2] = { 0 };
static GLuint Post_bloom_texture_id[3] = { 0 };

static int Post_texture_width = 0;
static int Post_texture_height = 0;


static SCP_string opengl_post_load_shader(const char *filename, int flags, int flags2);


static bool opengl_post_pass_bloom()
{
	if (Cmdline_bloom_intensity <= 0) {
		return false;
	}

	// we need the scissor test disabled
	GLboolean scissor_test = GL_state.ScissorTest(GL_FALSE);

	// ------  begin bright pass ------

	vglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, Post_framebuffer_id[0]);

	// width and height are 1/2 for the bright pass
	int width = Post_texture_width >> 1;
	int height = Post_texture_height >> 1;

	glViewport(0, 0, width, height);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	Shader& renderShader = GL_post_shader[3];
	shaderManager.enableShader(renderShader);

	renderShader.getUniform("tex").setValue(0);

	GL_state.Texture.SetActiveUnit(0);
	GL_state.Texture.SetTarget(GL_TEXTURE_2D);
	GL_state.Texture.Enable(Scene_color_texture);

	opengl_draw_textured_quad(-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);

	GL_state.Texture.Disable();

	// ------ end bright pass ------


	// ------ begin blur pass ------

	GL_state.Texture.SetActiveUnit(0);
	GL_state.Texture.SetTarget(GL_TEXTURE_2D);

	// drop width and height once more for the blur passes
	width >>= 1;
	height >>= 1;

	glViewport(0, 0, width, height);

	vglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, Post_framebuffer_id[1]);

	for (int pass = 0; pass < 2; pass++) {
		vglFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, Post_bloom_texture_id[1+pass], 0);

		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		Shader& passShader = GL_post_shader[1 + pass];
		shaderManager.enableShader(passShader);

		passShader.getUniform("tex").setValue(0);
		passShader.getUniform("bsize").setValue((pass) ? (float)width : (float)height);

		GL_state.Texture.Enable(Post_bloom_texture_id[pass]);

		opengl_draw_textured_quad(-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
	}

	GL_state.Texture.Disable();

	// ------ end blur pass --------

	// reset viewport, scissor test and exit
	glViewport(0, 0, gr_screen.max_w, gr_screen.max_h);
	GL_state.ScissorTest(scissor_test);

	vglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, opengl_get_rtt_framebuffer());

	return true;
}

void gr_opengl_post_process_begin()
{
	if ( !Post_initialized ) {
		return;
	}

	if (Post_in_frame) {
		return;
	}

	if (PostProcessing_override) {
		return;
	}

	vglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, Post_framebuffer_id[0]);

//	vglFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, Post_renderbuffer_id);

//	vglFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, Post_screen_texture_id, 0);

//	Assert( !opengl_check_framebuffer() );

	GLenum buffers[] = { GL_COLOR_ATTACHMENT0_EXT, GL_COLOR_ATTACHMENT1_EXT };
	vglDrawBuffers(2, buffers);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	Post_in_frame = true;
}

void recompile_fxaa_shader() {	
	opengl_shader_file_t *shader_file = &GL_post_shader_files[4];

	// choose appropriate files
	const char *vert_name = shader_file->vert;
	const char *frag_name = shader_file->frag;

	mprintf(("Recompiling FXAA shader with preset %d\n", Cmdline_fxaa_preset));

	Shader& new_shader = GL_post_shader[fxaa_shader_id];

	// First release any resource which may have been allocated previously
	new_shader.releaseResources();

	// read vertex shader
	SCP_string vert = opengl_post_load_shader(vert_name, shader_file->flags, 0);

	// read fragment shader
	SCP_string frag = opengl_post_load_shader(frag_name, shader_file->flags, 0);

	if (!new_shader.loadShaderSource(Shader::VERTEX_SHADER, vert))
	{
		new_shader.releaseResources();
		return;
	}

	if (!new_shader.loadShaderSource(Shader::FRAGMENT_SHADER, frag))
	{
		new_shader.releaseResources();
		return;
	}

	if (!new_shader.linkProgram())
	{
		new_shader.releaseResources();
		return;
	}
	
	new_shader.addPrimaryFlag(shader_file->flags);

	shaderManager.enableShader(new_shader);

	for (int i = 0; i < shader_file->num_uniforms; i++) {
		new_shader.addUniform(shader_file->uniforms[i]);
	}

	shaderManager.disableShader();

	Fxaa_preset_last_frame = Cmdline_fxaa_preset;
}

void opengl_post_pass_fxaa() {

	//If the preset changed, recompile the shader
	if (Fxaa_preset_last_frame != Cmdline_fxaa_preset) {
		recompile_fxaa_shader();
	}

	// We only want to draw to ATTACHMENT0
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

	// Do a prepass to convert the main shaders' RGBA output into RGBL
	Shader& shader = GL_post_shader[fxaa_shader_id + 1];
	shaderManager.enableShader(shader);

	// basic/default uniforms
	shader.getUniform("tex").setValue(0);

	vglFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, Scene_luminance_texture, 0);

	GL_state.Texture.SetActiveUnit(0);
	GL_state.Texture.SetTarget(GL_TEXTURE_2D);
	GL_state.Texture.Enable(Scene_color_texture);

	opengl_draw_textured_quad(-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, Scene_texture_u_scale, Scene_texture_u_scale);

	GL_state.Texture.Disable();

	// set and configure post shader ..
	Shader& shader2 = GL_post_shader[fxaa_shader_id];
	shaderManager.enableShader(shader2);

	vglFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, Scene_color_texture, 0);

	// basic/default uniforms
	shader2.getUniform("tex0").setValue(0);
	shader2.getUniform("rt_w").setValue(static_cast<float>(Post_texture_width));
	shader2.getUniform("rt_h").setValue(static_cast<float>(Post_texture_height));

	GL_state.Texture.SetActiveUnit(0);
	GL_state.Texture.SetTarget(GL_TEXTURE_2D);
	GL_state.Texture.Enable(Scene_luminance_texture);

	opengl_draw_textured_quad(-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, Scene_texture_u_scale, Scene_texture_u_scale);

	GL_state.Texture.Disable();

	shaderManager.disableShader();
}

extern GLuint shadow_map[2];
extern GLuint Scene_depth_texture;
extern GLuint Cockpit_depth_texture;
extern bool stars_sun_has_glare(int index);
extern float Sun_spot;
void gr_opengl_post_process_end()
{
	// state switch just the once (for bloom pass and final render-to-screen)
	GLboolean depth = GL_state.DepthTest(GL_FALSE);
	GLboolean depth_mask = GL_state.DepthMask(GL_FALSE);
	GLboolean light = GL_state.Lighting(GL_FALSE);
	GLboolean blend = GL_state.Blend(GL_FALSE);
	GLboolean cull = GL_state.CullFace(GL_FALSE);

	GL_state.Texture.SetShaderMode(GL_TRUE);

	// Do FXAA
	if (Cmdline_fxaa && !fxaa_unavailable && !GL_rendering_to_texture) {
		opengl_post_pass_fxaa();
	}
	
	Shader& shader = GL_post_shader[6];
	shaderManager.enableShader(shader);
	float x,y;
	// should we even be here?
	if (!Game_subspace_effect && ls_on && !ls_force_off)
	{	
		int n_lights = light_get_global_count();
		
		for(int idx=0; idx<n_lights; idx++)
		{
			vec3d light_dir;
			vec3d local_light_dir;
			light_get_global_dir(&light_dir, idx);
			vm_vec_rotate(&local_light_dir, &light_dir, &Eye_matrix);
			if (!stars_sun_has_glare(idx))
				continue;
			float dot;
			if((dot=vm_vec_dot( &light_dir, &Eye_matrix.vec.fvec )) > 0.7f)
			{
				
				x = asin(vm_vec_dot( &light_dir, &Eye_matrix.vec.rvec ))/PI*1.5f+0.5f; //cant get the coordinates right but this works for the limited glare fov
				y = asin(vm_vec_dot( &light_dir, &Eye_matrix.vec.uvec ))/PI*1.5f*gr_screen.clip_aspect+0.5f;
				shader.getUniform("sun_pos").setValue2f(x, y);
				shader.getUniform("scene").setValue(0);
				shader.getUniform("cockpit").setValue(1);
				shader.getUniform("density").setValue(ls_density);
				shader.getUniform("falloff").setValue(ls_falloff);
				shader.getUniform("weight").setValue(ls_weight);
				shader.getUniform("intensity").setValue(Sun_spot * ls_intensity);
				shader.getUniform("cp_intensity").setValue(Sun_spot * ls_cpintensity);

				GL_state.Texture.SetActiveUnit(0);
				GL_state.Texture.SetTarget(GL_TEXTURE_2D);
				GL_state.Texture.Enable(Scene_depth_texture);
				GL_state.Texture.SetActiveUnit(1);
				GL_state.Texture.SetTarget(GL_TEXTURE_2D);
				GL_state.Texture.Enable(Cockpit_depth_texture);
				GL_state.Color(255, 255, 255, 255);
				GL_state.Blend(GL_TRUE);
				GL_state.SetAlphaBlendMode(ALPHA_BLEND_ADDITIVE);
				
				opengl_draw_textured_quad(-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, Scene_texture_u_scale, Scene_texture_u_scale);

				GL_state.Blend(GL_FALSE);
				break;
			}
		}
	}
	if(zbuffer_saved)
	{
		zbuffer_saved = false;
		gr_zbuffer_set(GR_ZBUFF_FULL);
		glClear(GL_DEPTH_BUFFER_BIT);
		gr_zbuffer_set(GR_ZBUFF_NONE);
		vglFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_TEXTURE_2D, Scene_depth_texture, 0);
	}
	
	// Bind the correct framebuffer. opengl_get_rtt_framebuffer returns 0 if not doing RTT
	vglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, opengl_get_rtt_framebuffer());

	// do bloom, hopefully ;)
	bool bloomed = opengl_post_pass_bloom();

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	GL_state.Color(255, 255, 255, 255);

	// set and configure post shader ...

	Shader& activeShader = GL_post_shader[Post_active_shader_index];
	shaderManager.enableShader(activeShader);

	// basic/default uniforms
	activeShader.getUniform("tex").setValue(0);
	activeShader.getUniform("depth_tex").setValue(2);
	activeShader.getUniform("timer").setValue(static_cast<float>(timer_get_milliseconds() % 100 + 1));

	for (size_t idx = 0; idx < Post_effects.size(); idx++) {
		if ( GL_post_shader[Post_active_shader_index].getSecondaryFlags() & (1<<idx) ) {
			float value = Post_effects[idx].intensity;
			
			activeShader.getUniform(Post_effects[idx].uniform_name).setValue(value);
		}
	}

	// bloom uniforms, but only if we did the bloom
	if (bloomed) {
		float intensity = MIN((float)Cmdline_bloom_intensity, 200.0f) * 0.01f;

		if (Neb2_render_mode != NEB2_RENDER_NONE) {
			// we need less intensity for full neb missions, so cut it by 30%
			intensity /= 3.0f;
		}

		activeShader.getUniform("bloom_intensity").setValue(intensity);

		activeShader.getUniform("bloomed").setValue(1);

		GL_state.Texture.SetActiveUnit(1);
		GL_state.Texture.SetTarget(GL_TEXTURE_2D);
		GL_state.Texture.Enable(Post_bloom_texture_id[2]);
	}

	// now render it to the screen ...

	GL_state.Texture.SetActiveUnit(0);
	GL_state.Texture.SetTarget(GL_TEXTURE_2D);
	GL_state.Texture.Enable(Scene_color_texture);

	GL_state.Texture.SetActiveUnit(2);
	GL_state.Texture.SetTarget(GL_TEXTURE_2D);
	GL_state.Texture.Enable(Scene_depth_texture);

	opengl_draw_textured_quad(-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, Scene_texture_u_scale, Scene_texture_u_scale);
	// Done!

	GL_state.Texture.SetActiveUnit(2);
	GL_state.Texture.Disable();
	GL_state.Texture.SetActiveUnit(1);	
	GL_state.Texture.Disable();
	GL_state.Texture.SetActiveUnit(0);
	GL_state.Texture.Disable();

	GL_state.Texture.SetShaderMode(GL_FALSE);

	// reset state
	GL_state.DepthTest(depth);
	GL_state.DepthMask(depth_mask);
	GL_state.Lighting(light);
	GL_state.Blend(blend);
	GL_state.CullFace(cull);

	Post_in_frame = false;

	shaderManager.disableShader();
}

void get_post_process_effect_names(SCP_vector<SCP_string> &names) 
{
	size_t idx;

	for (idx = 0; idx < Post_effects.size(); idx++) {
		names.push_back(Post_effects[idx].name);
	}
}

static bool opengl_post_compile_shader(int flags)
{
	bool in_error = false;

	opengl_shader_file_t *shader_file = &GL_post_shader_files[0];
	int num_main_uniforms = 0;
	int idx;

	for (idx = 0; idx < (int)Post_effects.size(); idx++) {
		if ( flags & (1 << idx) ) {
			num_main_uniforms++;
		}
	}

	// choose appropriate files
	const char *vert_name = shader_file->vert;
	const char *frag_name = shader_file->frag;

	mprintf(("POST-PROCESSING: Compiling new post-processing shader with flags %d ... \n", flags));

	Shader postShader("Post-processing shader");

	// read vertex shader
	SCP_string vert = opengl_post_load_shader(vert_name, shader_file->flags, flags);

	// read fragment shader
	SCP_string frag = opengl_post_load_shader(frag_name, shader_file->flags, flags);

	if (!postShader.loadShaderSource(Shader::VERTEX_SHADER, vert))
	{
		in_error = true;
	}

	if (!postShader.loadShaderSource(Shader::FRAGMENT_SHADER, frag))
	{
		in_error = true;
	}
	
	if (!in_error)
	{
		if (!postShader.linkProgram())
		{
			in_error = true;
		}
	}

	if (!in_error)
	{
		postShader.addPrimaryFlag(shader_file->flags);
		postShader.addSecondaryFlag(flags);

		shaderManager.enableShader(postShader);

		for (idx = 0; idx < shader_file->num_uniforms; idx++) {
			postShader.addUniform(shader_file->uniforms[idx]);
		}

		for (idx = 0; idx < (int)Post_effects.size(); idx++) {
			if ( flags & (1 << idx) ) {
				postShader.addUniform(Post_effects[idx].uniform_name);
			}
		}

		shaderManager.disableShader();

		// add it to our list of embedded shaders
		GL_post_shader.push_back(postShader);
	}
	else
	{
		postShader.releaseResources();
	}

	return in_error;
}

void gr_opengl_post_process_set_effect(const char *name, int value)
{
	if ( !Post_initialized ) {
		return;
	}

	if (name == NULL) {
		return;
	}

	size_t idx;
	int sflags = 0;
	bool need_change = true;

	if(!stricmp("lightshafts",name))
	{
		ls_intensity = value / 100.0f;
		ls_on = !!value;
		return;
	}

	for (idx = 0; idx < Post_effects.size(); idx++) {
		const char *eff_name = Post_effects[idx].name.c_str();

		if ( !stricmp(eff_name, name) ) {
			Post_effects[idx].intensity = (value / Post_effects[idx].div) + Post_effects[idx].add;
			break;
		}
	}

	// figure out new flags
	for (idx = 0; idx < Post_effects.size(); idx++) {
		if ( Post_effects[idx].always_on || (Post_effects[idx].intensity != Post_effects[idx].default_intensity) ) {
			sflags |= (1<<idx);
		}
	}

	// see if any existing shader has those flags
	for (idx = 0; idx < GL_post_shader.size(); idx++) {
		if (GL_post_shader[idx].getSecondaryFlags() == sflags) {
			// no change required
			need_change = false;

			// set this as the active post shader
			Post_active_shader_index = (int)idx;

			break;
		}
	}

	// if not then add a new shader to the list
	if (need_change) {
		if ( !opengl_post_compile_shader(sflags) ) {
			// shader added, set it as active
			Post_active_shader_index = (int)(GL_post_shader.size() - 1);
		} else {
			// failed to load, just go with default
			Post_active_shader_index = 0;
		}
	}
}

void gr_opengl_post_process_set_defaults()
{
	size_t idx, list_size;

	if ( !Post_initialized ) {
		return;
	}

	// reset all effects to their default values
	for (idx = 0; idx < Post_effects.size(); idx++) {
		Post_effects[idx].intensity = Post_effects[idx].default_intensity;
	}

	// remove any post shaders created on-demand, leaving only the defaults
	list_size = GL_post_shader.size();

	for (idx = list_size-1; idx > 0; idx--) {
		if ( !(GL_post_shader[idx].getPrimaryFlags() & SDR_POST_FLAG_MAIN) ) {
			break;
		}

		GL_post_shader[idx].releaseResources();

		GL_post_shader.pop_back();
	}

	Post_active_shader_index = 0;
}

extern GLuint Cockpit_depth_texture;
void gr_opengl_post_process_save_zbuffer()
{
	if (Post_initialized)
	{
		vglFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_TEXTURE_2D, Cockpit_depth_texture, 0);
		gr_zbuffer_clear(TRUE);
		zbuffer_saved = true;
	}
	else
	{
		// If we can't save the z-buffer then just clear it so cockpits are still rendered correctly when
		// post-processing isn't available/enabled.
		gr_zbuffer_clear(TRUE);
	}
}


static bool opengl_post_init_table()
{
	int rval;
	bool warned = false;

	if ( (rval = setjmp(parse_abort)) != 0 ) {
		mprintf(("Unable to parse 'post_processing.tbl'!  Error code = %d.\n", rval));
		return false;
	}

	if (cf_exists_full("post_processing.tbl", CF_TYPE_TABLES))
		read_file_text("post_processing.tbl", CF_TYPE_TABLES);
	else
		read_file_text_from_array(defaults_get_file("post_processing.tbl"));

	reset_parse();


	if (optional_string("#Effects")) {
		while ( !required_string_3("$Name:", "#Ship Effects", "#End") ) {
			char tbuf[NAME_LENGTH+1] = { 0 };
			post_effect_t eff;

			required_string("$Name:");
			stuff_string(tbuf, F_NAME, NAME_LENGTH);
			eff.name = tbuf;

			required_string("$Uniform:");
			stuff_string(tbuf, F_NAME, NAME_LENGTH);
			eff.uniform_name = tbuf;

			required_string("$Define:");
			stuff_string(tbuf, F_NAME, NAME_LENGTH);
			eff.define_name = tbuf;

			required_string("$AlwaysOn:");
			stuff_boolean(&eff.always_on);

			required_string("$Default:");
			stuff_float(&eff.default_intensity);
			eff.intensity = eff.default_intensity;

			required_string("$Div:");
			stuff_float(&eff.div);

			required_string("$Add:");
			stuff_float(&eff.add);

			// Post_effects index is used for flag checks, so we can't have more than 32
			if (Post_effects.size() < 32) {
				Post_effects.push_back( eff );
			} else if ( !warned ) {
				mprintf(("WARNING: post_processing.tbl can only have a max of 32 effects! Ignoring extra...\n"));
				warned = true;
			}
		}
	}

	//Built-in per-ship effects
	ship_effect se1;
	strcpy_s(se1.name, "FS1 Ship select");
	se1.shader_effect = ANIMATED_SHADER_LOADOUTSELECT_FS1;
	se1.disables_rendering = false;
	se1.invert_timer = false;
	Ship_effects.push_back(se1);

	if (optional_string("#Ship Effects")) {
		while ( !required_string_3("$Name:", "#Light Shafts", "#End") ) {
			ship_effect se;
			char tbuf[NAME_LENGTH] = { 0 };

			required_string("$Name:");
			stuff_string(tbuf, F_NAME, NAME_LENGTH);
			strcpy_s(se.name, tbuf);

			required_string("$Shader Effect:");

			int temp;
			stuff_int(&temp);

			se.shader_effect = static_cast<AnimatedShader>(temp);

			required_string("$Disables Rendering:");
			stuff_boolean(&se.disables_rendering);

			required_string("$Invert timer:");
			stuff_boolean(&se.invert_timer);

			Ship_effects.push_back(se);
		}
	}

	if (optional_string("#Light Shafts")) {
		required_string("$AlwaysOn:");
		stuff_boolean(&ls_on);
		required_string("$Density:");
		stuff_float(&ls_density);
		required_string("$Falloff:");
		stuff_float(&ls_falloff);
		required_string("$Weight:");
		stuff_float(&ls_weight);
		required_string("$Intensity:");
		stuff_float(&ls_intensity);
		required_string("$Sample Number:");
		stuff_int(&ls_samplenum);

		ls_cpintensity = ls_weight;
		for(int i = 1; i < ls_samplenum; i++)
			ls_cpintensity += ls_weight * pow(ls_falloff, i);
		ls_cpintensity *= ls_intensity;
	}
	
	required_string("#End");

	return true;
}

static SCP_string opengl_post_load_shader(const char *filename, int flags, int flags2)
{
	SCP_stringstream ss;

	if (Use_GLSL >= 4) {
		ss << "#define SHADER_MODEL 4\n";
	} else if (Use_GLSL == 3) {
		ss << "#define SHADER_MODEL 3\n";
	} else {
		ss << "#define SHADER_MODEL 2\n";
	}

	for (size_t idx = 0; idx < Post_effects.size(); idx++) {
		if ( flags2 & (1 << idx) ) {
			ss << "#define ";
			ss << Post_effects[idx].define_name.c_str();
			ss << "\n";
		}
	}

	if (flags & SDR_POST_FLAG_PASS1) {
		ss << "#define PASS_0\n";
	} else if (flags & SDR_POST_FLAG_PASS2) {
		ss << "#define PASS_1\n";
	}

	if (flags & SDR_POST_FLAG_LIGHTSHAFT) {
		char temp[42];
		sprintf(temp, "#define SAMPLE_NUM %d\n", ls_samplenum);
		ss << temp;
	}
	
	switch (Cmdline_fxaa_preset) {
		case 0:
			ss << "#define FXAA_QUALITY_PRESET 10\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD (1.0/6.0)\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD_MIN (1.0/12.0)\n";
			ss << "#define FXAA_QUALITY_SUBPIX 0.33\n";
			break;
		case 1:
			ss << "#define FXAA_QUALITY_PRESET 11\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD (1.0/7.0)\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD_MIN (1.0/14.0)\n";
			ss << "#define FXAA_QUALITY_SUBPIX 0.33\n";
			break;
		case 2:
			ss << "#define FXAA_QUALITY_PRESET 12\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD (1.0/8.0)\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD_MIN (1.0/16.0)\n";
			ss << "#define FXAA_QUALITY_SUBPIX 0.33\n";
			break;
		case 3:
			ss << "#define FXAA_QUALITY_PRESET 13\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD (1.0/9.0)\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD_MIN (1.0/18.0)\n";
			ss << "#define FXAA_QUALITY_SUBPIX 0.33\n";
			break;
		case 4:
			ss << "#define FXAA_QUALITY_PRESET 14\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD (1.0/10.0)\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD_MIN (1.0/20.0)\n";
			ss << "#define FXAA_QUALITY_SUBPIX 0.33\n";
			break;
		case 5:
			ss << "#define FXAA_QUALITY_PRESET 25\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD (1.0/11.0)\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD_MIN (1.0/22.0)\n";
			ss << "#define FXAA_QUALITY_SUBPIX 0.33\n";
			break;
		case 6:
			ss << "#define FXAA_QUALITY_PRESET 26\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD (1.0/12.0)\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD_MIN (1.0/24.0)\n";
			ss << "#define FXAA_QUALITY_SUBPIX 0.33\n";
			break;
		case 7:
			ss << "#define FXAA_PC 1\n";
			ss << "#define FXAA_QUALITY_PRESET 27\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD (1.0/13.0)\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD_MIN (1.0/26.0)\n";
			ss << "#define FXAA_QUALITY_SUBPIX 0.33\n";
			break;
		case 8:
			ss << "#define FXAA_QUALITY_PRESET 28\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD (1.0/14.0)\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD_MIN (1.0/28.0)\n";
			ss << "#define FXAA_QUALITY_SUBPIX 0.33\n";
			break;
		case 9:
			ss << "#define FXAA_QUALITY_PRESET 39\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD (1.0/15.0)\n";
			ss << "#define FXAA_QUALITY_EDGE_THRESHOLD_MIN (1.0/32.0)\n";
			ss << "#define FXAA_QUALITY_SUBPIX 0.33\n";
			break;
	}
	
	SCP_string shaderSource;

	if (Enable_external_shaders && stricmp(filename, "fxaapre-f.sdr") && stricmp(filename, "fxaa-f.sdr") && stricmp(filename, "fxaa-v.sdr")) {
		CFILE *cf_shader = cfopen(filename, "rt", CFILE_NORMAL, CF_TYPE_EFFECTS);

		if (cf_shader != NULL  ) {
			shaderSource.resize(cfilelength(cf_shader));

			cfread(&shaderSource[0], 1, shaderSource.size(), cf_shader);
			cfclose(cf_shader);

			ss << shaderSource;

			return ss.str();
		} 
	}

	mprintf(("   Loading built-in default shader for: %s\n", filename));
	char* def_shader = defaults_get_file(filename);
	shaderSource.assign(def_shader);

	ss << shaderSource;
	return ss.str();
}

static bool opengl_post_init_shader()
{
	bool rval = true;
	int flags2 = 0;
	int num_main_uniforms = 0;

	for (int idx = 0; idx < (int)Post_effects.size(); idx++) {
		if (Post_effects[idx].always_on) {
			flags2 |= (1 << idx);
			num_main_uniforms++;
		}
	}

	for (int idx = 0; idx < (int)Num_post_shader_files; idx++) {
		bool in_error = false;

		opengl_shader_file_t *shader_file = &GL_post_shader_files[idx];

		// choose appropriate files
		const char *vert_name = shader_file->vert;
		const char *frag_name = shader_file->frag;

		mprintf(("  Compiling post-processing shader %d ... \n", idx+1));

		Shader postShader("Post-processing shader");

		// read vertex shader
		SCP_string vert = opengl_post_load_shader(vert_name, shader_file->flags, flags2);

		// read fragment shader
		SCP_string frag = opengl_post_load_shader(frag_name, shader_file->flags, flags2);

		if (!postShader.loadShaderSource(Shader::VERTEX_SHADER, vert))
		{
			in_error = true;
		}

		if (!postShader.loadShaderSource(Shader::FRAGMENT_SHADER, frag))
		{
			in_error = true;
		}

		if (!in_error)
		{
			if (!postShader.linkProgram())
			{
				in_error = true;
			}
		}

		if (!in_error)
		{
			postShader.addPrimaryFlag(shader_file->flags);
			postShader.addSecondaryFlag(flags2);

			shaderManager.enableShader(postShader);

			for (int uniform = 0; uniform < shader_file->num_uniforms; uniform++) {
				postShader.addUniform(shader_file->uniforms[uniform]);
			}

			if (idx == 0) {
				for (int i = 0; i < (int)Post_effects.size(); i++) {
					if (flags2 & (1 << i)) {
						postShader.addUniform(Post_effects[i].uniform_name);
					}
				}

				flags2 = 0;
				num_main_uniforms = 0;
			}

			shaderManager.disableShader();

			// add it to our list of embedded shaders
			GL_post_shader.push_back(postShader);

			if (idx == 4)
				fxaa_shader_id = GL_post_shader.size() - 1;
		}
		else
		{
			postShader.releaseResources();
		}

		if (in_error) {
			if (idx == 0) {
				// only the main/first shader is actually required for post-processing
				rval = false;
				break;
			} else if (idx == 4) {
				Cmdline_fxaa = false;
				fxaa_unavailable = true;
				mprintf(("Error while compiling FXAA shaders. FXAA will be unavailable.\n"));
			} else if ( shader_file->flags & (SDR_POST_FLAG_BLUR|SDR_POST_FLAG_BRIGHT) ) {
				// disable bloom if we don't have those shaders available
				Cmdline_bloom_intensity = 0;
			}
		}
	}

	mprintf(("\n"));

	return rval;
}

// generate and test the framebuffer and textures that we are going to use
static bool opengl_post_init_framebuffer()
{
	bool rval = false;

	// clamp size, if needed
	Post_texture_width = gr_screen.max_w;
	Post_texture_height = gr_screen.max_h;

	if (Post_texture_width > GL_max_renderbuffer_size) {
		Post_texture_width = GL_max_renderbuffer_size;
	}

	if (Post_texture_height > GL_max_renderbuffer_size) {
		Post_texture_height = GL_max_renderbuffer_size;
	}

	if (Cmdline_bloom_intensity > 0) {
		// two more framebuffers, one each for the two different sized bloom textures
		vglGenFramebuffersEXT(1, &Post_framebuffer_id[0]);
		vglGenFramebuffersEXT(1, &Post_framebuffer_id[1]);

		// need to generate textures for bloom too
		glGenTextures(3, Post_bloom_texture_id);

		// half size
		int width = Post_texture_width >> 1;
		int height = Post_texture_height >> 1;

		for (int tex = 0; tex < 3; tex++) {
			GL_state.Texture.SetActiveUnit(0);
			GL_state.Texture.SetTarget(GL_TEXTURE_2D);
			GL_state.Texture.Enable(Post_bloom_texture_id[tex]);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);

			if (tex == 0) {
				// attach to our bright pass framebuffer and make sure it's ok
				vglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, Post_framebuffer_id[0]);
				vglFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, Post_bloom_texture_id[tex], 0);

				// if not then clean up and disable bloom
				if ( opengl_check_framebuffer() ) {
					vglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
					vglDeleteFramebuffersEXT(1, &Post_framebuffer_id[0]);
					vglDeleteFramebuffersEXT(1, &Post_framebuffer_id[1]);
					Post_framebuffer_id[0] = 0;
					Post_framebuffer_id[1] = 0;

					glDeleteTextures(3, Post_bloom_texture_id);
					memset(Post_bloom_texture_id, 0, sizeof(Post_bloom_texture_id));

					Cmdline_bloom_intensity = 0;

					break;
				}

				// width and height are 1/2 for the bright pass, 1/4 for the blur, so drop down
				width >>= 1;
				height >>= 1;
			} else {
				// attach to our blur pass framebuffer and make sure it's ok
				vglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, Post_framebuffer_id[1]);
				vglFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, Post_bloom_texture_id[tex], 0);

				// if not then clean up and disable bloom
				if ( opengl_check_framebuffer() ) {
					vglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
					vglDeleteFramebuffersEXT(1, &Post_framebuffer_id[0]);
					vglDeleteFramebuffersEXT(1, &Post_framebuffer_id[1]);
					Post_framebuffer_id[0] = 0;
					Post_framebuffer_id[1] = 0;

					glDeleteTextures(3, Post_bloom_texture_id);
					memset(Post_bloom_texture_id, 0, sizeof(Post_bloom_texture_id));

					Cmdline_bloom_intensity = 0;

					break;
				}
			}
		}
	}

	vglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

	GL_state.Texture.Disable();

	rval = true;
	
	if ( opengl_check_for_errors("post_init_framebuffer()") ) {
		rval = false;
	}

	return rval;
}

void opengl_post_process_init()
{
	Post_initialized = 0;

	//We need to read the tbl first. This is mostly for FRED's benefit, as otherwise the list of post effects for the sexp doesn't get updated.
	if ( !opengl_post_init_table() ) {
		mprintf(("  Unable to read post-processing table! Disabling post-processing...\n\n"));
		Cmdline_postprocess = 0;
		return;
	}

	if ( !Cmdline_postprocess ) {
		return;
	}

	if ( !Scene_texture_initialized ) {
		return;
	}

	if ( !Use_GLSL || Cmdline_no_fbo || !Is_Extension_Enabled(OGL_EXT_FRAMEBUFFER_OBJECT) ) {
		Cmdline_postprocess = 0;
		return;
	}

	// for ease of use we require support for non-power-of-2 textures in one
	// form or another:
	//    - the NPOT extension
	//    - GL version 2.0+ (which should work for non-reporting ATI cards since we don't use mipmaps)
	if ( !(Is_Extension_Enabled(OGL_ARB_TEXTURE_NON_POWER_OF_TWO) || (GL_version >= 20)) ) {
		Cmdline_postprocess = 0;
		return;
	}

	if ( !opengl_post_init_shader() ) {
		mprintf(("  Unable to initialize post-processing shaders! Disabling post-processing...\n\n"));
		Cmdline_postprocess = 0;
		return;
	}

	if ( !opengl_post_init_framebuffer() ) {
		mprintf(("  Unable to initialize post-processing framebuffer! Disabling post-processing...\n\n"));
		Cmdline_postprocess = 0;
		return;
	}

	Post_initialized = 1;
}

void opengl_post_process_shutdown()
{
	if ( !Post_initialized ) {
		return;
	}

	for (size_t i = 0; i < GL_post_shader.size(); i++) {
		GL_post_shader[i].releaseResources();
	}

	GL_post_shader.clear();

	if (Post_bloom_texture_id[0]) {
		glDeleteTextures(3, Post_bloom_texture_id);
		memset(Post_bloom_texture_id, 0, sizeof(Post_bloom_texture_id));
	}

	if (Post_framebuffer_id[0]) {
		vglDeleteFramebuffersEXT(1, &Post_framebuffer_id[0]);
		Post_framebuffer_id[0] = 0;

		if (Post_framebuffer_id[1]) {
			vglDeleteFramebuffersEXT(1, &Post_framebuffer_id[1]);
			Post_framebuffer_id[1] = 0;
		}
	}

	Post_effects.clear();

	Post_in_frame = false;
	Post_active_shader_index = 0;

	Post_initialized = 0;
}
