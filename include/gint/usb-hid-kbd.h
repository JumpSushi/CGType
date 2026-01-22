//---
// gint:usb-hid-kbd - USB HID Keyboard interface
//
// This interface (class 0x03/0x01/0x01) implements a standard USB HID keyboard
// that can send keypresses to a connected computer. The calculator appears as
// a regular keyboard to the host system.
//---

#ifndef GINT_USB_HID_KBD
#define GINT_USB_HID_KBD

#ifdef __cplusplus
extern "C" {
#endif

#include <gint/usb.h>
#include <stdint.h>
#include <stdbool.h>

/* This HID keyboard interface with class code 0x03/0x01/0x01 implements a 
   standard USB keyboard using the boot protocol. You can use it to send
   keypresses from the calculator to a connected computer. */
extern usb_interface_t const usb_hid_kbd;

//---
// USB HID Keyboard keycodes (based on HID Usage Tables)
//---

/* Standard USB HID keyboard keycodes */
enum {
    HID_KEY_NONE = 0x00,
    
    /* Letters A-Z */
    HID_KEY_A = 0x04,
    HID_KEY_B = 0x05,
    HID_KEY_C = 0x06,
    HID_KEY_D = 0x07,
    HID_KEY_E = 0x08,
    HID_KEY_F = 0x09,
    HID_KEY_G = 0x0A,
    HID_KEY_H = 0x0B,
    HID_KEY_I = 0x0C,
    HID_KEY_J = 0x0D,
    HID_KEY_K = 0x0E,
    HID_KEY_L = 0x0F,
    HID_KEY_M = 0x10,
    HID_KEY_N = 0x11,
    HID_KEY_O = 0x12,
    HID_KEY_P = 0x13,
    HID_KEY_Q = 0x14,
    HID_KEY_R = 0x15,
    HID_KEY_S = 0x16,
    HID_KEY_T = 0x17,
    HID_KEY_U = 0x18,
    HID_KEY_V = 0x19,
    HID_KEY_W = 0x1A,
    HID_KEY_X = 0x1B,
    HID_KEY_Y = 0x1C,
    HID_KEY_Z = 0x1D,
    
    /* Numbers 1-0 */
    HID_KEY_1 = 0x1E,
    HID_KEY_2 = 0x1F,
    HID_KEY_3 = 0x20,
    HID_KEY_4 = 0x21,
    HID_KEY_5 = 0x22,
    HID_KEY_6 = 0x23,
    HID_KEY_7 = 0x24,
    HID_KEY_8 = 0x25,
    HID_KEY_9 = 0x26,
    HID_KEY_0 = 0x27,
    
    /* Special keys */
    HID_KEY_ENTER = 0x28,
    HID_KEY_ESC = 0x29,
    HID_KEY_BACKSPACE = 0x2A,
    HID_KEY_TAB = 0x2B,
    HID_KEY_SPACE = 0x2C,
    HID_KEY_MINUS = 0x2D,        /* - */
    HID_KEY_EQUAL = 0x2E,        /* = */
    HID_KEY_LEFTBRACE = 0x2F,    /* [ */
    HID_KEY_RIGHTBRACE = 0x30,   /* ] */
    HID_KEY_BACKSLASH = 0x31,    /* \ */
    HID_KEY_SEMICOLON = 0x33,    /* ; */
    HID_KEY_APOSTROPHE = 0x34,   /* ' */
    HID_KEY_GRAVE = 0x35,        /* ` */
    HID_KEY_COMMA = 0x36,        /* , */
    HID_KEY_DOT = 0x37,          /* . */
    HID_KEY_SLASH = 0x38,        /* / */
    
    /* Function keys */
    HID_KEY_F1 = 0x3A,
    HID_KEY_F2 = 0x3B,
    HID_KEY_F3 = 0x3C,
    HID_KEY_F4 = 0x3D,
    HID_KEY_F5 = 0x3E,
    HID_KEY_F6 = 0x3F,
    HID_KEY_F7 = 0x40,
    HID_KEY_F8 = 0x41,
    HID_KEY_F9 = 0x42,
    HID_KEY_F10 = 0x43,
    HID_KEY_F11 = 0x44,
    HID_KEY_F12 = 0x45,
    
    /* Arrow keys */
    HID_KEY_RIGHT = 0x4F,
    HID_KEY_LEFT = 0x50,
    HID_KEY_DOWN = 0x51,
    HID_KEY_UP = 0x52,
};

/* Modifier key flags (bit flags for modifier byte) */
enum {
    HID_MOD_LCTRL = 0x01,
    HID_MOD_LSHIFT = 0x02,
    HID_MOD_LALT = 0x04,
    HID_MOD_LMETA = 0x08,
    HID_MOD_RCTRL = 0x10,
    HID_MOD_RSHIFT = 0x20,
    HID_MOD_RALT = 0x40,
    HID_MOD_RMETA = 0x80,
};

//---
// Keyboard control functions
//---

/* usb_hid_kbd_send(): Send a keyboard report
   
   Sends a keyboard report with the specified modifier keys and up to 6
   simultaneous keypresses. The report will be sent to the host.
   
   @modifiers  Bitfield of modifier keys (HID_MOD_* flags)
   @key1-6     Up to 6 simultaneous key codes (use HID_KEY_NONE for unused)
   Returns 0 on success, negative on error */
int usb_hid_kbd_send(uint8_t modifiers, 
                     uint8_t key1, uint8_t key2, uint8_t key3,
                     uint8_t key4, uint8_t key5, uint8_t key6);

/* usb_hid_kbd_press(): Press and release a single key
   
   This is a convenience function that presses a key, releases it, and sends
   both reports. For sending modifier keys, use the modifiers parameter.
   
   @modifiers  Bitfield of modifier keys (HID_MOD_* flags)
   @key        Key code to press
   Returns 0 on success, negative on error */
int usb_hid_kbd_press(uint8_t modifiers, uint8_t key);

/* usb_hid_kbd_type_string(): Type a string
   
   Types a string character by character. Only supports basic ASCII characters
   that can be typed with a US keyboard layout.
   
   @str        Null-terminated string to type
   Returns 0 on success, negative on error */
int usb_hid_kbd_type_string(char const *str);

/* Progress callback type for usb_hid_kbd_type_string_progress */
typedef void (*usb_hid_kbd_progress_cb)(int current, int total);

/* usb_hid_kbd_type_string_progress(): Type a string with progress updates
   
   Same as usb_hid_kbd_type_string but calls a callback after each character
   to report progress. Useful for showing a progress bar.
   
   @str        Null-terminated string to type
   @callback   Function called with (current_char, total_chars) after each char
   Returns 0 on success, negative on error */
int usb_hid_kbd_type_string_progress(char const *str, usb_hid_kbd_progress_cb callback);

#ifdef __cplusplus
}
#endif

#endif /* GINT_USB_HID_KBD */
