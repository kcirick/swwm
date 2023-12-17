#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/edges.h>
#include <xkbcommon/xkbcommon.h>
#if XWAYLAND
#include <wlr/xwayland.h>
#endif

//
//#include <wlr/types/wlr_export_dmabuf_v1.h>
//#include <wlr/types/wlr_screencopy_v1.h>
//#include <wlr/types/wlr_data_control_v1.h>
//#include <wlr/types/wlr_primary_selection.h>
//#include <wlr/types/wlr_primary_selection_v1.h>
//#include <wlr/types/wlr_viewporter.h>
//#include <wlr/types/wlr_single_pixel_buffer_v1.h>
//#include <wlr/types/wlr_fractional_scale_v1.h>
//

#include "dwl-ipc-unstable-v2-protocol.h"
#include "globals.h"
#include "layer.h"
#include "client.h"
#include "server.h"
#include "action.h"
#include "ipc.h"


//------------------------------------------------------------------------
void
setCurrentTag(struct simple_server* server, int tag)
{
   say(DEBUG, "setCurrentTag %d", tag);
   struct simple_output* output = server->cur_output;
   output->cur_tag = TAGMASK(tag);

   arrange_output(output);
}

struct simple_output*
get_output_at(struct simple_server* server, double x, double y)
{
   struct wlr_output *output = wlr_output_layout_output_at(server->output_layout, x, y);
   return output ? output->data : NULL;
}

void
print_server_info(struct simple_server* server) 
{
   struct simple_output* output;
   struct simple_client* client;

   wl_list_for_each(output, &server->outputs, link) {
      say(INFO, "output %s", output->wlr_output->name);
      say(INFO, " -> cur_output = %u", output == server->cur_output);
      say(INFO, " -> tag = %u", output->cur_tag);
      wl_list_for_each(client, &server->clients, link) {
         say(INFO, " -> client");
         say(INFO, "    -> client tags = %u", client->tags);
      }
   }

   wl_list_for_each(output, &server->outputs, link)
      dwl_ipc_output_printstatus(output);
}

static void
new_decoration_notify(struct wl_listener *listener, void *data)
{
   struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
   wlr_xdg_toplevel_decoration_v1_set_mode(decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void
arrange_output(struct simple_output* output)
{
   say(DEBUG, "arrange_output");
   struct simple_server* server = output->server;
   struct simple_client* client;

   wl_list_for_each(client, &server->clients, link) {
      if(client->output == output){
         say(DEBUG, ">>> %u", VISIBLEON(client, output));
         wlr_scene_node_set_enabled(&client->scene_tree->node, VISIBLEON(client, output));
      }
   }
   print_server_info(server);
}

//--- Output notify functions --------------------------------------------
static void 
output_layout_change_notify(struct wl_listener *listener, void *data) 
{
   say(DEBUG, "output_layout_change_notify");
   struct simple_server *server = wl_container_of(listener, server, output_layout_change);

   struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
   struct wlr_output_configuration_head_v1 *config_head;
   if(!config)
      say(ERROR, "wlr_output_configuration_v1_create failed");

   struct simple_output *output;
   wl_list_for_each(output, &server->outputs, link) {
      if(!output->wlr_output->enabled) continue;

      config_head = wlr_output_configuration_head_v1_create(config, output->wlr_output);
      if(!config_head) {
         wlr_output_configuration_v1_destroy(config);
         say(ERROR, "wlr_output_configuration_head_v1_create failed");
      }
      struct wlr_box box;
      wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
      if(wlr_box_empty(&box))
         say(ERROR, "Failed to get output layout box");

      memset(&output->usable_area, 0, sizeof(output->usable_area));
      output->usable_area = box;
      say(INFO, " box x=%d / y=%d / w=%d / h=%d", box.x, box.y, box.width, box.height);

      //arrange_layers(output);

      config_head->state.x = box.x;
      config_head->state.y = box.y;
   }

   if(config)
      wlr_output_manager_v1_set_configuration(server->output_manager, config);
   else
      say(ERROR, "wlr_output_manager_v1_set_configuration failed");
}

static void 
output_manager_apply_notify(struct wl_listener *listener, void *data) 
{
   say(DEBUG, "output_manager_apply_notify");
   //
}

static void 
output_manager_test_notify(struct wl_listener *listener, void *data) 
{
   say(DEBUG, "output_manager_test_notify");
   //
}

static void 
output_frame_notify(struct wl_listener *listener, void *data) 
{
   //say(DEBUG, "output_frame_notify");
   struct simple_output *output = wl_container_of(listener, output, frame);
   struct wlr_scene *scene = output->server->scene;
   struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(scene, output->wlr_output);
   
   // Render the scene if needed and commit the output 
   wlr_scene_output_commit(scene_output, NULL);
   
   struct timespec now;
   clock_gettime(CLOCK_MONOTONIC, &now);
   wlr_scene_output_send_frame_done(scene_output, &now);
}

static void 
output_request_state_notify(struct wl_listener *listener, void *data) 
{
   say(DEBUG, "output_request_state_notify");
   // called when the backend requests a new state for the output
   struct simple_output *output = wl_container_of(listener, output, request_state);
   const struct wlr_output_event_request_state *event = data;
   wlr_output_commit_state(output->wlr_output, event->state);
}

static void 
output_destroy_notify(struct wl_listener *listener, void *data) 
{
   say(DEBUG, "output_destroy_notify");
   struct simple_output *output = wl_container_of(listener, output, destroy);

   struct DwlIpcOutput *ipc_output, *ipc_output_tmp;
   wl_list_for_each_safe(ipc_output, ipc_output_tmp, &output->dwl_ipc_outputs, link)
      wl_resource_destroy(ipc_output->resource);

   wl_list_remove(&output->frame.link);
   wl_list_remove(&output->request_state.link);
   wl_list_remove(&output->destroy.link);
   wl_list_remove(&output->link);
   free(output);
}

//------------------------------------------------------------------------
static void 
new_output_notify(struct wl_listener *listener, void *data) 
{
   say(DEBUG, "new_output_notify");
   
   struct simple_server *server = wl_container_of(listener, server, new_output);
   struct wlr_output *wlr_output = data;

   // Don't configure any non-desktop displays, such as VR headsets
   if(wlr_output->non_desktop) {
      say(DEBUG, "Not configuring non-desktop output");
      return;
   }

   // Configures the output created by the backend to use the allocator and renderer
   // Must be done once, before committing the output
   if(!wlr_output_init_render(wlr_output, server->allocator, server->renderer))
      say(ERROR, "unable to initialize output renderer");
   
   // The output may be disabled. Switch it on
   struct wlr_output_state state;
   wlr_output_state_init(&state);
   wlr_output_state_set_enabled(&state, true);

   struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
   if (mode)
      wlr_output_state_set_mode(&state, mode);

   wlr_output_commit_state(wlr_output, &state);
   wlr_output_state_finish(&state);

   struct simple_output *output = calloc(1, sizeof(struct simple_output));
   output->wlr_output = wlr_output;
   wlr_output->data = output;
   output->server = server;

   wl_list_init(&output->dwl_ipc_outputs);   // ipc addition

   LISTEN(&wlr_output->events.frame, &output->frame, output_frame_notify);
   LISTEN(&wlr_output->events.destroy, &output->destroy, output_destroy_notify);
   LISTEN(&wlr_output->events.request_state, &output->request_state, output_request_state_notify);

   wl_list_insert(&server->outputs, &output->link);
   

   for(int i=0; i<N_LAYER_SHELL_LAYERS; i++)
      wl_list_init(&output->layer_shells[i]);
   
   wlr_scene_node_lower_to_bottom(&server->layer_tree[LyrBottom]->node);
   wlr_scene_node_lower_to_bottom(&server->layer_tree[LyrBg]->node);
   wlr_scene_node_raise_to_top(&server->layer_tree[LyrTop]->node);
   wlr_scene_node_raise_to_top(&server->layer_tree[LyrOverlay]->node);

   //set default tag
   output->cur_tag = TAGMASK(0);

   struct wlr_output_layout_output *l_output =
      wlr_output_layout_add_auto(server->output_layout, wlr_output);
   struct wlr_scene_output *scene_output =
      wlr_scene_output_create(server->scene, wlr_output);
   wlr_scene_output_layout_add_output(server->scene_output_layout, l_output, scene_output);

   say(INFO, " -> Output %s : %dx%d+%d+%d", l_output->output->name,
         l_output->output->width, l_output->output->height, 
         l_output->x, l_output->y);
}

//--- Input functions ----------------------------------------------------
void 
input_focus_surface(struct simple_server *server, struct wlr_surface *surface) 
{
   if(!surface) {
      wlr_seat_keyboard_notify_clear_focus(server->seat);
      return;
   }
   struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
      
   wlr_seat_keyboard_notify_enter(server->seat, surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
}

//--- Keyboard events ----------------------------------------------------
static void 
kb_modifiers_notify(struct wl_listener *listener, void *data) 
{
   struct simple_input *keyboard = wl_container_of(listener, keyboard, kb_modifiers);

   wlr_seat_set_keyboard(keyboard->server->seat, keyboard->keyboard);
   wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->keyboard->modifiers);
}

static void 
kb_key_notify(struct wl_listener *listener, void *data) 
{
   struct simple_input *keyboard = wl_container_of(listener, keyboard, kb_key);
   struct simple_server *server = keyboard->server;
   struct wlr_keyboard_key_event *event = data;

   uint32_t keycode = event->keycode + 8;
   const xkb_keysym_t *syms;
   int nsyms = xkb_state_key_get_syms(keyboard->keyboard->xkb_state, keycode, &syms);
   
   bool handled = false;
   uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->keyboard);

   if(event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
      for(int i=0; i<nsyms; i++){
         struct keymap *keymap;
         wl_list_for_each(keymap, &server->config->key_bindings, link) {
            if (modifiers ^ keymap->mask) continue;

            if (syms[i] == keymap->keysym){
               key_function(server, keymap);
               handled=true;
            }
         }
      }
   }

   if(!handled) {
      wlr_seat_set_keyboard(server->seat, keyboard->keyboard);
      wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
   }
}

//--- Pointer events -----------------------------------------------------
static uint32_t 
get_resize_edges(struct simple_client *client, double x, double y) 
{
   uint32_t edges = 0;

   struct wlr_box box = client->geom;
   edges |= x < (box.x + box.width/2)  ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT;
   edges |= y < (box.y + box.height/2) ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM;
   return edges;
}

static void 
process_cursor_move(struct simple_server *server, uint32_t time) 
{
   struct simple_client *client = server->grabbed_client;
   client->geom.x = server->cursor->x - server->grab_x;
   client->geom.y = server->cursor->y - server->grab_y;
   wlr_scene_node_set_position(&client->scene_tree->node, client->geom.x, client->geom.y);
}

static void 
process_cursor_resize(struct simple_server *server, uint32_t time) 
{
   struct simple_client *client = server->grabbed_client;
   
   double delta_x = server->cursor->x - server->grab_x;
   double delta_y = server->cursor->y - server->grab_y;
   int new_left = server->grab_box.x;
   int new_right = server->grab_box.x + server->grab_box.width;
   int new_top = server->grab_box.y;
   int new_bottom = server->grab_box.y + server->grab_box.height;
   
   if (server->resize_edges & WLR_EDGE_TOP) {
      new_top += delta_y;
      if(new_top >= new_bottom)
         new_top = new_bottom - 1;
   } else if (server->resize_edges & WLR_EDGE_BOTTOM) {
      new_bottom += delta_y;
      if(new_bottom <= new_top)
         new_bottom = new_top + 1;
   }
   
   if (server->resize_edges & WLR_EDGE_LEFT) {
      new_left += delta_x;
      if(new_left >= new_right)
         new_left = new_right - 1;
   } else if (server->resize_edges & WLR_EDGE_RIGHT) {
      new_right += delta_x;
      if(new_right <= new_left)
         new_right = new_left + 1;
   }

   client->geom.x = new_left;
   client->geom.y = new_top;
   client->geom.width = new_right - new_left;
   client->geom.height = new_bottom - new_top;

   //wlr_scene_node_set_position(&client->scene_tree->node, client->geom.x, client->geom.y);
   set_client_size_position(client, client->geom);
}

static void 
process_cursor_motion(struct simple_server *server, uint32_t time) 
{
   //say(DEBUG, "process_cursor_motion");
   if(server->cursor_mode == CURSOR_MOVE) {
      process_cursor_move(server, time);
      return;
   } else if(server->cursor_mode == CURSOR_RESIZE) {
      process_cursor_resize(server, time);
      return;
   } 

   // Otherwise, find the client under the pointer and send the event along
   double sx, sy;
   struct wlr_seat *wlr_seat = server->seat;
   struct wlr_surface *surface = NULL;
   struct simple_client *client = get_client_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
   
   if(!client)
      wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "left_ptr");

   if(client && surface && client->server->config->sloppy_focus){
      focus_client(client, surface, false);
   }

   if(surface) {
      wlr_seat_pointer_notify_enter(wlr_seat, surface, sx, sy);
      wlr_seat_pointer_notify_motion(wlr_seat, time, sx, sy);
   } else
      wlr_seat_pointer_clear_focus(wlr_seat);
}

//------------------------------------------------------------------------
static void 
cursor_motion_notify(struct wl_listener *listener, void *data) 
{
   //say(DEBUG, "cursor_motion_notify");
   struct simple_server *server = wl_container_of(listener, server, cursor_motion);
   struct wlr_pointer_motion_event *event = data;

   wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
   process_cursor_motion(server, event->time_msec);
}

static void 
cursor_motion_abs_notify(struct wl_listener *listener, void *data) 
{
  // say(DEBUG, "cursor_motion_abs_notify");
   struct simple_server *server = wl_container_of(listener, server, cursor_motion_abs);
   struct wlr_pointer_motion_absolute_event *event = data;

   wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
   process_cursor_motion(server, event->time_msec);
}

static void 
cursor_button_notify(struct wl_listener *listener, void *data) 
{
   say(DEBUG, "cursor_button_notify");
   struct simple_server *server = wl_container_of(listener, server, cursor_button);
   struct wlr_pointer_button_event *event = data;
   
   // Notify the client with pointer focus that a button press has occurred
   wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);

   double sx, sy;
   struct wlr_surface *surface = NULL;
   //uint32_t resize_edges;
   struct simple_client *client = get_client_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

   // button release
   if(event->state == WLR_BUTTON_RELEASED) {
      server->cursor_mode = CURSOR_PASSTHROUGH;
      server->grabbed_client = NULL;
      return;
   }
   
   // press on desktop
   if(!client) {
      say(INFO, "press on desktop");
      return;
   }
   
   //press on client 
   struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
   uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard);
   if((modifiers & WLR_MODIFIER_ALT) && (event->button == BTN_LEFT) ) {
      begin_interactive(client, CURSOR_MOVE, 0);
      return;
   } else if ((modifiers & WLR_MODIFIER_ALT) && (event->button == BTN_RIGHT) ) {
      uint32_t resize_edges = get_resize_edges(client, server->cursor->x, server->cursor->y);
      begin_interactive(client, CURSOR_RESIZE, resize_edges);
      return;
   }
   
   focus_client(client, surface, true);
}

static void 
cursor_axis_notify(struct wl_listener *listener, void *data) 
{
   //say(DEBUG, "cursor_axis_notify");
   struct simple_server *server = wl_container_of(listener, server, cursor_axis);
   struct wlr_pointer_axis_event *event = data;

   wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta, event->delta_discrete, event->source);
}

static void 
cursor_frame_notify(struct wl_listener *listener, void *data) 
{
   //say(DEBUG, "cursor_frame_notify");
   struct simple_server *server = wl_container_of(listener, server, cursor_frame);

   wlr_seat_pointer_notify_frame(server->seat);
}

static void 
request_cursor_notify(struct wl_listener *listener, void *data) 
{
   //say(DEBUG, "seat_request_cursor_notify");
   struct simple_server *server = wl_container_of(listener, server, request_cursor);
   struct wlr_seat_pointer_request_set_cursor_event *event = data;
   struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;
   
   if(focused_client == event->seat_client) {
      wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
   }
}

static void 
request_set_selection_notify(struct wl_listener *listener, void *data) 
{
   //say(DEBUG, "seat_request_set_selection_notify");
   struct simple_server *server = wl_container_of(listener, server, request_set_selection);
   struct wlr_seat_request_set_selection_event *event = data;
   wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void 
input_destroy_notify(struct wl_listener *listener, void *data) 
{
   say(DEBUG, "input_destroy_notify");
   struct simple_input *input = wl_container_of(listener, input, destroy);
   if (input->type==INPUT_KEYBOARD) {
      wl_list_remove(&input->kb_modifiers.link);
      wl_list_remove(&input->kb_key.link);
   }
   wl_list_remove(&input->destroy.link);
   wl_list_remove(&input->link);
   free(input);
}

//--- Input notify function ----------------------------------------------
static void 
new_input_notify(struct wl_listener *listener, void *data) 
{
   //say(DEBUG, "new_input_notify");
   struct simple_server *server = wl_container_of(listener, server, new_input);
   struct wlr_input_device *device = data;

   struct simple_input *input = calloc(1, sizeof(struct simple_input));
   input->device = device;
   input->server = server;

   if(device->type == WLR_INPUT_DEVICE_POINTER) {
      say(DEBUG, "New Input: POINTER");
      input->type = INPUT_POINTER;
      wlr_cursor_attach_input_device(server->cursor, input->device);

   } else if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
      say(DEBUG, "New Input: KEYBOARD");
      input->type = INPUT_KEYBOARD;
      struct wlr_keyboard *kb = wlr_keyboard_from_input_device(device);
      input->keyboard = kb;

      struct xkb_rule_names rules = { 0 };
      struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
      struct xkb_keymap *keymap = xkb_map_new_from_names(context, &rules, 
            XKB_KEYMAP_COMPILE_NO_FLAGS);

      wlr_keyboard_set_keymap(kb, keymap);
      xkb_keymap_unref(keymap);
      xkb_context_unref(context);
      wlr_keyboard_set_repeat_info(kb, 25, 600);

      LISTEN(&kb->events.modifiers, &input->kb_modifiers, kb_modifiers_notify);
      LISTEN(&kb->events.key, &input->kb_key, kb_key_notify);
      
      wlr_seat_set_keyboard(server->seat, kb);
   } else {
      say(DEBUG, "New Input: SOMETHING ELSE");
      input->type = INPUT_MISC;
   }

   LISTEN(&device->events.destroy, &input->destroy, input_destroy_notify);
   wl_list_insert(&server->inputs, &input->link);

   uint32_t caps = 0;
   wl_list_for_each(input, &server->inputs, link) {
      switch (input->device->type){
         case WLR_INPUT_DEVICE_KEYBOARD:
            caps |= WL_SEAT_CAPABILITY_KEYBOARD;
            break;
         case WLR_INPUT_DEVICE_POINTER:
            caps |= WL_SEAT_CAPABILITY_POINTER;
            break;
         default:
            break;
      }
   }
   wlr_seat_set_capabilities(server->seat, caps);
}

//------------------------------------------------------------------------
void 
prepareServer(struct simple_server *server, struct wlr_session *session, int info_level) 
{
   say(INFO, "Preparing Wayland server initialization");
   
   wlr_log_init(info_level, NULL);

   if(!(server->display = wl_display_create()))
      say(ERROR, "Unable to create Wayland display!");

   if(!(server->backend = wlr_backend_autocreate(server->display, &session)))
      say(ERROR, "Unable to create wlr_backend!");

   // create a scene graph used to lay out windows
   server->scene = wlr_scene_create();
   for(int i=0; i<NLayers; i++)
      server->layer_tree[i] = wlr_scene_tree_create(&server->scene->tree);

   // create renderer
   if(!(server->renderer = wlr_renderer_autocreate(server->backend)))
      say(ERROR, "Unable to create wlr_renderer");

   wlr_renderer_init_wl_display(server->renderer, server->display);
 
   // create an allocator
   if(!(server->allocator = wlr_allocator_autocreate(server->backend, server->renderer)))
      say(ERROR, "Unable to create wlr_allocator");

   // create compositor
   server->compositor = wlr_compositor_create(server->display, COMPOSITOR_VERSION, server->renderer);
   wlr_subcompositor_create(server->display);
   wlr_data_device_manager_create(server->display);
   /*
   wlr_export_dmabuf_manager_v1_create(server->display);
   wlr_screencopy_manager_v1_create(server->display);
   wlr_data_control_manager_v1_create(server->display);
   wlr_primary_selection_v1_device_manager_create(server->display);
   wlr_viewporter_create(server->display);
   wlr_single_pixel_buffer_manager_v1_create(server->display);
   wlr_fractional_scale_manager_v1_create(server->display, 1);
   */
   
   // create an output layout, i.e. wlroots utility for working with an arrangement of 
   // screens in a physical layout
   server->output_layout = wlr_output_layout_create();
   LISTEN(&server->output_layout->events.change, &server->output_layout_change, output_layout_change_notify);

   wl_list_init(&server->outputs);   
   LISTEN(&server->backend->events.new_output, &server->new_output, new_output_notify);

   server->scene_output_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);

   wlr_xdg_output_manager_v1_create(server->display, server->output_layout);
   server->output_manager = wlr_output_manager_v1_create(server->display);
   LISTEN(&server->output_manager->events.apply, &server->output_manager_apply, output_manager_apply_notify);
   LISTEN(&server->output_manager->events.test, &server->output_manager_test, output_manager_test_notify);
   
   // set up seat and inputs
   server->seat = wlr_seat_create(server->display, "seat0");
   if(!server->seat)
      say(ERROR, "cannot allocate seat");

   wl_list_init(&server->inputs);
   LISTEN(&server->backend->events.new_input, &server->new_input, new_input_notify);

   LISTEN(&server->seat->events.request_set_cursor, &server->request_cursor, request_cursor_notify);
   LISTEN(&server->seat->events.request_set_selection, &server->request_set_selection, request_set_selection_notify);

   server->cursor = wlr_cursor_create();
   wlr_cursor_attach_output_layout(server->cursor, server->output_layout); 

   // create a cursor manager
   server->cursor_manager = wlr_xcursor_manager_create(NULL, 24);
   wlr_xcursor_manager_load(server->cursor_manager, 1);

   server->cursor_mode = CURSOR_PASSTHROUGH;
   LISTEN(&server->cursor->events.motion, &server->cursor_motion, cursor_motion_notify);
   LISTEN(&server->cursor->events.motion_absolute, &server->cursor_motion_abs, cursor_motion_abs_notify);
   LISTEN(&server->cursor->events.button, &server->cursor_button, cursor_button_notify);
   LISTEN(&server->cursor->events.axis, &server->cursor_axis, cursor_axis_notify);
   LISTEN(&server->cursor->events.frame, &server->cursor_frame, cursor_frame_notify);


   // set up Wayland shells, i.e. XDG, Layer, and XWayland
   wl_list_init(&server->clients);
   wl_list_init(&server->focus_order);
   
   if(!(server->xdg_shell = wlr_xdg_shell_create(server->display, XDG_SHELL_VERSION)))
      say(ERROR, "unable to create XDG shell interface");
   LISTEN(&server->xdg_shell->events.new_surface, &server->xdg_new_surface, xdg_new_surface_notify);

   server->layer_shell = wlr_layer_shell_v1_create(server->display, LAYER_SHELL_VERSION);
   LISTEN(&server->layer_shell->events.new_surface, &server->layer_new_surface, layer_new_surface_notify);
   
   //unmanaged surfaces
   // ...

   // idle_notifier = ...
   // idle_inhibit_manager = ...
   // LISTEN ...

   // Use decoration protocols to negotiate server-side decorations
   wlr_server_decoration_manager_set_default_mode(wlr_server_decoration_manager_create(server->display),
         WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
   server->xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(server->display);
   LISTEN(&server->xdg_decoration_manager->events.new_toplevel_decoration, &server->new_decoration, new_decoration_notify);

   struct wlr_presentation *presentation = wlr_presentation_create(server->display, server->backend);
   wlr_scene_set_presentation(server->scene, presentation);

   wl_global_create(server->display, &zdwl_ipc_manager_v2_interface, 2, NULL, dwl_ipc_manager_bind);

#if XWAYLAND
   if(!(server->xwayland = wlr_xwayland_create(server->display, server->compositor, true))) {
      say(INFO, "unable to create xwayland server. Continuing without it");
      return;
   }

   LISTEN(&server->xwayland->events.new_surface, &server->xwl_new_surface, xwl_new_surface_notify);
   LISTEN(&server->xwayland->events.ready, &server->xwl_ready, xwl_ready_notify);
#endif
}

void 
startServer(struct simple_server *server) 
{

   const char* socket = wl_display_add_socket_auto(server->display);
   if(!socket){
      cleanupServer(server);
      say(ERROR, "Unable to add socket to Wayland display!");
   }

   if(!wlr_backend_start(server->backend)){
      cleanupServer(server);
      say(ERROR, "Unable to start WLR backend!");
   }
   
   setenv("WAYLAND_DISPLAY", socket, true);
   say(INFO, "Wayland server is running on WAYLAND_DISPLAY=%s ...", socket);

#if XWAYLAND
   if(setenv("DISPLAY", server->xwayland->display_name, true) < 0)
      say(WARNING, " -> Unable to set DISPLAY for xwayland");
   else 
      say(INFO, " -> XWayland is running on display %s", server->xwayland->display_name);
#endif

   // choose initial output based on cursor position
   server->cur_output = get_output_at(server, server->cursor->x, server->cursor->y);

   print_server_info(server);
}

void 
cleanupServer(struct simple_server *server) 
{
   say(DEBUG, "cleanupServer");
#if XWAYLAND
   server->xwayland = NULL;
   wlr_xwayland_destroy(server->xwayland);
#endif
   //if(server->backend)
   //   wlr_backend_destroy(server->backend);

   wl_display_destroy_clients(server->display);
   wlr_xcursor_manager_destroy(server->cursor_manager);
   wlr_output_layout_destroy(server->output_layout);
   wl_display_destroy(server->display);
   // Destroy after the wayland display
   wlr_scene_node_destroy(&server->scene->tree.node);
   say(INFO, "Disconnected from display");
}

void
quitServer(struct simple_server *server) 
{
   wl_display_terminate(server->display);
}
