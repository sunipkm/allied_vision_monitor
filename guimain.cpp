// Dear ImGui: standalone example application for GLFW + OpenGL2, using legacy fixed pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

// **DO NOT USE THIS CODE IF YOUR CODE/ENGINE IS USING MODERN OPENGL (SHADERS, VBO, VAO, etc.)**
// **Prefer using the code in the example_glfw_opengl2/ folder**
// See imgui_impl_glfw.cpp for details.

#include <vector>

#include "imgui/imgui.h"
#include "backend/imgui_impl_glfw.h"
#include "backend/imgui_impl_opengl2.h"
#include <stdio.h>
#include <unistd.h>

#include "aDIO_library.h"

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#endif
#include <GLFW/glfw3.h>

#include "imagetexture.hpp"
#include "guiwin.hpp"
#include "camlist.hpp"

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

int main(int argc, char *argv[])
{
    // arguments
    std::string camera_id = "";
    int adio_minor_num = 0;
    bool adio_init = true;
    std::string cti_path = "";
    // process args
    int c;
    while ((c = getopt(argc, argv, "c:a:h:p:")) != -1)
    {
        switch (c)
        {
            case 'c':
            {
                printf("Camera ID from command line: %s\n", optarg);
                camera_id = optarg;
                break;
            }
            case 'a':
            {
                printf("ADIO minor number: %s\n", optarg);
                adio_minor_num = atoi(optarg);
                break;
            }
            case 'p':
            {
                printf("CTI path: %s\n", optarg);
                cti_path = optarg;
                break;
            }
            case 'h':
            default:
            {
                printf("\nUsage: %s [-c camera_id] [-a adio_minor_num] [-p /path/to/cti/files] [-h Show this message]\n\n", argv[0]);
                exit(EXIT_SUCCESS);
            }
        }
    }
    // setup ADIO API
    DeviceHandle adio_dev = nullptr;
    if (OpenDIO_aDIO(&adio_dev, adio_minor_num) != 0)
    {
        printf("Could not initialize ADIO API. Check if /dev/rtd-aDIO* exists. aDIO features will be disabled.\n");
        adio_init = false;
    }
    if (!adio_init)
    {
        free(adio_dev);
        adio_dev = nullptr;
    }
    else
    {
        // set up port A as output and set all bits to low
        int ret = LoadPort0BitDir_aDIO(adio_dev, 1, 1, 1, 1, 1, 1, 1, 1);
        if (ret == -1)
        {
            printf("Could not set PORT0 to output.\n");
            goto done_allied;
        }
        ret = WritePort_aDIO(adio_dev, 0, 0); // set all to low
        if (ret < 0)
        {
            printf("Could not set all PORT0 bits to LOW: %s [%d]\n", strerror(ret), ret);
        }
    }
done_allied:
    // setup allied camera API
    const char *cti_path_cstr = NULL;
    if (cti_path != "")
    {
        cti_path_cstr = cti_path.c_str();
    }
    if (allied_init_api(cti_path_cstr) != VmbErrorSuccess)
    {
        printf("Could not initialize the Allied Camera API. Check if .cti files are in path.\n");
        exit(EXIT_FAILURE);
    } 
    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Allied Vision Camera ViewFinder", NULL, NULL);
    if (window == NULL)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    io.Fonts->AddFontFromFileTTF("font/Inconsolata-Regular.ttf", 14.0f);
    //IM_ASSERT(font != NULL);

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    CameraList *camlist;
    camlist = new CameraList(camera_id, adio_dev);

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        camlist->render();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        // If you are using this code with non-legacy OpenGL header/contexts (which you should not, prefer using imgui_impl_opengl3.cpp!!),
        // you may need to backup/reset/restore current shader using the commented lines below.
        //GLint last_program;
        //glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
        //glUseProgram(0);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        //glUseProgram(last_program);

        glfwMakeContextCurrent(window);
        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    delete camlist;
    if (adio_dev != nullptr)
        CloseDIO_aDIO(adio_dev);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
