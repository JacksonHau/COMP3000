#include <iostream>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

const GLint WIDTH = 800, HEIGHT = 600;

int main() {
	// Initialise GLFW
	if (!glfwInit()) {
		std::cout << "GLFW Failed to initialise" << "\n";
		glfwTerminate();
		return 1;
	}

	// Setup window properties
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

	// Create the window
	GLFWwindow* Window = glfwCreateWindow(WIDTH, HEIGHT, "Test Window", NULL, NULL);

	if (!Window) {
		std::cout << "Failed to create window" << "\n";
		glfwTerminate();
		return 1;
	}

	// Get frame buffer size
	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(Window, &bufferWidth, &bufferHeight);

	// Set the context for GLEW to use
	glfwMakeContextCurrent(Window);

	// Init GLEW
	glewExperimental = GL_TRUE;

	if (glewInit() != GLEW_OK) {
		std::cout << "Failed to initialise GLEW!" << "\n";
		glfwDestroyWindow(Window);
		glfwTerminate();
		return 1;
	}

	// Viewport
	glViewport(0, 0, bufferWidth, bufferHeight);

	// Main loop
	while (!glfwWindowShouldClose(Window)) {
		// Get and handle user user input 
		glfwPollEvents();

		// Render
		// Ckear buffer
		glClearColor(1.0f, 0.0f, 0.0f, 255.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		glfwSwapBuffers(Window);
	}

	// Cleanup
	glfwDestroyWindow(Window);
	glfwTerminate();

	return 0;
}