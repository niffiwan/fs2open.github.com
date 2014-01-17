/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/


#ifndef _GROPENGLSHADER_H
#define _GROPENGLSHADER_H

#include "globalincs/pstypes.h"

#include "graphics/gropengl.h"

#include <string>

namespace opengl
{
	class Shader;

	class Uniform
	{
	private:
		SCP_string name;
		GLint location;
		Shader *parentShader;

	public:
		Uniform(Shader* parentShader, const SCP_string& name, GLint location)
			: parentShader(parentShader), name(name), location(location) {}

		Uniform() : name(""), location(-1), parentShader(NULL) {}

		inline GLint getLocation() const { return location; }

		inline const SCP_string& getName() const { return name; }

		template<class T>
		void setValue(size_t count, T* values);
	};

	class Attribute
	{
	private:
		SCP_string name;
		GLint location;
		Shader *parentShader;

	public:
		Attribute(Shader* parentShader, const SCP_string& name, GLint location)
			: parentShader(parentShader), name(name), location(location) {}

		Attribute() : name(""), location(-1), parentShader(NULL) {}

		inline GLint getLocation() const { return location; }

		inline const SCP_string& getName() const { return name; }
	};

	class Shader
	{
	public:
		enum ShaderType { VERTEX_SHADER, FRAGMENT_SHADER, GEOMETRY_SHADER, MAX_SHADER_TYPES };

	private:
		SCP_string name;
		GLhandleARB shaderHandle;
		bool enabled;
		GLhandleARB programs[MAX_SHADER_TYPES];

		SCP_hash_map<SCP_string, Uniform> uniforms;
		SCP_hash_map<SCP_string, Attribute> attributes;

	public:
		Shader::Shader(const SCP_string& name) : name(name), shaderHandle(0), enabled(false) {}
		~Shader();

		bool loadShaderSource(ShaderType shaderType, const SCP_string& source);
		bool linkProgram();

		void releaseResources();

		Uniform& addUniform(const SCP_string& name);
		Attribute& addAttribute(const SCP_string& name);

		inline GLhandleARB getHandle() const { return shaderHandle; }
		inline SCP_string getName() const { return name; }

		inline Attribute& getAttribute(const SCP_string& name)
		{
			Assertion(attributes.find(name) != attributes.end(), "Failed to find attribute '%s' in shader '%s'.", name.c_str(), this->name.c_str());

			return attributes[name];
		}

		inline Uniform& getUniform(const SCP_string& name)
		{
			Assertion(uniforms.find(name) != uniforms.end(), "Failed to find uniform '%s' in shader '%s'.", name.c_str(), this->name.c_str());

			return uniforms[name];
		}

		inline Uniform& operator[] (const SCP_string& name)
		{
			return getUniform(name);
		}
	};

	class ShaderManager
	{
	private:
		SCP_vector<Shader> shaders;
		Shader* currentShader;

	public:
		ShaderManager() : shaders(SCP_vector<Shader>()), currentShader(NULL) {}

		Shader& newShader(const SCP_string& name);

		void enableShader(Shader& shader);

		void disableShader(const Shader& shader);

		void shutdown();

		inline Shader* getCurrentShader() { return currentShader; }

		inline Shader& getShader(size_t index)
		{
			Assertion(index >= 0 && index < shaders.size(), "Invalid shader index %d!", index);

			return shaders[index];
		}
	};

	extern ShaderManager shaderManager;
}

#define MAX_SHADER_UNIFORMS		15

#define SDR_ATTRIB_RADIUS		0

#define MAX_SDR_ATTRIBUTES		1

struct opengl_shader_flag_t {
	char *vert;
	char *frag;

	int flags;
};

struct opengl_shader_uniform_reference_t {
	int flag;

	int num_uniforms;
	char* uniforms[MAX_SHADER_UNIFORMS];

	int num_attributes;
	char* attributes[MAX_SDR_ATTRIBUTES];

	const char* name;
};

typedef struct opengl_shader_uniform_t {
	SCP_string text_id;
	GLint location;

	opengl_shader_uniform_t() : location(-1) {}
} opengl_shader_uniform_t;

typedef struct opengl_shader_t {
	GLhandleARB program_id;
	int flags;
	int flags2;

	SCP_vector<opengl_shader_uniform_t> uniforms;
	SCP_vector<opengl_shader_uniform_t> attributes; // using the uniform data structure to keep track of vert attribs

	opengl_shader_t() :
		program_id(0), flags(0), flags2(0)
	{
	}
} opengl_shader_t;

extern SCP_vector<opengl_shader_t> GL_shader;

extern opengl_shader_t *Current_shader;

int gr_opengl_maybe_create_shader(int flags);
void opengl_shader_set_current(opengl_shader_t *shader_obj = NULL);

void opengl_shader_init();
void opengl_shader_shutdown();

void opengl_compile_main_shader(int flags);
GLhandleARB opengl_shader_create(const char *vs, const char *fs);

void opengl_shader_init_attribute(const char *attribute_text);
GLint opengl_shader_get_attribute(const char *attribute_text);

void opengl_shader_init_uniform(const char *uniform_text);
GLint opengl_shader_get_uniform(const char *uniform_text);

void opengl_shader_set_animated_effect(int effect);
int opengl_shader_get_animated_effect();
void opengl_shader_set_animated_timer(float timer);
float opengl_shader_get_animated_timer();

#define ANIMATED_SHADER_LOADOUTSELECT_FS1	0
#define ANIMATED_SHADER_LOADOUTSELECT_FS2	1
#define ANIMATED_SHADER_CLOAK				2

#endif	// _GROPENGLSHADER_H
