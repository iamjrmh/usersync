#include "App.h"
#include "Gui.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#endif

#include <cstdio>

namespace {

void glfw_error(int code, const char* desc) {
    std::fprintf(stderr, "[glfw %d] %s\n", code, desc);
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    glfwSetErrorCallback(glfw_error);
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    GLFWwindow* win = glfwCreateWindow(1280, 800, "usersync", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

#ifdef _WIN32
    // GLFW doesn't auto-pick the embedded .rc icon. Pull it from the exe's
    // resources and attach it to the native HWND so the title bar, taskbar,
    // and Alt-Tab all show JURMRWEED.
    HWND hwnd = glfwGetWin32Window(win);
    if (hwnd) {
        HINSTANCE hinst = GetModuleHandleW(nullptr);
        HICON big = LoadIconW(hinst, MAKEINTRESOURCEW(1));
        HICON sml = LoadIconW(hinst, MAKEINTRESOURCEW(1));
        if (big) SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)big);
        if (sml) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)sml);
    }
#endif

    usersync::App app;
    usersync::Gui gui(app);
    gui.init(win);

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        gui.draw();

        ImGui::Render();

        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0x0D / 255.0f, 0x00 / 255.0f, 0x15 / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(win);
    }

    gui.shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
