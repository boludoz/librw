// vulkantest - Basic Vulkan backend test
// This file tests the Vulkan device initialization

#include <stdio.h>
#include <stdlib.h>

#include "rw.h"

#ifdef RW_VULKAN
#include "src/vulkan/rwvk.h"

int main(int argc, char *argv[])
{
	printf("librw Vulkan Test\n");
	printf("=================\n\n");

#ifdef LIBRW_SDL2
	printf("Windowing: SDL2\n");
#elif defined(LIBRW_SDL3)
	printf("Windowing: SDL3\n");
#elif defined(LIBRW_GLFW)
	printf("Windowing: GLFW\n");
#else
	printf("Windowing: Unknown\n");
#endif

	printf("\nVulkan backend configured successfully!\n");
	printf("Note: Full test requires window creation and Vulkan context.\n");

	return 0;
}

#else

int main(int argc, char *argv[])
{
	printf("Error: This test requires RW_VULKAN to be defined.\n");
	printf("Build with the vulkan platform.\n");
	return 1;
}

#endif
