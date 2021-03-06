#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cgutils/Shader.h>
#include <cgutils/utils.h>
#include <cgutils/ResourceManager.h>
#include <viewer/UIController.h>
#include <common/logger.h>
#include <imgui_impl/imgui_impl_glfw_gl3.h>

#include <stb_image.h>

#include <thread>

constexpr size_t DEFAULT_WIN_WIDTH = 1200;
constexpr size_t DEFAULT_WIN_HEIGHT = 800;

constexpr const char *WINDOW_TITLE = "Rewind viewer for Russian AI Cup";

GLFWwindow *setup_window();
void prepare_and_run_game_loop(GLFWwindow *window);

int main(int argc, char **argv) {
    loguru::init(argc, argv);
    loguru::add_file("rewindviewer-debug.log", loguru::Truncate, loguru::Verbosity_MAX);
    loguru::add_file("rewindviewer.log", loguru::Truncate, loguru::Verbosity_INFO);

    // Init GLFW
    LOG_INFO("Init GLFW")
    if (glfwInit() != GL_TRUE) {
        LOG_FATAL("Failed to initialize GLFW");
        return -1;
    }

    auto window = setup_window();
    if (!window) {
        LOG_FATAL("Cannot setup window");
        return -3;
    }

    LOG_INFO("Load OpenGL functions")
    if (!gladLoadGL()) {
        LOG_FATAL("Failed to load opengl");
        return -2;
    }

    LOG_INFO("OpenGL %s, GLSL %s", glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
    LOG_INFO("Driver %s, Renderer %s", glGetString(GL_VENDOR), glGetString(GL_RENDERER));

#ifdef OPENGL_DEBUG
#  if (GL_ARB_debug_output)
    LOG_INFO("OpenGL:: Debug output enabled");
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
    glDebugMessageCallbackARB(cg::debug_output_callback, nullptr);
    glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
#  endif
#endif

    LOG_INFO("Setup vertical sync to 60fps")
    glfwSwapInterval(1);
    try {
        LOG_INFO("Start main draw loop")
        prepare_and_run_game_loop(window);
    } catch (const std::exception &e) {
        LOG_ERROR("Exception:: %s", e.what());
    }

    glfwTerminate();
    return 0;
}

GLFWwindow *setup_window() {
    glfwSetErrorCallback([](int error, const char *description) {
        LOG_ERROR("GLFW error(%d): %s", error, description);
    });

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

#ifdef OPENGL_DEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif

    LOG_INFO("Create main window")
    GLFWwindow *window = glfwCreateWindow(DEFAULT_WIN_WIDTH, DEFAULT_WIN_HEIGHT, WINDOW_TITLE, nullptr, nullptr);
    if (!window) {
        return nullptr;
    }

    int width;
    int height;
    int nr_channels;
    auto icon_data = stbi_load("resources/icon.png", &width, &height, &nr_channels, 0);
    GLFWimage icon{width, height, icon_data};
    LOG_INFO("Setup application icon")
    glfwSetWindowIcon(window, 1, &icon);

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow *, int width, int height) {
        glViewport(0, 0, width, height);
    });

    return window;
}

void prepare_and_run_game_loop(GLFWwindow *window) {
    LOG_INFO("Create camera")
    Camera cam({730.0f, 445.0f}, 1000);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    LOG_INFO("Create Resource manager")
    ResourceManager res;
    LOG_INFO("Create Scene")
    Scene scene(&res);
    LOG_INFO("Create GUI controller")
    UIController ui(&cam);

    //Start network listening
    LOG_INFO("Start networking thread")
    NetListener net(&scene, "127.0.0.1", 7000);
    std::thread network_thread([&net] {
        try {
            net.run();
        } catch (const std::exception &ex) {
            LOG_ERROR("NetListener Exception:: %s", ex.what());
        }
    });
    network_thread.detach();

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    LOG_INFO("Start render loop")
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        //Updates
        ui.next_frame(&scene, net.connection_status());
        cam.update();

        if (ui.close_requested()) {
            net.stop();
            LOG_INFO("Exit from application");
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(100ms);
            glfwSetWindowShouldClose(window, true);
        }

        //Non Ui related drawing
        scene.update_and_render(cam.proj_view(), cam.y_axes_invert());

        // Cleanup opengl state
        glBindVertexArray(0);
        glUseProgram(0);

        // Render UI
        ui.frame_end();

        // Swap buffers
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
}