#include <gint/usb.h>
#include <gint/usb-hid-kbd.h>
#include <gint/cpu.h>
#include <string.h>
#include <endian.h>

//---
// USB HID Keyboard Descriptors
//---

static usb_dc_interface_t dc_interface = {
    .bLength              = sizeof(usb_dc_interface_t),
    .bDescriptorType      = USB_DC_INTERFACE,
    .bInterfaceNumber     = -1, /* Set by driver */
    .bAlternateSetting    = 0,
    .bNumEndpoints        = 1,
    .bInterfaceClass      = 0x03, /* HID */
    .bInterfaceSubClass   = 0x01, /* Boot Interface */
    .bInterfaceProtocol   = 0x01, /* Keyboard */
    .iInterface           = 0,
};

/* HID Descriptor */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdHID;
    uint8_t  bCountryCode;
    uint8_t  bNumDescriptors;
    uint8_t  bDescriptorType2;
    uint16_t wDescriptorLength;
} GPACKED(4) usb_dc_hid_t;

static usb_dc_hid_t dc_hid = {
    .bLength            = sizeof(usb_dc_hid_t),
    .bDescriptorType    = 0x21, /* HID */
    .bcdHID             = htole16(0x0111), /* HID 1.11 */
    .bCountryCode       = 0, /* Not localized */
    .bNumDescriptors    = 1,
    .bDescriptorType2   = 0x22, /* Report descriptor */
    .wDescriptorLength  = htole16(63), /* Report descriptor length */
};

/* HID Report Descriptor for boot keyboard */
static uint8_t const hid_report_descriptor[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop) */
    0x09, 0x06,        /* Usage (Keyboard) */
    0xA1, 0x01,        /* Collection (Application) */
    
    /* Modifier keys */
    0x05, 0x07,        /*   Usage Page (Key Codes) */
    0x19, 0xE0,        /*   Usage Minimum (224) */
    0x29, 0xE7,        /*   Usage Maximum (231) */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x25, 0x01,        /*   Logical Maximum (1) */
    0x75, 0x01,        /*   Report Size (1) */
    0x95, 0x08,        /*   Report Count (8) */
    0x81, 0x02,        /*   Input (Data, Variable, Absolute) */
    
    /* Reserved byte */
    0x95, 0x01,        /*   Report Count (1) */
    0x75, 0x08,        /*   Report Size (8) */
    0x81, 0x01,        /*   Input (Constant) */
    
    /* LED report */
    0x95, 0x05,        /*   Report Count (5) */
    0x75, 0x01,        /*   Report Size (1) */
    0x05, 0x08,        /*   Usage Page (LEDs) */
    0x19, 0x01,        /*   Usage Minimum (1) */
    0x29, 0x05,        /*   Usage Maximum (5) */
    0x91, 0x02,        /*   Output (Data, Variable, Absolute) */
    
    /* LED report padding */
    0x95, 0x01,        /*   Report Count (1) */
    0x75, 0x03,        /*   Report Size (3) */
    0x91, 0x01,        /*   Output (Constant) */
    
    /* Key arrays (6 keys) */
    0x95, 0x06,        /*   Report Count (6) */
    0x75, 0x08,        /*   Report Size (8) */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x25, 0x65,        /*   Logical Maximum (101) */
    0x05, 0x07,        /*   Usage Page (Key Codes) */
    0x19, 0x00,        /*   Usage Minimum (0) */
    0x29, 0x65,        /*   Usage Maximum (101) */
    0x81, 0x00,        /*   Input (Data, Array) */
    
    0xC0               /* End Collection */
};

/* Endpoint for keyboard reports (calculator -> PC) */
static usb_dc_endpoint_t dc_endpoint_in = {
    .bLength             = sizeof(usb_dc_endpoint_t),
    .bDescriptorType     = USB_DC_ENDPOINT,
    .bEndpointAddress    = 0x81, /* 1 IN */
    .bmAttributes        = 0x03, /* Interrupt transfer */
    .wMaxPacketSize      = htole16(8),
    .bInterval           = 10, /* Poll every 10ms */
};

usb_interface_t const usb_hid_kbd = {
    /* List of descriptors */
    .dc = (void const *[]){
        &dc_interface,
        &dc_hid,
        &dc_endpoint_in,
        NULL,
    },
    /* Parameters for each endpoint */
    .params = (usb_interface_endpoint_t []){
        { .endpoint     = 0x81, /* 1 IN */
          .buffer_size  = 64, },
        { 0 },
    },
    .notify_read = NULL,
};

GCONSTRUCTOR static void set_strings(void)
{
    dc_interface.iInterface = usb_dc_string(u"HID Keyboard", 0);
}

//---
// Keyboard report structure
//---

typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} GPACKED(1) hid_keyboard_report_t;

//---
// Keyboard control functions
//---

int usb_hid_kbd_send(uint8_t modifiers, 
                     uint8_t key1, uint8_t key2, uint8_t key3,
                     uint8_t key4, uint8_t key5, uint8_t key6)
{
    if(!usb_is_open_interface(&usb_hid_kbd))
        return -1;
    
    hid_keyboard_report_t report;
    report.modifiers = modifiers;
    report.reserved = 0;
    report.keys[0] = key1;
    report.keys[1] = key2;
    report.keys[2] = key3;
    report.keys[3] = key4;
    report.keys[4] = key5;
    report.keys[5] = key6;
    
    int pipe = usb_interface_pipe(&usb_hid_kbd, 0x81);
    usb_write_sync(pipe, &report, sizeof(report), false);
    usb_commit_sync(pipe);
    
    return 0;
}

int usb_hid_kbd_press(uint8_t modifiers, uint8_t key)
{
    int rc;
    
    /* Press key */
    rc = usb_hid_kbd_send(modifiers, key, 0, 0, 0, 0, 0);
    if(rc < 0) return rc;
    
    /* Minimal delay - just enough for USB polling */
    for(volatile int i = 0; i < 3000; i++);
    
    /* Release key */
    rc = usb_hid_kbd_send(0, 0, 0, 0, 0, 0, 0);
    if(rc < 0) return rc;
    
    /* Minimal delay */
    for(volatile int i = 0; i < 3000; i++);
    
    return 0;
}

int usb_hid_kbd_type_string(char const *str)
{
    if(!str) return -1;
    
    while(*str) {
        uint8_t modifiers = 0;
        uint8_t key = HID_KEY_NONE;
        
        char c = *str++;
        
        /* Handle letters */
        if(c >= 'a' && c <= 'z') {
            key = HID_KEY_A + (c - 'a');
        }
        else if(c >= 'A' && c <= 'Z') {
            key = HID_KEY_A + (c - 'A');
            modifiers = HID_MOD_LSHIFT;
        }
        /* Handle numbers */
        else if(c >= '1' && c <= '9') {
            key = HID_KEY_1 + (c - '1');
        }
        else if(c == '0') {
            key = HID_KEY_0;
        }
        /* Handle special characters */
        else if(c == ' ') {
            key = HID_KEY_SPACE;
        }
        else if(c == '\n') {
            key = HID_KEY_ENTER;
        }
        else if(c == '\t') {
            key = HID_KEY_TAB;
        }
        else if(c == '-') {
            key = HID_KEY_MINUS;
        }
        else if(c == '=') {
            key = HID_KEY_EQUAL;
        }
        else if(c == '[') {
            key = HID_KEY_LEFTBRACE;
        }
        else if(c == ']') {
            key = HID_KEY_RIGHTBRACE;
        }
        else if(c == '\\') {
            key = HID_KEY_BACKSLASH;
        }
        else if(c == ';') {
            key = HID_KEY_SEMICOLON;
        }
        else if(c == '\'') {
            key = HID_KEY_APOSTROPHE;
        }
        else if(c == '`') {
            key = HID_KEY_GRAVE;
        }
        else if(c == ',') {
            key = HID_KEY_COMMA;
        }
        else if(c == '.') {
            key = HID_KEY_DOT;
        }
        else if(c == '/') {
            key = HID_KEY_SLASH;
        }
        /* Shifted special characters */
        else if(c == '!') {
            key = HID_KEY_1;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '@') {
            key = HID_KEY_2;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '#') {
            key = HID_KEY_3;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '$') {
            key = HID_KEY_4;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '%') {
            key = HID_KEY_5;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '^') {
            key = HID_KEY_6;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '&') {
            key = HID_KEY_7;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '*') {
            key = HID_KEY_8;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '(') {
            key = HID_KEY_9;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == ')') {
            key = HID_KEY_0;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '_') {
            key = HID_KEY_MINUS;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '+') {
            key = HID_KEY_EQUAL;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '{') {
            key = HID_KEY_LEFTBRACE;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '}') {
            key = HID_KEY_RIGHTBRACE;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '|') {
            key = HID_KEY_BACKSLASH;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == ':') {
            key = HID_KEY_SEMICOLON;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '"') {
            key = HID_KEY_APOSTROPHE;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '~') {
            key = HID_KEY_GRAVE;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '<') {
            key = HID_KEY_COMMA;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '>') {
            key = HID_KEY_DOT;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '?') {
            key = HID_KEY_SLASH;
            modifiers = HID_MOD_LSHIFT;
        }
        else {
            /* Skip unsupported characters */
            continue;
        }
        
        int rc = usb_hid_kbd_press(modifiers, key);
        if(rc < 0) return rc;
    }
    
    return 0;
}

int usb_hid_kbd_type_string_progress(char const *str, usb_hid_kbd_progress_cb callback)
{
    if(!str) return -1;
    
    int total = strlen(str);
    int current = 0;
    char const *ptr = str;
    
    while(*ptr) {
        uint8_t modifiers = 0;
        uint8_t key = HID_KEY_NONE;
        
        char c = *ptr++;
        current++;
        
        /* Handle letters */
        if(c >= 'a' && c <= 'z') {
            key = HID_KEY_A + (c - 'a');
        }
        else if(c >= 'A' && c <= 'Z') {
            key = HID_KEY_A + (c - 'A');
            modifiers = HID_MOD_LSHIFT;
        }
        /* Handle numbers */
        else if(c >= '1' && c <= '9') {
            key = HID_KEY_1 + (c - '1');
        }
        else if(c == '0') {
            key = HID_KEY_0;
        }
        /* Handle special characters */
        else if(c == ' ') {
            key = HID_KEY_SPACE;
        }
        else if(c == '\n') {
            key = HID_KEY_ENTER;
        }
        else if(c == '\t') {
            key = HID_KEY_TAB;
        }
        else if(c == '-') {
            key = HID_KEY_MINUS;
        }
        else if(c == '=') {
            key = HID_KEY_EQUAL;
        }
        else if(c == '[') {
            key = HID_KEY_LEFTBRACE;
        }
        else if(c == ']') {
            key = HID_KEY_RIGHTBRACE;
        }
        else if(c == '\\') {
            key = HID_KEY_BACKSLASH;
        }
        else if(c == ';') {
            key = HID_KEY_SEMICOLON;
        }
        else if(c == '\'') {
            key = HID_KEY_APOSTROPHE;
        }
        else if(c == '`') {
            key = HID_KEY_GRAVE;
        }
        else if(c == ',') {
            key = HID_KEY_COMMA;
        }
        else if(c == '.') {
            key = HID_KEY_DOT;
        }
        else if(c == '/') {
            key = HID_KEY_SLASH;
        }
        /* Shifted special characters */
        else if(c == '!') {
            key = HID_KEY_1;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '@') {
            key = HID_KEY_2;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '#') {
            key = HID_KEY_3;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '$') {
            key = HID_KEY_4;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '%') {
            key = HID_KEY_5;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '^') {
            key = HID_KEY_6;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '&') {
            key = HID_KEY_7;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '*') {
            key = HID_KEY_8;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '(') {
            key = HID_KEY_9;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == ')') {
            key = HID_KEY_0;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '_') {
            key = HID_KEY_MINUS;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '+') {
            key = HID_KEY_EQUAL;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '{') {
            key = HID_KEY_LEFTBRACE;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '}') {
            key = HID_KEY_RIGHTBRACE;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '|') {
            key = HID_KEY_BACKSLASH;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == ':') {
            key = HID_KEY_SEMICOLON;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '"') {
            key = HID_KEY_APOSTROPHE;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '~') {
            key = HID_KEY_GRAVE;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '<') {
            key = HID_KEY_COMMA;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '>') {
            key = HID_KEY_DOT;
            modifiers = HID_MOD_LSHIFT;
        }
        else if(c == '?') {
            key = HID_KEY_SLASH;
            modifiers = HID_MOD_LSHIFT;
        }
        else {
            /* Skip unsupported characters, but still update progress */
            if(callback) callback(current, total);
            continue;
        }
        
        int rc = usb_hid_kbd_press(modifiers, key);
        if(rc < 0) return rc;
        
        /* Report progress after each character */
        if(callback) callback(current, total);
    }
    
    return 0;
}
