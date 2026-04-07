#include "runtime/renderer/RuntimeRenderer.h"

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

	// Full-screen runtime camera — no editor panels, no pan offset
	const float windowW = static_cast<float>(swapchainExtent.width);
	const float windowH = static_cast<float>(swapchainExtent.height);

	CameraUniforms cam{};
	cam.ndcCenterX = 0.f;
	cam.ndcCenterY = 0.f;
	cam.scaleX = 64.f / (windowW * 0.5f);
	cam.scaleY = 64.f / (windowH * 0.5f);
	cam.panX = 0.f;
	cam.panY = 0.f;

	BeginRenderPass(ctx, scene);
	DrawSprites(ctx, scene, cam);
	EndRenderPass(ctx);

	EndFrame(ctx);
}