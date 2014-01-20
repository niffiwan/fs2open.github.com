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

#include <boost/unordered_map.hpp>

#include <string>

namespace opengl
{
	namespace shader
	{
		class Shader;

		class Uniform
		{
		private:
			SCP_string name;
			GLint location;
			Shader *parentShader;

			bool valid;

		public:
			Uniform(Shader* parentShader, const SCP_string& name, GLint location)
				: parentShader(parentShader), name(name), location(location), valid(true) {}

			Uniform() : name(""), location(-1), parentShader(NULL), valid(false) {}

			inline GLint getLocation() const { return location; }

			inline const SCP_string& getName() const { return name; }

			template<class T>
			void setValue(const T& value);

			template<class T>
			void setValues(size_t count, T* value);

			void setValue3f(float x, float y, float z);

			void setValue2f(float x, float y);
		};

		extern Uniform invalidUniform;

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
			GLhandleARB programs[MAX_SHADER_TYPES];
			int flags;
			int flags2;

			boost::unordered_map<const char*, Uniform, boost::hash<const char*>> uniforms;
			boost::unordered_map<const char*, Attribute, boost::hash<const char*>> attributes;

			// Disallow allignment
			Shader& operator= (const Shader&);

		public:
			Shader::Shader(const SCP_string& name) : name(name), shaderHandle(0), flags(0), flags2(0)
			{
				for (int i = 0; i < MAX_SHADER_TYPES; i++)
				{
					programs[i] = 0;
				}
			}

			~Shader();

			bool loadShaderSource(ShaderType shaderType, const SCP_string& source);
			bool loadShaderFile(ShaderType shaderType, const SCP_string& filename);

			bool linkProgram();

			void addPrimaryFlag(int flags);
			void addSecondaryFlag(int flags);

			void releaseResources();

			bool addUniform(const char* name);
			bool addAttribute(const char* name);

			inline GLhandleARB getHandle() const { return shaderHandle; }
			inline SCP_string getName() const { return name; }
			inline int getPrimaryFlags() const { return flags; }
			inline int getSecondaryFlags() const { return flags2; }

			inline Attribute& getAttribute(const char* name)
			{
				Assertion(attributes.find(name) != attributes.end(), "Failed to find attribute '%s' in shader '%s'.", name, this->name.c_str());

				return attributes[name];
			}

			inline Uniform& getUniform(const char* name)
			{
				boost::unordered_map<const char*, Uniform>::iterator iter = uniforms.find(name);

				if (iter == uniforms.end())
				{
					nprintf(("SHADER-DEBUG", "WARNING: Unable to find uniform \"%s\" in shader \"%s\"!\n", name, this->name.c_str()));
					return invalidUniform;
				}
				else
				{
					return iter->second;
				}
			}
		};

		class ShaderState
		{
		private:
			SCP_vector<Shader> shaders;
			Shader* currentShader;

		public:
			ShaderState() : shaders(SCP_vector<Shader>()), currentShader(NULL) {}

			Shader& addShader(Shader& newShader);

			void enableShader(Shader& shader);

			void disableShader();

			void shutdown();

			inline const SCP_vector<Shader>& getShaderVector() const { return shaders; }

			inline Shader* getCurrentShader() { return currentShader; }

			inline Shader& getShader(size_t index)
			{
				Assertion(index >= 0 && index < shaders.size(), "Invalid shader index %d!", index);

				return shaders[index];
			}
		};

		enum AnimatedShader
		{
			ANIMATED_SHADER_LOADOUTSELECT_FS1,
			ANIMATED_SHADER_LOADOUTSELECT_FS2,
			ANIMATED_SHADER_CLOAK,
			ANIMATED_SHADER_NONE,
		};

		void init();
		void shutdown();

		void compile_main_shader(int flags);

		void set_animated_effect(AnimatedShader effect);
		AnimatedShader get_animated_effect();

		void set_animated_timer(float timer);
		float get_animated_timer();
	}
}

#define MAX_SHADER_UNIFORMS		15
#define MAX_SDR_ATTRIBUTES		1

int gr_opengl_maybe_create_shader(int flags);

#endif	// _GROPENGLSHADER_H
