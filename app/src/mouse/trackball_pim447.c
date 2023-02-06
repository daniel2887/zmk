/*
 * Copyright (c) 2021 Cedric VINCENT - original code
 * Copyright (c) 2022 voidyourwarranty@mailbox.org - extension & modification
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include <drivers/sensor.h>
#include <drivers/ext_power.h>
#include <logging/log.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/trackball_pim447.h>

#define LOG_LEVEL CONFIG_SENSOR_LOG_LEVEL
LOG_MODULE_REGISTER(PIM447, CONFIG_SENSOR_LOG_LEVEL);
//LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define MOVE_X_FACTOR  DT_PROP(DT_INST(0, pimoroni_trackball_pim447), move_factor_x)
#define MOVE_Y_FACTOR  DT_PROP(DT_INST(0, pimoroni_trackball_pim447), move_factor_y)
#define MOVE_X_INVERT  DT_PROP(DT_INST(0, pimoroni_trackball_pim447), invert_move_x)
#define MOVE_Y_INVERT  DT_PROP(DT_INST(0, pimoroni_trackball_pim447), invert_move_y)
#define MOVE_X_INERTIA DT_PROP(DT_INST(0, pimoroni_trackball_pim447), move_inertia_x)
#define MOVE_Y_INERTIA DT_PROP(DT_INST(0, pimoroni_trackball_pim447), move_inertia_y)
#define FACTOR_X  (MOVE_X_FACTOR * (MOVE_X_INVERT ? -1 : 1))
#define FACTOR_Y  (MOVE_Y_FACTOR * (MOVE_Y_INVERT ? -1 : 1))

#define SCROLL_X_INVERT   DT_PROP(DT_INST(0, pimoroni_trackball_pim447), invert_scroll_x)
#define SCROLL_Y_INVERT   DT_PROP(DT_INST(0, pimoroni_trackball_pim447), invert_scroll_y)
#define SCROLL_X_DIVISOR  DT_PROP(DT_INST(0, pimoroni_trackball_pim447), scroll_divisor_x)
#define SCROLL_Y_DIVISOR  DT_PROP(DT_INST(0, pimoroni_trackball_pim447), scroll_divisor_y)
#define DIVISOR_X  (SCROLL_X_DIVISOR * (SCROLL_X_INVERT ? -1 : 1))
#define DIVISOR_Y  (SCROLL_Y_DIVISOR * (SCROLL_Y_INVERT ?  1 : -1))

#define SWAP_AXES      DT_PROP(DT_INST(0, pimoroni_trackball_pim447), swap_axes)
#define POLL_INTERVAL  DT_PROP(DT_INST(0, pimoroni_trackball_pim447), poll_interval)

#define BUTTON    DT_PROP(DT_INST(0, pimoroni_trackball_pim447), button)
#define NORM      DT_PROP(DT_INST(0, pimoroni_trackball_pim447), norm)
#define EXACTNESS DT_PROP(DT_INST(0, pimoroni_trackball_pim447), exactness)
#define MAX_ACCEL DT_PROP(DT_INST(0, pimoroni_trackball_pim447), max_accel)

#define POWER_LAYER  DT_PROP(DT_INST(0, pimoroni_trackball_pim447), power_layer)
#define IDLE_TIMEOUT DT_PROP(DT_INST(0, pimoroni_trackball_pim447), idle_timeout)

static int mode = DT_PROP(DT_INST(0, pimoroni_trackball_pim447), mode);

#define ABS(x) ((x<0)?(-x):(x))
#define GRACE_PERIOD 100

/*static char dbg_buf[256] = {0};*/
/*static int dbg_chars_written = 0;*/

/*
 * The function <zmk_trackball_pim447_set_mode()> allows behaviors to change the track ball mode.
 */

void zmk_trackball_pim447_set_mode(int new_mode)
{
    switch (new_mode) {
        case PIM447_MOVE:
        case PIM447_SCROLL:
            mode = new_mode;
            break;

       case PIM447_TOGGLE:
            mode = mode == PIM447_MOVE
                   ? PIM447_SCROLL
                   : PIM447_MOVE;
            break;

       default:
            break;
    }
}

static struct k_timer trackball_idle_timer; // timer that resets the keyboard to the default layer if idle for a certain period

/*
 * The function <trackball_idle_timer_expiry_function()> is called after <IDLE_TIMEOUT> seconds of idle period and
 * resets the keyboard to layer 0.
 */

static void trackball_idle_timer_expiry_function ( struct k_timer *timer_id ) {
    // TODO: This won't work when GAMING layer is active; it'll reset back to default layer...
  zmk_keymap_layer_to (0);
}

int ds87_accel(int val) {
    static const int pow[] = {0,3,7,10,48,86,124,162,200,260,319,379,438,498,558,617,677,737,796,856,915,975,1035,1094,1154,1213,1273,1333,1392,1452};
    static const size_t pow_sz = sizeof(pow) / sizeof(pow[0]);

    int sign = val < 0 ? -1 : 1;
    int abs = ABS(val);

    if (abs > pow_sz) {
        return pow[pow_sz - 1] * sign;
    } else {
        return pow[abs] * sign;
    }
}

#define DELTA_HIST_LEN 8
static int sent_idx = 0;
static int last_n_sent_dx[DELTA_HIST_LEN] = {0};
static int last_n_sent_dy[DELTA_HIST_LEN] = {0};

void clear_delta_history() {
    memset(last_n_sent_dx, 0, sizeof(last_n_sent_dx));
    memset(last_n_sent_dy, 0, sizeof(last_n_sent_dy));
    sent_idx = 0;
}

int filter_delta_history(int *last_n_sent, int win_len) {
    // TODO: For now, assume samples are perfectly spaced apart
    int sum = 0;
    for (int i = 0; i < win_len; i++) {
        sum += last_n_sent[i];
    }

    return sum / win_len;
}

/*
 * The main thread of the track ball driver. Once instance of this tread is created by the present module.
 */

static void thread_code(void *p1, void *p2, void *p3)
{
    const struct device *dev;
    int result;

    /* PIM447 trackball initialization. */

    const char *label = DT_LABEL(DT_INST(0, pimoroni_trackball_pim447));
    dev = device_get_binding(label);
    if (dev == NULL) {
        LOG_ERR("Cannot get TRACKBALL_PIM447 device");
        return;
    }

    /* Event loop. */

    bool button_press_sent   = false;
    bool button_release_sent = false;

    while (true) {
        struct sensor_value pos_dx, pos_dy, pos_dz;
        bool send_report = false;
        int clear = PIM447_NONE;

        result = sensor_sample_fetch(dev);
        if (result < 0) {
            LOG_ERR("Failed to fetch TRACKBALL_PIM447 sample");
            return;
        }

        result = sensor_channel_get(dev, SENSOR_CHAN_POS_DX, &pos_dx);
        if (result < 0) {
            LOG_ERR("Failed to get TRACKBALL_PIM447 pos_dx channel value");
            return;
        }

        result = sensor_channel_get(dev, SENSOR_CHAN_POS_DY, &pos_dy);
        if (result < 0) {
            LOG_ERR("Failed to get TRACKBALL_PIM447 pos_dy channel value");
            return;
        }

        result = sensor_channel_get(dev, SENSOR_CHAN_POS_DZ, &pos_dz);
        if (result < 0) {
            LOG_ERR("Failed to get TRACKBALL_PIM447 pos_dz channel value");
            return;
        }

        if (pos_dx.val1 != 0 || pos_dy.val1 != 0) {
            if (SWAP_AXES) {
                int32_t tmp = pos_dx.val1;
                pos_dx.val1 = pos_dy.val1;
                pos_dy.val1 = tmp;
            }
	}

	switch (mode) {
	default:
	case PIM447_MOVE:

	  {
          /*memset(dbg_buf, 0, sizeof(dbg_buf));*/
          /*dbg_chars_written = 0;*/

		/*dbg_chars_written += snprintf(dbg_buf+dbg_chars_written, sizeof(dbg_buf), "%d,%d,%d,%d,%d,%d,",*/
                /*stored_dx, stored_dy, pos_dx.val1, pos_dy.val1, pos_dx.val1*FACTOR_X, pos_dy.val1*FACTOR_Y);*/

          int dx = pos_dx.val1*FACTOR_X/100;
          int dy = pos_dy.val1*FACTOR_Y/100;
          int accel_dx = ds87_accel(dx);
          int accel_dy = ds87_accel(dy);

          // Bias the faster axis towards the slower
          /*if (ABS(accel_dx) > ABS(accel_dy)) {*/
              /*accel_dx = (accel_dx - accel_dy) * 80 / 100 + accel_dy;*/
          /*} else if (ABS(accel_dy) > ABS(accel_dx)) {*/
              /*accel_dy = (accel_dy - accel_dx) * 80 / 100 + accel_dx;*/
          /*}*/

           last_n_sent_dx[sent_idx] = accel_dx;
           last_n_sent_dy[sent_idx] = accel_dy;
           sent_idx = (sent_idx + 1) % DELTA_HIST_LEN;

           int send_dx = filter_delta_history(last_n_sent_dx, DELTA_HIST_LEN);
           int send_dy = filter_delta_history(last_n_sent_dy, DELTA_HIST_LEN);

        if ((send_dx != 0) || (send_dy != 0)) {
            /*char buf[256] = {0};*/
            /*int chars_written = 0;*/
            /*chars_written += snprintf(buf + chars_written, sizeof(buf), "; ");*/
            /*for (int i = 0; i < DELTA_HIST_LEN; i++) {*/
                /*chars_written += snprintf(buf + chars_written, sizeof(buf), "%6d", last_n_sent_dx[i]);*/
            /*}*/
            /*chars_written += snprintf(buf + chars_written, sizeof(buf), "; ");*/
            /*for (int i = 0; i < DELTA_HIST_LEN; i++) {*/
                /*chars_written += snprintf(buf + chars_written, sizeof(buf), "%6d", last_n_sent_dy[i]);*/
            /*}*/
          LOG_DBG("ds87: %6d,%6d,%6d,%6d", dx, dy, send_dx, send_dy);
	      zmk_hid_mouse_movement_set (send_dx,send_dy);

	      send_report = true;
	      clear = PIM447_MOVE;
        }
	  }

	  break;

	case PIM447_SCROLL:

	  {
          // TODO: Dividing here zeros out low scorll speeds (causes these events to be ignored)
          // Fix this (switching to speed, dx/s and dy/s, and spreading out low speed events over a number
          // of loops, based on polling rate, could work.)
	    int dx = pos_dx.val1 / DIVISOR_X;
	    int dy = pos_dy.val1 / DIVISOR_Y;

	    zmk_hid_mouse_scroll_set (dx,dy);

	    send_report = true;
	    clear = PIM447_MOVE;
	  }

	  break;
	}

        if (pos_dz.val1 == 0x80 && button_press_sent == false) {
            zmk_hid_mouse_button_press(BUTTON);
            button_press_sent   = true;
            button_release_sent = false;
            send_report = true;
        } else if (pos_dz.val1 == 0x01 && button_release_sent == false) {
            zmk_hid_mouse_button_release(BUTTON);
            button_press_sent   = false;
            button_release_sent = true;
            send_report = true;
        }

        if (send_report) {
            zmk_endpoints_send_mouse_report();

            switch (clear) {
                case PIM447_MOVE: zmk_hid_mouse_movement_set(0, 0); break;
                case PIM447_SCROLL: zmk_hid_mouse_scroll_set(0, 0); break;
                default: break;
            }

	    if (IDLE_TIMEOUT) {
	      k_timer_stop (&trackball_idle_timer);
	      k_timer_start (&trackball_idle_timer,K_SECONDS (IDLE_TIMEOUT),K_SECONDS (0));
	    }
        }

        k_sleep (K_MSEC (POLL_INTERVAL));
    }
}

/*
 * The function <trackball_keycode_state_changed_callback()> is the callback associated with events of the type
 * <zmk_keycode_state_changed> as defined in ./app/include/zmk/events/keycode_state_changed.c.
 */

static int trackball_keycode_changed_callback ( const zmk_event_t *ev ) {

  if (IDLE_TIMEOUT) {
    k_timer_stop (&trackball_idle_timer);
    k_timer_start (&trackball_idle_timer,K_SECONDS (IDLE_TIMEOUT),K_SECONDS (0)); // restart the timer that resets the layer to default after a certain idle period
  }

  return (ZMK_EV_EVENT_BUBBLE); // bubble this event b/c other listeners may need to see it, too
}

ZMK_LISTENER (trackball_keycode_changed,trackball_keycode_changed_callback); // the above function is a listener
ZMK_SUBSCRIPTION (trackball_keycode_changed,zmk_keycode_state_changed);      // subscribe to all events of type <zmk_keycode_state_changed>

#define STACK_SIZE 1024

static K_THREAD_STACK_DEFINE(thread_stack, STACK_SIZE);
static struct k_thread thread;
static k_tid_t thread_id;              // the ID of the track ball driver thread created by the present module
static const struct device *ext_power; // device that controls the 3V3 output pin of the Nice!Nano

/*
 * The function <trackball_layer_changed_callback()> is the callback associated with events of the type
 * <zmk_layer_state_changed> as defined in ./app/include/zmk/events/layer_state_changed.c.
 */

static int trackball_layer_changed_callback ( const zmk_event_t *ev ) {

  struct zmk_layer_state_changed *data = as_zmk_layer_state_changed (ev); // obtain the event specific data

  if (POWER_LAYER) { // if POWER_LAYER is 0, output 3V3 is always on and the track ball driver thread always running, then the following is not needed
    if (data->layer == POWER_LAYER) { // if the state of the layer changes whose activation turns the track ball on and off
      if (data->state) {
	LOG_DBG ("Track ball layer %d activated; resuming track ball driver.\n",POWER_LAYER);

	if (ext_power) {
	  int power = ext_power_get (ext_power);
	  if (!power) { // power is off but ought to be switched on
	    int rc = ext_power_enable (ext_power);
            if (rc)
	      LOG_ERR ("Unable to enable EXT_POWER: %d",rc);

	    LOG_DBG ("External power ON.\n");
	  }
        } else {
	  LOG_DBG ("External power not controlled by track ball driver.\n");
	}

	if (IDLE_TIMEOUT) {
	  k_timer_stop (&trackball_idle_timer);
	  k_timer_start (&trackball_idle_timer,K_SECONDS (IDLE_TIMEOUT),K_SECONDS (0));
	}

	k_sleep (K_MSEC (GRACE_PERIOD));
    clear_delta_history();
	k_thread_resume (thread_id);  // resume the track ball driver thread
      } else {
	LOG_DBG ("Track ball layer %d deactivated, suspending track ball driver.\n",POWER_LAYER);
	k_thread_suspend (thread_id); // suspend the track ball driver thread
	k_sleep (K_MSEC (GRACE_PERIOD));

	if (IDLE_TIMEOUT) {
	  k_timer_stop (&trackball_idle_timer);
	}

	if (ext_power) {
	  int power = ext_power_get (ext_power);
	  if (power) { // power is on but ought to be switched off
	    int rc = ext_power_disable (ext_power);
            if (rc)
	      LOG_ERR ("Unable to disable EXT_POWER: %d",rc);

	    LOG_DBG ("External power OFF.\n");
	  }
        } else {
	  LOG_DBG ("External power not controlled by track ball driver.\n");
	}
      }
    }
  }

  return (ZMK_EV_EVENT_BUBBLE); // bubble this event b/c other listeners may need to see it, too
}

ZMK_LISTENER (trackball_layer_changed,trackball_layer_changed_callback); // the above function is a listener
ZMK_SUBSCRIPTION (trackball_layer_changed,zmk_layer_state_changed);      // subscribe to all events of type <zmk_layer_state_changed>

/*
 * Initialize the track ball driver thread.
 */

int zmk_trackball_pim447_init()
{

  ext_power = device_get_binding ("EXT_POWER"); // the device that controls 3V3 output of the Nice!Nano
  if (!ext_power) {
    LOG_ERR ("Unable to retrieve ext_power device: EXT_POWER");
  }

  if (ext_power) {
    if (!POWER_LAYER) { // if POWER_LAYER is 0, then the track ball is always on
      LOG_DBG ("Track ball always on.");

      if (ext_power) {
	int power = ext_power_get (ext_power);
	if (!power) { // power is on but ought to be switched off
	  int rc = ext_power_enable (ext_power);
	  if (rc)
	    LOG_ERR ("Unable to enable EXT_POWER: %d",rc);

	  LOG_DBG ("External power ON.");
	}
      } else {
	LOG_DBG ("External power not controlled by track ball driver.");
      }
    }
  }

  k_sleep (K_MSEC (GRACE_PERIOD));
  thread_id = k_thread_create (&thread, thread_stack, STACK_SIZE, thread_code, NULL, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);

  if (POWER_LAYER) { // if POWER_LAYER is not 0, then power of the track ball depends on a specific layer
    LOG_DBG ("Track ball depends on layer %d; initially suspending track ball driver.",POWER_LAYER);
    k_thread_suspend (thread_id); // in this case, we suspend the track ball until the layer is activated
    k_sleep (K_MSEC (GRACE_PERIOD));

    if (ext_power) {
      int power = ext_power_get (ext_power);
      if (power) { // power is on but ought to be switched off
	int rc = ext_power_disable (ext_power);
	if (rc)
	  LOG_ERR ("Unable to disable EXT_POWER: %d",rc);

	LOG_DBG ("External power OFF.");
      }
    } else {
      LOG_DBG ("External power not controlled by track ball driver.");
    }
  }

  k_timer_init (&trackball_idle_timer,trackball_idle_timer_expiry_function,NULL);

  return 0;
}

SYS_INIT(zmk_trackball_pim447_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
