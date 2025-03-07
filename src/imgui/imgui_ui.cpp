#include "imgui_ui.h"
#include <rdp/parallel_rdp_wrapper.h>
#include <volk.h>
#include <imgui.h>
#include <implot.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <nfd.hpp>

#include <frontend/render_internal.h>
#include <metrics.h>
#include <mem/pif.h>
#include <mem/mem_util.h>
#include <cpu/dynarec/dynarec.h>
#include <frontend/audio.h>
#include <frontend/render.h>

static bool show_metrics_window = false;
static bool show_imgui_demo_window = false;
static bool show_settings_window = false;

static bool is_fullscreen = false;

#define METRICS_HISTORY_SECONDS 5

#define METRICS_HISTORY_ITEMS ((METRICS_HISTORY_SECONDS) * 60)

template<typename T>
struct RingBuffer {
    int offset;
    T data[METRICS_HISTORY_ITEMS];
    RingBuffer() {
        offset = 0;
        memset(data, 0, sizeof(data));
    }

    T max() {
        T max_ = 0;
        for (int i = 0; i < METRICS_HISTORY_ITEMS; i++) {
            if (data[i] > max_) {
                max_ = data[i];
            }
        }
        return max_;
    }

    void add_point(T point) {
        data[offset++] = point;
        offset %= METRICS_HISTORY_ITEMS;
    }
};

RingBuffer<double> frame_times;
RingBuffer<ImU64> block_complilations;
RingBuffer<ImU64> rsp_steps;
RingBuffer<ImU64> codecache_bytes_used;
RingBuffer<ImU64> audiostream_bytes_available;
RingBuffer<ImU64> si_interrupts;
RingBuffer<ImU64> pi_interrupts;
RingBuffer<ImU64> ai_interrupts;
RingBuffer<ImU64> dp_interrupts;
RingBuffer<ImU64> sp_interrupts;


void render_menubar() {
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Load ROM")) {
                nfdchar_t* rom_path;
                nfdfilteritem_t filters[] = {{"N64 ROMs", "n64,v64,z64,N64,V64,Z64"}};
                if(NFD_OpenDialog(&rom_path, filters, 1, nullptr) == NFD_OKAY) {
                    reset_n64system();
                    n64_load_rom(rom_path);
                    NFD_FreePath(rom_path);
                    pif_rom_execute();
                }

            }

            if (ImGui::MenuItem("Save RDRAM dump (big endian)")) {
#ifdef N64_BIG_ENDIAN
                save_rdram_dump(false);
#else
                save_rdram_dump(true);
#endif
            }

            if (ImGui::MenuItem("Save RDRAM dump (little endian)")) {
#ifdef N64_BIG_ENDIAN
                save_rdram_dump(true);
#else
                save_rdram_dump(false);
#endif
            }

            if (ImGui::MenuItem("Quit")) {
                n64_request_quit();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Emulation")) {
            if(ImGui::MenuItem("Reset")) {
                if(strcmp(n64sys.rom_path, "") != 0) {
                    reset_n64system();
                    n64_load_rom(n64sys.rom_path);
                    pif_rom_execute();
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window"))
        {
            if (ImGui::MenuItem("Metrics", nullptr, show_metrics_window)) { show_metrics_window = !show_metrics_window; }
            if (ImGui::MenuItem("Settings", nullptr, show_settings_window)) { show_settings_window = !show_settings_window; }
            if (ImGui::MenuItem("ImGui Demo Window", nullptr, show_imgui_demo_window)) { show_imgui_demo_window = !show_imgui_demo_window; }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Fullscreen", nullptr, is_fullscreen)) {
                is_fullscreen = !is_fullscreen;
                if (is_fullscreen) {
                    SDL_SetWindowFullscreen(get_window_handle(), SDL_WINDOW_FULLSCREEN_DESKTOP); // Fake fullscreen
                } else {
                    SDL_SetWindowFullscreen(get_window_handle(), 0); // Back to windowed
                }
            }

            if (ImGui::MenuItem("Unlock Framerate", nullptr, is_framerate_unlocked())) {
                set_framerate_unlocked(!is_framerate_unlocked());
            }

            ImGui::EndMenu();
        }

        ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);

        ImGui::EndMainMenuBar();
    }
}

void render_metrics_window() {
    block_complilations.add_point(get_metric(METRIC_BLOCK_COMPILATION));
    rsp_steps.add_point(get_metric(METRIC_RSP_STEPS));
    double frametime = 1000.0f / ImGui::GetIO().Framerate;
    frame_times.add_point(frametime);
    codecache_bytes_used.add_point(n64sys.dynarec->codecache_used);
    audiostream_bytes_available.add_point(get_metric(METRIC_AUDIOSTREAM_AVAILABLE));

    si_interrupts.add_point(get_metric(METRIC_SI_INTERRUPT));
    pi_interrupts.add_point(get_metric(METRIC_PI_INTERRUPT));
    ai_interrupts.add_point(get_metric(METRIC_AI_INTERRUPT));
    dp_interrupts.add_point(get_metric(METRIC_DP_INTERRUPT));
    sp_interrupts.add_point(get_metric(METRIC_SP_INTERRUPT));


    ImGui::Begin("Performance Metrics", &show_metrics_window);
    ImGui::Text("Average %.3f ms/frame (%.1f FPS)", frametime, ImGui::GetIO().Framerate);

    ImPlot::GetStyle().AntiAliasedLines = true;

    ImPlot::SetNextPlotLimitsY(0, frame_times.max(), ImGuiCond_Always, 0);
    ImPlot::SetNextPlotLimitsX(0, METRICS_HISTORY_ITEMS, ImGuiCond_Always);
    if (ImPlot::BeginPlot("Frame Times")) {
        ImPlot::PlotLine("Frame Time (ms)", frame_times.data, METRICS_HISTORY_ITEMS, 1, 0, frame_times.offset);
        ImPlot::EndPlot();
    }

    ImPlot::SetNextPlotLimitsY(0, rsp_steps.max(), ImGuiCond_Always, 0);
    ImPlot::SetNextPlotLimitsX(0, METRICS_HISTORY_ITEMS, ImGuiCond_Always);
    if (ImPlot::BeginPlot("RSP Steps Per Frame")) {
        ImPlot::PlotLine("RSP Steps", rsp_steps.data, METRICS_HISTORY_ITEMS, 1, 0, rsp_steps.offset);
        ImPlot::EndPlot();
    }

    ImGui::Text("Block compilations this frame: %ld", get_metric(METRIC_BLOCK_COMPILATION));
    ImPlot::SetNextPlotLimitsY(0, block_complilations.max(), ImGuiCond_Always, 0);
    ImPlot::SetNextPlotLimitsX(0, METRICS_HISTORY_ITEMS, ImGuiCond_Always);
    if (ImPlot::BeginPlot("Block Compilations Per Frame")) {
        ImPlot::PlotBars("Block compilations", block_complilations.data, METRICS_HISTORY_ITEMS, 1, 0, block_complilations.offset);
        ImPlot::EndPlot();
    }

    ImPlot::SetNextPlotLimitsY(0, n64sys.dynarec->codecache_size, ImGuiCond_Always, 0);
    ImPlot::SetNextPlotLimitsX(0, METRICS_HISTORY_ITEMS, ImGuiCond_Always);
    if (ImPlot::BeginPlot("Codecache bytes used")) {
        ImPlot::PlotBars("Codecache bytes used", codecache_bytes_used.data, METRICS_HISTORY_ITEMS, 1, 0, codecache_bytes_used.offset);
        ImPlot::EndPlot();
    }

    ImGui::Text("Audio stream bytes available: %ld", get_metric(METRIC_AUDIOSTREAM_AVAILABLE));
    ImPlot::SetNextPlotLimitsY(0, audiostream_bytes_available.max(), ImGuiCond_Always, 0);
    ImPlot::SetNextPlotLimitsX(0, METRICS_HISTORY_ITEMS, ImGuiCond_Always);
    if (ImPlot::BeginPlot("Audio Stream Bytes Available")) {
        ImPlot::PlotLine("Audio Stream Bytes Available", audiostream_bytes_available.data, METRICS_HISTORY_ITEMS, 1, 0, audiostream_bytes_available.offset);
        ImPlot::EndPlot();
    }

#define MAX(a, b) ((a) > (b) ? (a) : (b))

    int interruptsMax = MAX(MAX(MAX(MAX(si_interrupts.max(), pi_interrupts.max()), ai_interrupts.max()), dp_interrupts.max()), sp_interrupts.max());
    ImPlot::SetNextPlotLimitsY(0, interruptsMax, ImGuiCond_Always, 0);
    ImPlot::SetNextPlotLimitsX(0, METRICS_HISTORY_ITEMS, ImGuiCond_Always);
    if (ImPlot::BeginPlot("Interrupts Per Frame")) {
        ImPlot::PlotLine("SI Interrupts", si_interrupts.data, METRICS_HISTORY_ITEMS, 1, 0, si_interrupts.offset);
        ImPlot::PlotLine("PI Interrupts", pi_interrupts.data, METRICS_HISTORY_ITEMS, 1, 0, pi_interrupts.offset);
        ImPlot::PlotLine("AI Interrupts", ai_interrupts.data, METRICS_HISTORY_ITEMS, 1, 0, ai_interrupts.offset);
        ImPlot::PlotLine("DP Interrupts", dp_interrupts.data, METRICS_HISTORY_ITEMS, 1, 0, dp_interrupts.offset);
        ImPlot::PlotLine("SP Interrupts", sp_interrupts.data, METRICS_HISTORY_ITEMS, 1, 0, sp_interrupts.offset);
        ImPlot::EndPlot();
    }

    ImGui::End();
}

void render_settings_window() {
    ImGui::Begin("Settings", &show_settings_window);
    ImGui::End();
}

void render_ui() {
    if (SDL_GetMouseFocus() || n64sys.mem.rom.rom == nullptr) {
        render_menubar();
    }
    if (show_metrics_window) { render_metrics_window(); }
    if (show_imgui_demo_window) { ImGui::ShowDemoWindow(&show_imgui_demo_window); }
    if (show_settings_window) { render_settings_window(); }
}

static VkAllocationCallbacks*   g_Allocator = NULL;
static VkInstance               g_Instance = VK_NULL_HANDLE;
static VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
static VkDevice                 g_Device = VK_NULL_HANDLE;
static uint32_t                 g_QueueFamily = (uint32_t)-1;
static VkQueue                  g_Queue = VK_NULL_HANDLE;
static VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;
static VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;

static ImGui_ImplVulkanH_Window g_MainWindowData;
static uint32_t                 g_MinImageCount = 2;

static void check_vk_result(VkResult err)
{
    if (err == 0)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

void load_imgui_ui() {
    VkResult err;

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    g_Instance = get_vk_instance();
    g_PhysicalDevice = get_vk_physical_device();
    g_Device = get_vk_device();
    g_QueueFamily = get_vk_graphics_queue_family();
    g_Queue = get_graphics_queue();
    g_PipelineCache = nullptr;
    g_DescriptorPool = nullptr;
    g_Allocator = nullptr;
    g_MinImageCount = 2;


    // Create Descriptor Pool
    {
        VkDescriptorPoolSize pool_sizes[] =
                {
                        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
                        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
                        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
                        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
                        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
                        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
                        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
                        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
                        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
                        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
                        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
                };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
        check_vk_result(err);
    }

    // Create the Render Pass
    VkRenderPass renderPass;
    {
        VkAttachmentDescription attachment = {};
        attachment.format = get_vk_format();
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference color_attachment = {};
        color_attachment.attachment = 0;
        color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment;
        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;
        err = vkCreateRenderPass(g_Device, &info, g_Allocator, &renderPass);
        check_vk_result(err);
    }

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForVulkan(get_window_handle());
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = g_Instance;
    init_info.PhysicalDevice = g_PhysicalDevice;
    init_info.Device = g_Device;
    init_info.QueueFamily = g_QueueFamily;
    init_info.Queue = g_Queue;
    init_info.PipelineCache = g_PipelineCache;
    init_info.DescriptorPool = g_DescriptorPool;
    init_info.Allocator = g_Allocator;
    init_info.MinImageCount = g_MinImageCount;
    init_info.ImageCount = 2;
    init_info.CheckVkResultFn = check_vk_result;

    ImGui_ImplVulkan_Init(&init_info, renderPass);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);

    // Upload Fonts
    {
        VkCommandBuffer command_buffer = get_vk_command_buffer();
        ImGui_ImplVulkan_CreateFontsTexture(command_buffer);
        submit_requested_vk_command_buffer();
    }

    NFD_Init();
}

ImDrawData* imgui_frame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame(get_window_handle());
    ImGui::NewFrame();

    render_ui();

    ImGui::Render();
    return ImGui::GetDrawData();
}

bool imgui_wants_mouse() {
    return ImGui::GetIO().WantCaptureMouse;
}

bool imgui_wants_keyboard() {
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool imgui_handle_event(SDL_Event* event) {
    bool captured = false;
    switch (event->type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        case SDL_TEXTEDITING:
        case SDL_TEXTINPUT:
        case SDL_KEYMAPCHANGED:
            captured = imgui_wants_keyboard();
            break;
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
            captured = imgui_wants_mouse();
            break;
    }
    ImGui_ImplSDL2_ProcessEvent(event);

    return captured;
}