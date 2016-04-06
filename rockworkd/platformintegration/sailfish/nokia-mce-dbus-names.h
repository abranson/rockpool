/**
 * @file dbus-names.h
 * D-Bus Interface to the Mode Control Entity
 * <p>
 * This file is part of mce-dev
 * <p>
 * Copyright © 2004-2009 Nokia Corporation.
 * <p>
 * @author David Weinehall <david.weinehall@nokia.com>
 *
 * These headers are free software; you can redistribute them
 * and/or modify them under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * These headers are distributed in the hope that they will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this software; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#ifndef _MCE_DBUS_NAMES_H_
#define _MCE_DBUS_NAMES_H_

/**
 * @name D-Bus Daemon
 */

/*@{*/

/** MCE D-Bus service */
#define MCE_SERVICE			"com.nokia.mce"

/** MCE D-Bus Request interface */
#define MCE_REQUEST_IF			"com.nokia.mce.request"
/** MCE D-Bus Signal interface */
#define MCE_SIGNAL_IF			"com.nokia.mce.signal"
/** MCE D-Bus Request path */
#define MCE_REQUEST_PATH		"/com/nokia/mce/request"
/** MCE D-Bus Signal path */
#define MCE_SIGNAL_PATH			"/com/nokia/mce/signal"

/** The MCE D-Bus error interface; currently not used */
#define MCE_ERROR_FATAL			"com.nokia.mce.error.fatal"
/** The D-Bus interface for invalid arguments; currently not used */
#define MCE_ERROR_INVALID_ARGS		"org.freedesktop.DBus.Error.InvalidArgs"

/*@}*/

/**
 * @name Generic D-Bus methods
 */

/*@{*/

/**
 * Query the device mode
 *
 * @since v0.5.3
 * @return @c gchar @c * with the device mode
 *         (see @ref mce/mode-names.h for valid device modes)
 */
#define MCE_DEVICE_MODE_GET		"get_device_mode"

/**
 * Query the call state
 *
 * @since v1.8.1
 * @return @c gchar @c * with the new call state
 *             (see @ref mce/mode-names.h for valid call states)
 * @return @c gchar @c * with the new emergency state type
 *             (see @ref mce/mode-names.h for valid emergency state types)
 */
#define MCE_CALL_STATE_GET		"get_call_state"

/**
 * Query the device lock mode
 *
 * @since v0.8.0
 * @return @c gchar @c * with the device lock mode
 *         (see @ref mce/mode-names.h for valid lock modes)
 */
#define MCE_DEVLOCK_MODE_GET		"get_devicelock_mode"

/**
 * Query the touchscreen/keypad lock mode
 *
 * @since v1.4.0
 * @return @c gchar @c * with the touchscreen/keypad lock mode
 *         (see @ref mce/mode-names.h for valid lock modes)
 */
#define MCE_TKLOCK_MODE_GET		"get_tklock_mode"

/**
 * Query the display status
 *
 * @since v1.5.21
 * @return @c gchar @c * with the display state
 *         (see @ref mce/mode-names.h for valid display states)
 */
#define MCE_DISPLAY_STATUS_GET		"get_display_status"

/**
 * Query CABC mode
 *
 * @since v1.8.13
 * @return @c gchar @c * with the CABC mode
 *         (see @ref mce/mode-names.h for valid CABC modes)
 */
#define MCE_CABC_MODE_GET		"get_cabc_mode"

/**
 * Query the inactivity status
 *
 * @since v1.5.2
 * @return @c dbus_bool_t @c TRUE if the system is inactive,
 *                        @c FALSE if the system is active
 */
#define MCE_INACTIVITY_STATUS_GET	"get_inactivity_status"

/**
 * Query the device orientation information
 *
 * @since v1.8.1
 * @return @c gchar @c * portrait/landscape orientation
 *         (see @ref mce/mode-names.h for valid portrait/landscape states)
 * @return @c gchar @c * on/off stand
 *         (see @ref mce/mode-names.h for valid stand states)
 * @return @c gchar @c * face up/face down
 *         (see @ref mce/mode-names.h for valid facing states)
 * @return @c dbus_int32_t x axis (unit mG)
 * @return @c dbus_int32_t y axis (unit mG)
 * @return @c dbus_int32_t z axis (unit mG)
 */
#define MCE_DEVICE_ORIENTATION_GET	"get_device_orientation"

/**
 * Query the MCE version
 *
 * @since v1.1.6
 * @return @c gchar @c * with the MCE version
 */
#define MCE_VERSION_GET			"get_version"

/**
 * Unblank display
 *
 * @since v0.5
 */
#define MCE_DISPLAY_ON_REQ		"req_display_state_on"

/**
 * Dim display
 *
 * @since v1.6.20
 */
#define MCE_DISPLAY_DIM_REQ		"req_display_state_dim"

/**
 * Blank display
 *
 * @since v1.6.20
 */
#define MCE_DISPLAY_OFF_REQ		"req_display_state_off"

/**
 * Prevent display from blanking
 *
 * @since v0.5
 */
#define MCE_PREVENT_BLANK_REQ		"req_display_blanking_pause"

/**
 * Prevent keypad off
 *
 * On this request mce keeps keypad 
 * unblocked next 60 seconds
 *
 * @since v1.8.17
 */
#define MCE_PREVENT_KEYPAD_OFF_REQ		"req_keypad_off_pause"

/**
 * Request CABC mode change
 *
 * @since v1.8.13
 * @param mode @c gchar @c * with the new CABC mode
 *             (see @ref mce/mode-names.h for valid CABC modes)
 */
#define MCE_CABC_MODE_REQ		"req_cabc_mode"

/**
 * Request device mode change
 *
 * @since v0.5
 * @param mode @c gchar @c * with the new device mode
 *             (see @ref mce/mode-names.h for valid device modes)
 */
#define MCE_DEVICE_MODE_CHANGE_REQ	"req_device_mode_change"

/**
 * Request call state change
 * Changes can only be made to/from the "none" state; all other
 * transitions will be vetoed.  This is to avoid race conditions
 * between different services.
 *
 * An exception is made when the tuple is active:emergency;
 * such requests are always accepted
 *
 * If the service that requests the transition emits a NameOwnerChanged,
 * then the call state will revert back to "none", to ensure that
 * crashing applications doesn't cause a forever busy call state
 *
 * @since v1.8.1
 * @param call_state @c gchar @c * with the new call state
 *             (see @ref mce/mode-names.h for valid call states)
 * @param call_type @c gchar @c * with the new call type
 *             (see @ref mce/mode-names.h for valid call types)
 * @return @c dbus_bool_t @c TRUE if the new call state was accepted,
 *                        @c FALSE if the new call state was vetoed
 */
#define MCE_CALL_STATE_CHANGE_REQ	"req_call_state_change"

/**
 * Request tklock mode change
 *
 * @since v1.4.0
 * @param mode @c gchar @c * with the new touchscreen/keypad lock mode
 *             (see @ref mce/mode-names.h for valid lock modes)
 */
#define MCE_TKLOCK_MODE_CHANGE_REQ	"req_tklock_mode_change"

/**
 * Request powerkey event triggering
 *
 * @since v1.5.3
 * @param longpress @c dbus_bool_t with the type of powerkey event to
 *                  trigger; @c FALSE == short powerkey press,
 *                           @c TRUE == long powerkey press
 */
#define MCE_TRIGGER_POWERKEY_EVENT_REQ	"req_trigger_powerkey_event"

/**
 * Request to enable accelerometer.
 * If we have at least one active listener, device orientation change
 * messages are broadcasted with  MCE_DEVICE_ORIENTATION_SIG. 
 * If no listeners exist, then accelerometer is disabled and no messages are sent.
 * If this request message indicates reply requested (dbus_no_reply is set FALSE),
 * then reply message is sent with current orientation. This eliminates the 
 * need for separate MCE_DEVICE_ORIENTATION_GET when starting listening orientation.
 * When application no more needs the orientation information it should send
 * MCE_ACCELEROMETER_DISABLE_REQ.
 *
 * @return @c gchar @c * portrait/landscape orientation
 *         (see @ref mce/mode-names.h for valid portrait/landscape states)
 * @return @c gchar @c * on/off stand
 *         (see @ref mce/mode-names.h for valid stand states)
 * @return @c gchar @c * face up/face down
 *         (see @ref mce/mode-names.h for valid facing states)
 * @return @c dbus_int32_t x axis (unit mG)
 * @return @c dbus_int32_t y axis (unit mG)
 * @return @c dbus_int32_t z axis (unit mG)
 *
 * @since v1.8.14
 */
#define MCE_ACCELEROMETER_ENABLE_REQ	"req_accelerometer_enable"

/**
 * Request to disable accelerometer.
 * If we have at least one active listener, device orientation change
 * messages are broadcasted with  MCE_DEVICE_ORIENTATION_SIG. 
 * If no listeners exist, then accelerometer is disabled and no messages are sent.
 *
 * @since v1.8.14
 */
#define MCE_ACCELEROMETER_DISABLE_REQ	"req_accelerometer_disable"

/*@}*/

/**
 * @name Generic D-Bus signals
 */

/*@{*/

/**
 * Signal that indicates that the device lock mode has changed
 *
 * @since v0.8.0
 * @return @c gchar @c * with the new device lock mode
 *         (see @ref mce/mode-names.h for valid lock modes)
 */
#define MCE_DEVLOCK_MODE_SIG		"devicelock_mode_ind"

/**
 * Signal that indicates that the touchscreen/keypad lock mode has changed
 *
 * @since v1.4.0
 * @return @c gchar @c * with the new touchscreen/keypad lock mode
 *         (see @ref mce/mode-names.h for valid lock modes)
 */
#define MCE_TKLOCK_MODE_SIG		"tklock_mode_ind"

/**
 * Notify everyone that the display is on/dimmed/off
 *
 * @since v1.5.21
 * @return @c gchar @c * with the display state
 *         (see @ref mce/mode-names.h for valid display states)
 */
#define MCE_DISPLAY_SIG			"display_status_ind"

/**
 * Notify everyone that the system is active/inactive
 *
 * @since v0.9.3
 * @return @c dbus_bool_t @c TRUE if the system is inactive,
 *                        @c FALSE if the system is active
 */
#define MCE_INACTIVITY_SIG		"system_inactivity_ind"

/**
 * Notify everyone that the device mode has changed
 *
 * @since v0.5
 * @return @c gchar @c * with the new device mode
 *         (see @ref mce/mode-names.h for valid device modes)
 */
#define MCE_DEVICE_MODE_SIG		"sig_device_mode_ind"

/**
 * Notify everyone that the call state has changed
 *
 * @since v1.8.1
 * @return @c gchar @c * with the new call state
 *             (see @ref mce/mode-names.h for valid call states)
 * @return @c gchar @c * with the new emergency state type
 *             (see @ref mce/mode-names.h for valid emergency state types)
 */
#define MCE_CALL_STATE_SIG		"sig_call_state_ind"

/**
 * Notify everyone that the home button was pressed (short press)
 *
 * @since v0.3
 */
#define MCE_HOME_KEY_SIG		"sig_home_key_pressed_ind"

/**
 * Notify everyone that the home button was pressed (long press)
 *
 * @since v0.3
 */
#define MCE_HOME_KEY_LONG_SIG		"sig_home_key_pressed_long_ind"

/**
 * Notify everyone that the device orientation has changed
 * Note that this message is sent only if there is at least one active listener.
 * See MCE_ACCELEROMETER_ENABLE_REQ and MCE_ACCELEROMETER_DISABLE_REQ
 *
 * @since v1.8.1
 * @return @c gchar @c * portrait/landscape orientation
 *         (see @ref mce/mode-names.h for valid portrait/landscape states)
 * @return @c gchar @c * on/off stand
 *         (see @ref mce/mode-names.h for valid stand states)
 * @return @c gchar @c * face up/face down
 *         (see @ref mce/mode-names.h for valid facing states)
 * @return @c dbus_int32_t x axis (unit mG)
 * @return @c dbus_int32_t y axis (unit mG)
 * @return @c dbus_int32_t z axis (unit mG)
 */
#define MCE_DEVICE_ORIENTATION_SIG	"sig_device_orientation_ind"

/*@}*/

/**
 * @name LED interface D-Bus methods
 */

/*@{*/

/**
 * Activates a pre-defined LED pattern
 * Non-existing patterns are ignored
 *
 * @since v1.5.0
 * @param pattern @c gchar @c * with the pattern name
 *                (see @ref /etc/mce/mce.ini for valid pattern names)
 */
#define MCE_ACTIVATE_LED_PATTERN	"req_led_pattern_activate"

/**
 * Deactivates a pre-defined LED pattern
 * Non-existing patterns are ignored
 *
 * @since v1.5.0
 * @param pattern @c gchar @c * with the pattern name
 *                (see @ref /etc/mce/mce.ini for valid pattern names)
 */
#define MCE_DEACTIVATE_LED_PATTERN	"req_led_pattern_deactivate"

/**
 * Enable LED; this does not affect the LED pattern stack
 * Note: The GConf setting for LED flashing overrides this value
 *       If the pattern stack does not contain any active patterns,
 *       the LED logic will still remain in enabled mode,
 *       but will not display any patterns until a pattern is activated
 *
 * @since v1.5.0
 */
#define MCE_ENABLE_LED			"req_led_enable"

/**
 * Disable LED; this does not affect the LED pattern stack
 *
 * @since v1.5.0
 */
#define MCE_DISABLE_LED			"req_led_disable"

/*@}*/

/**
 * @name Vibrator interface D-Bus methods
 */

/*@{*/

/**
 * Activates a pre-defined vibrator pattern
 * Non-existing patterns are ignored
 *
 * @since v1.8.0
 * @param pattern @c gchar @c * with the pattern name
 *                (see @ref /etc/mce/mce.ini for valid pattern names)
 */
#define MCE_ACTIVATE_VIBRATOR_PATTERN	"req_vibrator_pattern_activate"

/**
 * Deactivates a pre-defined vibrator pattern
 * Non-existing patterns are ignored
 *
 * @since v1.8.0
 * @param pattern @c gchar @c * with the pattern name
 *                (see @ref /etc/mce/mce.ini for valid pattern names)
 */
#define MCE_DEACTIVATE_VIBRATOR_PATTERN	"req_vibrator_pattern_deactivate"

/**
 * Enable vibrator; this does not affect the vibrator pattern stack
 * Note: The profile settings for vibrator overrides this value
 *       If the pattern stack does not contain any active patterns,
 *       the vibrator logic will still remain in enabled mode,
 *       but will not display any patterns until a pattern is activated
 *
 * @since v1.8.0
 */
#define MCE_ENABLE_VIBRATOR		"req_vibrator_enable"

/**
 * Disable vibrator; this does not affect the vibrator pattern stack
 *
 * @since v1.8.0
 */
#define MCE_DISABLE_VIBRATOR		"req_vibrator_disable"

/**
 * Activate user defined vibrator pattern
 *
 * @since v1.8.18
 * @param speed @c int32 @c * vibration speed, value between -255 - 255
 * @param duration @c int32 @c * vibration duration in milliseconds (0 means forever)
 */
#define MCE_START_MANUAL_VIBRATION	"req_start_manual_vibration"

/**
 * Stop manual vibration pattern before duration is expired
 *
 * @since v1.8.18
 */
#define MCE_STOP_MANUAL_VIBRATION		"req_stop_manual_vibration"

/**
 * Query the keyboard backlight status
 *
 * @since v1.8.19
 */
#define  MCE_KEYBOARD_STATUS_GET		"get_keyboard_status"

/*@}*/

#endif /* _MCE_DBUS_NAMES_H_ */
