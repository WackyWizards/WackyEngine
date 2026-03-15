#include "runtime/RuntimeRenderer.h"

RuntimeRenderer::RuntimeRenderer(GLFWwindow* w)
	: VulkanBase(w)
{
	// VulkanBase constructor handles all setup.
	// No validation layers, no wireframe, no UI.
}

void RuntimeRenderer::DrawFrame(const SpriteList& scene)
{
	FrameContext ctx;
	if (!BeginFrame(ctx))
	{
		return;
	}

	BeginRenderPass(ctx, scene);
	DrawSprites(ctx, scene);
	EndRenderPass(ctx);

	EndFrame(ctx);
}