#pragma once

#include "Framebuffer.h"
#include "Shader.h"

class PostEffect
{
public:
	//Initialize this effects (will be overriden in each derived class)
	virtual void Init(unsigned width, unsigned height);

	//Applies the effect
	virtual void ApplyEffect(PostEffect* previousBuffer);
	virtual void DrawToScreen();

	//Reshapes the buffer
	virtual void Reshape(unsigned width, unsigned height);

	//Clears the buffers
	void Clear();

	//Unloads all the buffers
	void Unload();

	//Binds buffers
	void BindBuffer(int index);
	void UnbindBuffer();

	//Bind textures
	void BindColorAsTexture(int index, int colorBuffer, int textureSlot);
	void BindDepthAsTexture(int index, int textureSlot);
	void UnbindTexture(int textureSlot);

	//Bind shaders
	void BindShader(int index);
	void UnbindShader();

protected:
	//Holds all our buffers for the effects
	std::vector<Framebuffer*> _buffers;

	//Holds all our shaders for the effects
	std::vector<Shader::sptr> _shaders;
};