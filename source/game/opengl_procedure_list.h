// Shaders
GLProc(CREATESHADER, CreateShader)
GLProc(DELETESHADER, DeleteShader)
GLProc(COMPILESHADER, CompileShader)
GLProc(SHADERSOURCE, ShaderSource)
GLProc(CREATEPROGRAM, CreateProgram)
GLProc(DELETEPROGRAM, DeleteProgram)
GLProc(LINKPROGRAM, LinkProgram)
GLProc(VALIDATEPROGRAM, ValidateProgram)
GLProc(USEPROGRAM, UseProgram)
GLProc(ISPROGRAM, IsProgram)
GLProc(ATTACHSHADER, AttachShader)
GLProc(GETSHADERIV, GetShaderiv)
GLProc(GETPROGRAMIV, GetProgramiv)
GLProc(GETSHADERINFOLOG, GetShaderInfoLog)
GLProc(GETPROGRAMINFOLOG, GetProgramInfoLog)
GLProc(GETUNIFORMLOCATION, GetUniformLocation)
GLProc(UNIFORM1I, Uniform1i)
GLProc(UNIFORM1F, Uniform1f)
GLProc(UNIFORM2F, Uniform2f)
GLProc(UNIFORM3F, Uniform3f)
GLProc(UNIFORM4F, Uniform4f)
GLProc(UNIFORM2FV, Uniform2fv)
GLProc(UNIFORM4FV, Uniform4fv)
GLProc(UNIFORMMATRIX4FV, UniformMatrix4fv)
GLProc(BLENDFUNCSEPARATE, BlendFuncSeparate)

// Vertex arrays
GLProc(GENVERTEXARRAYS, GenVertexArrays)
GLProc(BINDVERTEXARRAY, BindVertexArray)
GLProc(ENABLEVERTEXATTRIBARRAY, EnableVertexAttribArray)
GLProc(VERTEXATTRIBPOINTER, VertexAttribPointer)
GLProc(VERTEXATTRIBDIVISOR, VertexAttribDivisor)

// Vertex buffers
GLProc(GENBUFFERS, GenBuffers)
GLProc(DELETEBUFFERS, DeleteBuffers)
GLProc(BINDBUFFER, BindBuffer)
GLProc(BUFFERDATA, BufferData)
GLProc(BUFFERSUBDATA, BufferSubData)

// Frame buffers
GLProc(GENFRAMEBUFFERS, GenFramebuffers)
GLProc(BINDFRAMEBUFFER, BindFramebuffer)
GLProc(CHECKFRAMEBUFFERSTATUS, CheckFramebufferStatus)
GLProc(DELETEFRAMEBUFFERS, DeleteFramebuffers)
GLProc(FRAMEBUFFERTEXTURE2D, FramebufferTexture2D)
GLProc(TEXIMAGE2DMULTISAMPLE, TexImage2DMultisample)
GLProc(BLITFRAMEBUFFER, BlitFramebuffer)

// Drawing
GLProc(DRAWARRAYSINSTANCED, DrawArraysInstanced)

// Type checks
GLProc(ISFRAMEBUFFER, IsFramebuffer)

// Debug
GLProc(DEBUGMESSAGECALLBACK, DebugMessageCallback)

#undef GLProc
