#include <nvds_version.h>
#include <cstring>
#include <unistd.h>
#include <termios.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <array>
#include <memory>

#include <gst/rtsp/rtsp.h>

#include "app.hpp"
#include "config_parser.hpp"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantFunctionResult"
#define APP_TITLE "DeepStream"

constexpr size_t MAX_INSTANCES{ 4 };

constexpr size_t DEFAULT_X_WINDOW_WIDTH{ 1920 };
constexpr size_t DEFAULT_X_WINDOW_HEIGHT{ 1080 };

std::array<std::unique_ptr<AppContext>, MAX_INSTANCES> g_app_contexts{};
std::unique_ptr<ConfigParser> g_config_parser;
static uint cintr{};
static GMainLoop *g_main_loop{};
static gchar **g_cfg_files{};
static gchar **g_input_uris{};
static bool g_print_version{};
static bool g_show_bbox_text{};
static bool g_print_dependencies_version{};
static bool g_quit{};
static int g_return_value{};
static uint g_num_instances;
[[maybe_unused]] static uint g_num_input_uris;
static GMutex g_fps_lock;
static gdouble g_fps[MAX_SOURCE_BINS];
static gdouble g_fps_avg[MAX_SOURCE_BINS];

static Display *g_display{};
static Window g_windows[MAX_INSTANCES] = { 0 };

static GThread *x_event_thread{};
static GMutex g_disp_lock;

static uint g_rrow, g_rcol, g_rcfg;
static bool rrowsel{}, selecting{};

GST_DEBUG_CATEGORY(NVDS_APP);

GOptionEntry entries[] = {
	{ "version", 'v', 0, G_OPTION_ARG_NONE, &g_print_version, "Print DeepStreamSDK version", nullptr },
	{ "tiledtext", 't', 0, G_OPTION_ARG_NONE, &g_show_bbox_text, "Display Bounding box labels in tiled mode", nullptr },
	{ "version-all", 0, 0, G_OPTION_ARG_NONE, &g_print_dependencies_version,
		"Print DeepStreamSDK and dependencies version", nullptr },
	{ "cfg-file", 'c', 0, G_OPTION_ARG_FILENAME_ARRAY, &g_cfg_files, "Set the config file", nullptr },
	{ "input-uri", 'i', 0, G_OPTION_ARG_FILENAME_ARRAY, &g_input_uris,
		"Set the input uri (file://stream or rtsp://stream)", nullptr },
	{ nullptr },
};

/**
 * Function will extract metadata received on nvdsanalytics
 * src pad and extract nvanalytics metadata etc.
 * */
static GstPadProbeReturn analytics_src_pad_buffer_probe(GstPad *, GstPadProbeInfo *info, gpointer)
{
	auto *buffer = reinterpret_cast<GstBuffer *>(info->data);
	NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buffer);
	if(!batch_meta)
	{
		TADS_WARN_MSG_V("Batch meta not found for buffer %p", buffer);
		return GST_PAD_PROBE_OK;
	}
//	display_analytics_metadata(batch_meta);
	return GST_PAD_PROBE_OK;
}

/**
 * Function to handle program interrupt signal.
 * It installs default handler after handling the interrupt.
 * */
static void intr_handler(int)
{
	struct sigaction action;

	TADS_ERR_MSG_V("User Interrupted..");

	memset(&action, 0, sizeof(action));
	action.sa_handler = SIG_DFL;

	sigaction(SIGINT, &action, nullptr);

	cintr = true;
}

/**
 * callback function to print the performance numbers of each stream.
 */
static void perf_cb(gpointer context, AppPerfStruct *str)
{
	static uint header_print_cnt{};
	uint i;
	auto *app_ctx = reinterpret_cast<AppContext *>(context);
	uint num_instances{ str->num_instances };

	g_mutex_lock(&g_fps_lock);
	for(i = 0; i < num_instances; i++)
	{
		g_fps[i] = str->fps.at(i);
		g_fps_avg[i] = str->fps_avg.at(i);
	}

	if(header_print_cnt % 20 == 0)
	{
		fmt::print("**PERF:  \n");
		for(i = 0; i < num_instances; i++)
		{
			fmt::print("FPS {} (Avg)\t", i);
		}
		fmt::print("\n");
		header_print_cnt = 0;
	}
	header_print_cnt++;

	if(g_num_instances > 1)
		fmt::print("PERF({}): ", app_ctx->instance_num);
	else
		fmt::print("**PERF:  ");

	for(i = 0; i < num_instances; i++)
	{
		fmt::print("{:.2f} (Avg {:.2f})\t", g_fps[i], g_fps_avg[i]);
	}
	fmt::print("\n");
	g_mutex_unlock(&g_fps_lock);
}

/**
 * Loop function to check the status of interrupts.
 * It comes out of loop if application got interrupted.
 */
static bool check_for_interrupt(gpointer)
{
	if(g_quit)
	{
		return false;
	}

	if(cintr)
	{
		cintr = false;

		g_quit = true;
		g_main_loop_quit(g_main_loop);

		return false;
	}
	return true;
}

/*
 * Function to install custom handler for program interrupt signal.
 */
static void intr_setup()
{
	struct sigaction action
	{};
	action.sa_handler = intr_handler;
	sigaction(SIGINT, &action, nullptr);
}

static bool kbhit()
{
	struct timeval tv
	{};
	fd_set rdfs;

	FD_ZERO(&rdfs);
	FD_SET(STDIN_FILENO, &rdfs);

	select(STDIN_FILENO + 1, &rdfs, nullptr, nullptr, &tv);
	return FD_ISSET(STDIN_FILENO, &rdfs);
}

/*
 * Function to enable / disable the canonical mode of terminal.
 * In non canonical mode input is available immediately (without the user
 * having to type a line-delimiter character).
 */
static void changemode(int dir)
{
	static struct termios oldt, newt;

	if(dir == 1)
	{
		tcgetattr(STDIN_FILENO, &oldt);
		newt = oldt;
		newt.c_lflag &= ~(ICANON);
		tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	}
	else
		tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

static void print_runtime_commands()
{
	g_print("\nRuntime commands:\n"
					"\th: Print this help\n"
					"\tq: Quit\n\n"
					"\tspace: Pause/Resume\n");

	if(g_app_contexts.at(0)->config.tiled_display_config.enable == TiledDisplayState::ENABLED)
	{
		g_print("NOTE: To expand a source in the 2D tiled display and view object details,"
						" left-click on the source.\n"
						"      To go back to the tiled display, right-click anywhere on the window.\n\n");
	}
}

/**
 * Loop function to check keyboard inputs and status of each pipeline.
 */
static bool event_thread_func(void *)
{
	uint i;
	bool success{ true };

	// Check if all instances have quit
	for(i = 0; i < g_num_instances; i++)
	{
		if(!g_app_contexts[i]->quit)
			break;
	}

	if(i == g_num_instances)
	{
		g_quit = true;
		g_main_loop_quit(g_main_loop);
		return false;
	}
	// Check for keyboard input
	if(!kbhit())
	{
		// continue;
		return true;
	}
	int c = fgetc(stdin);
	g_print("\n");

	int source_id;
	GstElement *tiler = g_app_contexts[g_rcfg]->pipeline.tiled_display.tiler;
	if(g_app_contexts[g_rcfg]->config.tiled_display_config.enable == TiledDisplayState::ENABLED)
	{
		g_object_get(G_OBJECT(tiler), "show-source", &source_id, nullptr);

		if(selecting)
		{
			if(!rrowsel)
			{
				if(c >= '0' && c <= '9')
				{
					g_rrow = c - '0';
					if(g_rrow < g_app_contexts[g_rcfg]->config.tiled_display_config.rows)
					{
						g_print("--selecting source  row %d--\n", g_rrow);
						rrowsel = true;
					}
					else
					{
						g_print("--selected source  row %d out of bound, reenter\n", g_rrow);
					}
				}
			}
			else
			{
				if(c >= '0' && c <= '9')
				{
					unsigned int tile_num_columns = g_app_contexts[g_rcfg]->config.tiled_display_config.columns;
					g_rcol = c - '0';
					if(g_rcol < tile_num_columns)
					{
						selecting = false;
						rrowsel = false;
						source_id = tile_num_columns * g_rrow + g_rcol;
						g_print("--selecting source  col %d sou=%d--\n", g_rcol, source_id);
						if(source_id >= (int)g_app_contexts[g_rcfg]->config.num_source_sub_bins)
						{
							source_id = -1;
						}
						else
						{
							g_app_contexts[g_rcfg]->show_bbox_text = true;
							g_app_contexts[g_rcfg]->active_source_index = source_id;
							g_object_set(G_OBJECT(tiler), "show-source", source_id, nullptr);
						}
					}
					else
					{
						g_print("--selected source  col %d out of bound, reenter\n", g_rcol);
					}
				}
			}
		}
	}
	TADS_DBG_MSG_V("pressed %d", c);
	switch(c)
	{
		case 'h':
			print_runtime_commands();
			break;
		case 'p':
			for(i = 0; i < g_num_instances; i++)
				g_app_contexts[i]->pause_pipeline();
			break;
		case 'r':
			for(i = 0; i < g_num_instances; i++)
				g_app_contexts[i]->resume_pipeline();
			break;
		case 'q':
			g_quit = true;
			g_main_loop_quit(g_main_loop);
			success = false;
			break;
		case 'c':
			if(g_app_contexts[g_rcfg]->config.tiled_display_config.enable == TiledDisplayState::ENABLED && !selecting &&
				 source_id == -1)
			{
				g_print("--selecting config file --\n");
				c = fgetc(stdin);
				if(c >= '0' && c <= '9')
				{
					g_rcfg = c - '0';
					if(g_rcfg < g_num_instances)
					{
						g_print("--selecting config  %d--\n", g_rcfg);
					}
					else
					{
						g_print("--selected config file %d out of bound, reenter\n", g_rcfg);
						g_rcfg = 0;
					}
				}
			}
			break;
		case 'z':
			if(g_app_contexts[g_rcfg]->config.tiled_display_config.enable == TiledDisplayState::ENABLED && source_id == -1 &&
				 !selecting)
			{
				g_print("--selecting source --\n");
				selecting = true;
			}
			else
			{
				if(!g_show_bbox_text)
					g_app_contexts[g_rcfg]->show_bbox_text = false;
				g_object_set(G_OBJECT(tiler), "show-source", -1, nullptr);
				g_app_contexts[g_rcfg]->active_source_index = -1;
				selecting = false;
				g_rcfg = 0;
				g_print("--tiled mode --\n");
			}
			break;
		default:
			break;
	}
	return success;
}

static int get_source_id_from_coordinates(float x_rel, float y_rel, AppContext *app_ctx)
{
	int tile_num_rows = app_ctx->config.tiled_display_config.rows;
	int tile_num_columns = app_ctx->config.tiled_display_config.columns;

	int source_id = (int)(x_rel * tile_num_columns);
	source_id += ((int)(y_rel * tile_num_rows)) * tile_num_columns;

	/* Don't allow clicks on empty tiles. */
	if(source_id >= (int)app_ctx->config.num_source_sub_bins)
		source_id = -1;

	return source_id;
}

/**
 * Thread to monitor X window events.
 */
static void *nvds_x_event_thread(void *)
{
	g_mutex_lock(&g_disp_lock);
	static bool is_paused{};
	while(g_display)
	{
		XEvent e;
		uint index;
		while(XPending(g_display))
		{
			XNextEvent(g_display, &e);
			switch(e.type)
			{
				case ButtonPress:
				{
					XWindowAttributes win_attr;
					XButtonEvent ev = e.xbutton;
					int source_id;
					GstElement *tiler;

					XGetWindowAttributes(g_display, ev.window, &win_attr);

					for(index = 0; index < MAX_INSTANCES; index++)
						if(ev.window == g_windows[index])
							break;

					tiler = g_app_contexts[index]->pipeline.tiled_display.tiler;
					g_object_get(G_OBJECT(tiler), "show-source", &source_id, nullptr);

					if(ev.button == Button1 && source_id == -1)
					{
						source_id = get_source_id_from_coordinates(ev.x * 1.0 / win_attr.width, ev.y * 1.0 / win_attr.height,
																											 g_app_contexts[index].get());
						if(source_id > -1)
						{
							g_object_set(G_OBJECT(tiler), "show-source", source_id, nullptr);
							g_app_contexts[index]->active_source_index = source_id;
							g_app_contexts[index]->show_bbox_text = true;
						}
					}
					else if(ev.button == Button3)
					{
						g_object_set(G_OBJECT(tiler), "show-source", -1, nullptr);
						g_app_contexts[index]->active_source_index = -1;
						if(!g_show_bbox_text)
							g_app_contexts[index]->show_bbox_text = false;
					}
				}
				break;
				case KeyRelease:
				case KeyPress:
				{
					KeySym p, r, q;
					uint i;
					p = XKeysymToKeycode(g_display, XK_space);
					r = XKeysymToKeycode(g_display, XK_space);
					q = XKeysymToKeycode(g_display, XK_Q);
					if(e.xkey.keycode == p && !is_paused)
					{
						for(i = 0; i < g_num_instances; i++)
							g_app_contexts[i]->pause_pipeline();
						is_paused = true;
						break;
					}
					if(e.xkey.keycode == r && is_paused)
					{
						for(i = 0; i < g_num_instances; i++)
							g_app_contexts[i]->resume_pipeline();
						is_paused = false;
						break;
					}
					if(e.xkey.keycode == q)
					{
						g_quit = true;
						g_main_loop_quit(g_main_loop);
					}
				}
				break;
				case ClientMessage:
				{
					Atom wm_delete;
					for(index = 0; index < MAX_INSTANCES; index++)
						if(e.xclient.window == g_windows[index])
							break;

					wm_delete = XInternAtom(g_display, "WM_DELETE_WINDOW", 1);
					if(wm_delete != None && wm_delete == (Atom)e.xclient.data.l[0])
					{
						g_quit = true;
						g_main_loop_quit(g_main_loop);
					}
				}
				break;
			}
		}
		g_mutex_unlock(&g_disp_lock);
		g_usleep(G_USEC_PER_SEC / 20);
		g_mutex_lock(&g_disp_lock);
	}
	g_mutex_unlock(&g_disp_lock);
	return nullptr;
}

static bool recreate_pipeline_thread_func(AppContext *app_ctx)
{
	uint i;
	bool success{ true };

	TADS_DBG_MSG_V("Destroy pipeline");
	app_ctx->destroy_pipeline();

	TADS_DBG_MSG_V("Recreate pipeline");
	if(!app_ctx->create_pipeline(perf_cb))
	{
		TADS_ERR_MSG_V("Failed to create pipeline");
		g_return_value = -1;
		return false;
	}

	if(gst_element_set_state(app_ctx->pipeline.pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE)
	{
		TADS_ERR_MSG_V("Failed to set pipeline to PAUSED");
		g_return_value = -1;
		return false;
	}

	for(i = 0; i < app_ctx->config.num_sink_sub_bins; i++)
	{
		auto *instance_bin{ &app_ctx->pipeline.instance_bins.at(0) };
		SinkSubBin *sub_bin{ &instance_bin->sink.sub_bins.at(i) };

		if(!GST_IS_VIDEO_OVERLAY(sub_bin->sink))
		{
			continue;
		}

		gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(sub_bin->sink), (gulong)g_windows[app_ctx->instance_num]);
		gst_video_overlay_expose(GST_VIDEO_OVERLAY(sub_bin->sink));
	}

	if(gst_element_set_state(app_ctx->pipeline.pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
	{
		TADS_WARN_MSG_V("Can't set pipeline to playing state");
		g_return_value = -1;
		return false;
	}

	return success;
}

int main(int argc, char *argv[])
{
	GOptionContext *ctx;
	GOptionGroup *group;
	uint i;
	GError *error{};

	ctx = g_option_context_new("Nvidia DeepStream Demo");
	group = g_option_group_new("abc", nullptr, nullptr, nullptr, nullptr);
	g_option_group_add_entries(group, entries);

	g_option_context_set_main_group(ctx, group);
	g_option_context_add_group(ctx, gst_init_get_option_group());

	GST_DEBUG_CATEGORY_INIT(NVDS_APP, "NVDS_APP", 0, nullptr);

	if(!g_option_context_parse(ctx, &argc, &argv, &error))
	{
		TADS_ERR_MSG_V("%s", error->message);
		return -1;
	}

	if(g_print_version)
	{
		g_print("App version %d.%d.%d\n", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_MICRO);
		nvds_version_print();
		return 0;
	}

	if(g_print_dependencies_version)
	{
		g_print("App version %d.%d.%d\n", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_MICRO);
		nvds_version_print();
		nvds_dependencies_version_print();
		return 0;
	}

	if(g_cfg_files)
	{
		g_num_instances = g_strv_length(g_cfg_files);
	}
	if(g_input_uris)
	{
		g_num_input_uris = g_strv_length(g_input_uris);
	}

	if(!g_cfg_files || g_num_instances == 0)
	{
		TADS_ERR_MSG_V("Specify config file with -c option");
		g_return_value = -1;
		goto done;
	}

	for(i = 0; i < g_num_instances; i++)
	{
		auto &app{ g_app_contexts.at(i) = std::make_unique<AppContext>() };
		app->instance_num = i;
		if(g_show_bbox_text)
		{
			app->show_bbox_text = true;
		}

		if(g_input_uris && g_input_uris[i])
		{
			app->config.multi_source_configs[0].uri = g_strdup_printf("%s", g_input_uris[i]);
			g_free(g_input_uris[i]);
		}

		g_config_parser = std::make_unique<ConfigParser>(g_cfg_files[i]);

		if(!g_config_parser->parse(&app->config))
		{
			app->status = -1;
			goto done;
		}
	}

	for(i = 0; i < g_num_instances; i++)
	{
		auto &app{ g_app_contexts.at(i) };
		if(!app->create_pipeline(perf_cb))
		{
			TADS_ERR_MSG_V("Failed to create pipeline");
			g_return_value = -1;
			goto done;
		}

		// TODO: Remove as a duplicate of AppContext::all_bbox_generated and less effective to classify plate number
		if(app->config.analytics_config.enable)
		{
			GstPad *src_pad;
			GstElement *analytics_elem = app->pipeline.common_elements.analytics.analytics_elem;
			src_pad = gst_element_get_static_pad(analytics_elem, "src");
			if(!src_pad)
			{
				TADS_WARN_MSG_V("Unable to get analytics src pad");
			}
			else
			{
				gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, analytics_src_pad_buffer_probe, nullptr, nullptr);
				gst_object_unref(src_pad);
			}
		}
	}

	g_main_loop = g_main_loop_new(nullptr, false);

	intr_setup();
	g_timeout_add(400, reinterpret_cast<GSourceFunc>(check_for_interrupt), nullptr);

	g_mutex_init(&g_disp_lock);
	g_display = XOpenDisplay(nullptr);

	for(i = 0; i < g_num_instances; i++)
	{
		auto &app_ctx{ g_app_contexts.at(i) };

		if(gst_element_set_state(app_ctx->pipeline.pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE)
		{
			TADS_ERR_MSG_V("Failed to set pipeline to PAUSED");
			g_return_value = -1;
			goto done;
		}

		for(uint j = 0; j < app_ctx->config.num_sink_sub_bins; j++)
		{
			XTextProperty xproperty;
			gchar *title;
			uint width, height;
			XSizeHints hints{};
			InstanceBin *instance_bin{ &app_ctx->pipeline.instance_bins.at(0) };
			SinkSubBinConfig *sink_sub_bin_config{ &app_ctx->config.sink_bin_sub_bin_configs.at(j) };
			SinkSubBin *sub_bin{ &instance_bin->sink.sub_bins.at(j) };

			if(!GST_IS_VIDEO_OVERLAY(sub_bin->sink))
			{
				continue;
			}

			if(!g_display)
			{
				TADS_ERR_MSG_V("Could not open X Display");
				g_return_value = -1;
				goto done;
			}

			if(sink_sub_bin_config->render_config.width)
				width = sink_sub_bin_config->render_config.width;
			else
				width = app_ctx->config.tiled_display_config.width;

			if(sink_sub_bin_config->render_config.height)
				height = sink_sub_bin_config->render_config.height;
			else
				height = app_ctx->config.tiled_display_config.height;

			width = (width) ? width : DEFAULT_X_WINDOW_WIDTH;
			height = (height) ? height : DEFAULT_X_WINDOW_HEIGHT;

			hints.flags = PPosition | PSize;
			hints.x = sink_sub_bin_config->render_config.offset_x;
			hints.y = sink_sub_bin_config->render_config.offset_y;
			hints.width = width;
			hints.height = height;

			g_windows[i] = XCreateSimpleWindow(g_display, RootWindow(g_display, DefaultScreen(g_display)), hints.x, hints.y,
																				 width, height, 2, 0x00000000, 0x00000000);

			XSetNormalHints(g_display, g_windows[i], &hints);

			if(g_num_instances > 1)
				title = g_strdup_printf(APP_TITLE "-%d", i);
			else
				title = g_strdup(APP_TITLE);
			if(XStringListToTextProperty((char **)&title, 1, &xproperty) != 0)
			{
				XSetWMName(g_display, g_windows[i], &xproperty);
				XFree(xproperty.value);
			}

			XSetWindowAttributes attr = { 0 };
			if((app_ctx->config.tiled_display_config.enable == TiledDisplayState::ENABLED &&
					app_ctx->config.tiled_display_config.rows * app_ctx->config.tiled_display_config.columns == 1) ||
				 (app_ctx->config.tiled_display_config.enable == TiledDisplayState::DISABLED))
			{
				attr.event_mask = KeyPress;
			}
			else if(app_ctx->config.tiled_display_config.enable == TiledDisplayState::ENABLED)
			{
				attr.event_mask = ButtonPress | KeyRelease;
			}
			XChangeWindowAttributes(g_display, g_windows[i], CWEventMask, &attr);

			Atom wmDeleteMessage = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
			if(wmDeleteMessage != None)
			{
				XSetWMProtocols(g_display, g_windows[i], &wmDeleteMessage, 1);
			}
			XMapRaised(g_display, g_windows[i]);
			XSync(g_display, 1); // discard the events for now
			gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(sub_bin->sink), (gulong)g_windows[i]);
			gst_video_overlay_expose(GST_VIDEO_OVERLAY(sub_bin->sink));
			if(!x_event_thread)
				x_event_thread = g_thread_new("nvds-window-event-thread", nvds_x_event_thread, nullptr);
		}
	}

	/* Dont try to set playing state if error is observed */
	if(g_return_value != -1)
	{
		for(i = 0; i < g_num_instances; i++)
		{
			auto &app_ctx{ g_app_contexts.at(i) };
			if(gst_element_set_state(app_ctx->pipeline.pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
			{
				TADS_WARN_MSG_V("Can't set pipeline to playing state");
				g_return_value = -1;
				goto done;
			}
			if(app_ctx->config.pipeline_recreate_sec)
				g_timeout_add_seconds(app_ctx->config.pipeline_recreate_sec,
															reinterpret_cast<GSourceFunc>(recreate_pipeline_thread_func), app_ctx.get());
		}
	}

	print_runtime_commands();

	changemode(1);

	g_timeout_add(40, reinterpret_cast<GSourceFunc>(event_thread_func), nullptr);
	g_main_loop_run(g_main_loop);

	changemode(0);

done:

	TADS_INFO_MSG_V("Quitting");
	for(i = 0; i < g_num_instances; i++)
	{
		if(g_app_contexts[i]->status == -1)
			g_return_value = -1;

		g_app_contexts[i]->destroy_pipeline();

		g_mutex_lock(&g_disp_lock);
		if(g_windows[i])
			XDestroyWindow(g_display, g_windows[i]);
		g_windows[i] = 0;
		g_mutex_unlock(&g_disp_lock);
	}

	g_mutex_lock(&g_disp_lock);
	if(g_display)
		XCloseDisplay(g_display);
	g_display = nullptr;
	g_mutex_unlock(&g_disp_lock);
	g_mutex_clear(&g_disp_lock);

	if(g_main_loop)
	{
		g_main_loop_unref(g_main_loop);
	}

	if(ctx)
	{
		g_option_context_free(ctx);
	}

	if(g_return_value == 0)
	{
		TADS_INFO_MSG_V("App run successful");
	}
	else
	{
		TADS_ERR_MSG_V("App run failed");
	}

	gst_deinit();

	return g_return_value;
}

#pragma clang diagnostic pop