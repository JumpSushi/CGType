/*
 * math2-editor.c - Clean math editor for Casio CG50
 * 
 * Tree-based expression editing with USB keyboard output
 */

#include <gint/display.h>
#include <gint/keyboard.h>
#include <gint/gint.h>
#include <gint/usb.h>
#include <gint/rtc.h>
#include <string.h>
#include <stdio.h>

#include "math2.h"
#include "usb-hid-kbd.h"

/* External variables (defined in math2.c) */
extern bool g_color_brackets;
extern bool g_cursor_visible;

/* ===== Screen Layout ===== */

#define SCREEN_W    396
#define SCREEN_H    224

#define HEADER_H    22
#define STATUS_H    20
#define INPUT_Y     80      /* Center expression vertically */
#define PREVIEW_Y   155     /* LaTeX preview position */

/* ===== Colors - Native Casio Style ===== */

#define COL_HEADER_BG   C_RGB(0, 0, 12)
#define COL_HEADER_TEXT C_WHITE
#define COL_BG          C_WHITE
#define COL_TEXT        C_BLACK
#define COL_TEXT_DIM    C_RGB(16, 16, 16)
#define COL_TEXT_GREY   C_RGB(12, 12, 12)
#define COL_STATUS_BG   C_RGB(28, 28, 28)
#define COL_STATUS_TEXT C_RGB(20, 20, 20)
#define COL_MODE_ON     C_RGB(31, 20, 0)
#define COL_PREVIEW_BG  C_RGB(30, 30, 30)
#define COL_PREVIEW_TEXT C_BLACK
#define COL_SENDING_BG  C_RGB(0, 0, 20)
#define COL_BOX_BORDER  C_BLACK
#define COL_SEPARATOR   C_RGB(22, 22, 22)
#define COL_LATEX_BOX   C_RGB(8, 12, 20)  /* Light blue */

/* Cursor flash state */
static int g_cursor_flash = 0;

/* ===== Mode Selection ===== */

#define MODE_NUMPAD 0
#define MODE_LATEX  1

static int current_mode = MODE_LATEX;
static int progress_percent = 0;

/* ===== Settings ===== */

typedef struct {
    bool color_brackets;    /* Color-code nested brackets */
    bool clear_on_send;     /* Clear expression after sending */
    bool show_latex;        /* Show LaTeX preview */
    bool wrap_in_dollars;   /* Wrap LaTeX in $...$ */
} settings_t;

static settings_t g_settings = {
    .color_brackets = true,
    .clear_on_send = false,
    .show_latex = true,
    .wrap_in_dollars = false,
};

/* Alpha lock state (persists across key presses) */
static bool g_alpha_lock = false;

/* USB interfaces */
static usb_interface_t const *interfaces[] = { &usb_hid_kbd, NULL };

/* ===== USB Connection Screen ===== */

/* Timeout for USB operations in 128Hz ticks (5 seconds) */
#define USB_TIMEOUT_TICKS (5 * 128)

/* Global cancel flag for sending */
static volatile bool g_cancel_send = false;

static bool check_cancel_send(void)
{
    /* Check for AC key press without blocking */
    key_event_t ev = pollevent();
    if(ev.type == KEYEV_DOWN && ev.key == KEY_ACON) {
        g_cancel_send = true;
        return true;
    }
    return g_cancel_send;
}

static bool wait_for_usb_connection(void)
{
    int frame_count = 0;
    
    while(1) {
        dclear(COL_BG);
        
        /* Header */
        drect(0, 0, SCREEN_W, HEADER_H - 1, COL_HEADER_BG);
        dtext(8, (HEADER_H - 11) / 2, COL_HEADER_TEXT, "CGType");
        
        /* Connection status */
        bool connected = usb_is_open_interface(&usb_hid_kbd);
        
        if(connected) {
            /* USB connected - show success briefly and return */
            dtext_opt(SCREEN_W / 2, SCREEN_H / 2 - 20, C_RGB(0, 20, 0), C_NONE,
                      DTEXT_CENTER, DTEXT_MIDDLE, "USB Connected!");
            dtext_opt(SCREEN_W / 2, SCREEN_H / 2 + 10, COL_TEXT_DIM, C_NONE,
                      DTEXT_CENTER, DTEXT_MIDDLE, "Starting...");
            dupdate();
            for(volatile int i = 0; i < 50000; i++);
            return true;
        }
        
        /* Not connected - show waiting message */
        dtext_opt(SCREEN_W / 2, SCREEN_H / 2 - 30, COL_TEXT, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, "Please connect USB cable");
        
        /* Animated dots based on frame count */
        int dots = (frame_count / 30) % 4;  /* Change every ~30 frames */
        char dot_str[8] = "...";
        dot_str[dots] = '\0';
        dtext_opt(SCREEN_W / 2, SCREEN_H / 2, COL_TEXT_DIM, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, dot_str);
        
        /* USB icon placeholder */
        dtext_opt(SCREEN_W / 2, SCREEN_H / 2 + 30, COL_TEXT_DIM, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, "[USB]");
        
        /* Status bar */
        drect(0, SCREEN_H - STATUS_H, SCREEN_W, SCREEN_H, COL_STATUS_BG);
        dtext_opt(SCREEN_W / 2, SCREEN_H - STATUS_H/2, COL_STATUS_TEXT, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, "EXIT to quit");
        
        dupdate();
        frame_count++;
        
        /* Check for exit */
        key_event_t ev = pollevent();
        if(ev.type == KEYEV_DOWN && ev.key == KEY_EXIT) {
            return false;
        }
        
        /* Small delay */
        for(volatile int i = 0; i < 5000; i++);
    }
}

/* ===== Variable Menu ===== */

static char show_variable_menu(void)
{
    const char *vars[] = {"x", "y", "z", "a", "b", "c", "n", "t"};
    const char *greek[] = {"α", "β", "γ", "θ", "λ", "μ", "ω", "Δ"};
    int sel = 0;
    int page = 0; /* 0 = latin, 1 = greek */
    
    while(1) {
        dclear(COL_BG);
        
        /* Header */
        drect(0, 0, SCREEN_W, HEADER_H - 1, COL_HEADER_BG);
        dtext_opt(SCREEN_W / 2, HEADER_H / 2, COL_HEADER_TEXT, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, 
                  page == 0 ? "Variables" : "Greek Letters");
        
        /* Draw grid */
        const char **list = page == 0 ? vars : greek;
        int cols = 4;
        int cell_w = 60;
        int cell_h = 40;
        int start_x = (SCREEN_W - cols * cell_w) / 2;
        int start_y = 60;
        
        for(int i = 0; i < 8; i++) {
            int row = i / cols;
            int col = i % cols;
            int x = start_x + col * cell_w;
            int y = start_y + row * cell_h;
            
            if(i == sel) {
                drect(x, y, x + cell_w - 2, y + cell_h - 2, COL_HEADER_BG);
                dtext_opt(x + cell_w/2, y + cell_h/2, C_WHITE, C_NONE,
                          DTEXT_CENTER, DTEXT_MIDDLE, list[i]);
            } else {
                drect_border(x, y, x + cell_w - 2, y + cell_h - 2, C_WHITE, 1, COL_TEXT_DIM);
                dtext_opt(x + cell_w/2, y + cell_h/2, COL_TEXT, C_NONE,
                          DTEXT_CENTER, DTEXT_MIDDLE, list[i]);
            }
        }
        
        /* Status */
        drect(0, SCREEN_H - STATUS_H, SCREEN_W, SCREEN_H, COL_STATUS_BG);
        dtext_opt(SCREEN_W / 2, SCREEN_H - STATUS_H/2, COL_STATUS_TEXT, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, 
                  "Arrows:Select  EXE:Insert  F1/F2:Page  EXIT:Cancel");
        
        dupdate();
        
        key_event_t ev = getkey();
        
        if(ev.key == KEY_EXIT) return 0;
        if(ev.key == KEY_EXE) {
            return page == 0 ? vars[sel][0] : greek[sel][0];
        }
        if(ev.key == KEY_LEFT && sel % cols > 0) sel--;
        if(ev.key == KEY_RIGHT && sel % cols < cols - 1) sel++;
        if(ev.key == KEY_UP && sel >= cols) sel -= cols;
        if(ev.key == KEY_DOWN && sel < cols) sel += cols;
        if(ev.key == KEY_F1) page = 0;
        if(ev.key == KEY_F2) page = 1;
    }
}

/* ===== Settings Menu ===== */

static void show_settings_menu(void)
{
    int selected = 0;  /* 0=color, 1=clear, 2=preview, 3=wrap */
    const int num_settings = 4;
    
    while(1) {
        dclear(COL_BG);
        
        /* Header */
        drect(0, 0, SCREEN_W, HEADER_H - 1, COL_HEADER_BG);
        dtext_opt(SCREEN_W / 2, HEADER_H / 2, COL_HEADER_TEXT, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, "Settings");
        
        int y = 50;
        int row_h = 35;
        
        /* Setting 1: Color brackets */
        if(selected == 0) {
            drect(15, y - 5, SCREEN_W - 15, y + 25, C_RGB(28, 28, 30));
        }
        dtext(25, y, COL_TEXT, "Color Brackets:");
        dtext(SCREEN_W - 60, y, g_settings.color_brackets ? C_RGB(0, 20, 0) : C_RGB(20, 0, 0),
              g_settings.color_brackets ? "On" : "Off");
        y += row_h;
        
        /* Setting 2: Clear on send */
        if(selected == 1) {
            drect(15, y - 5, SCREEN_W - 15, y + 25, C_RGB(28, 28, 30));
        }
        dtext(25, y, COL_TEXT, "Clear after send:");
        dtext(SCREEN_W - 60, y, g_settings.clear_on_send ? C_RGB(0, 20, 0) : C_RGB(20, 0, 0),
              g_settings.clear_on_send ? "On" : "Off");
        y += row_h;
        
        /* Setting 3: Show preview */
        if(selected == 2) {
            drect(15, y - 5, SCREEN_W - 15, y + 25, C_RGB(28, 28, 30));
        }
        dtext(25, y, COL_TEXT, "Show preview:");
        dtext(SCREEN_W - 60, y, g_settings.show_latex ? C_RGB(0, 20, 0) : C_RGB(20, 0, 0),
              g_settings.show_latex ? "On" : "Off");
        y += row_h;
        
        /* Setting 4: Wrap in dollars */
        if(selected == 3) {
            drect(15, y - 5, SCREEN_W - 15, y + 25, C_RGB(28, 28, 30));
        }
        dtext(25, y, COL_TEXT, "Wrap in $...$:");
        dtext(SCREEN_W - 60, y, g_settings.wrap_in_dollars ? C_RGB(0, 20, 0) : C_RGB(20, 0, 0),
              g_settings.wrap_in_dollars ? "On" : "Off");
        
        /* Function key bar at bottom - replaces status bar */
        int fkey_h = 16;
        int fkey_y = SCREEN_H - fkey_h;
        int fkey_w = SCREEN_W / 6;  /* 6 function keys */
        
        /* F1: ON */
        drect(0, fkey_y, fkey_w - 2, SCREEN_H, COL_HEADER_BG);
        dtext_opt(fkey_w / 2, fkey_y + fkey_h / 2, C_WHITE, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, "ON");
        
        /* F2: OFF */
        drect(fkey_w, fkey_y, fkey_w * 2 - 2, SCREEN_H, COL_HEADER_BG);
        dtext_opt(fkey_w + fkey_w / 2, fkey_y + fkey_h / 2, C_WHITE, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, "OFF");
        
        dupdate();
        
        key_event_t ev = getkey();
        
        if(ev.key == KEY_EXIT || ev.key == KEY_MENU) return;
        
        /* Navigate */
        if(ev.key == KEY_UP && selected > 0) selected--;
        if(ev.key == KEY_DOWN && selected < num_settings - 1) selected++;
        
        /* Set ON */
        if(ev.key == KEY_F1) {
            switch(selected) {
                case 0: g_settings.color_brackets = true; g_color_brackets = true; break;
                case 1: g_settings.clear_on_send = true; break;
                case 2: g_settings.show_latex = true; break;
                case 3: g_settings.wrap_in_dollars = true; break;
            }
        }
        
        /* Set OFF */
        if(ev.key == KEY_F2) {
            switch(selected) {
                case 0: g_settings.color_brackets = false; g_color_brackets = false; break;
                case 1: g_settings.clear_on_send = false; break;
                case 2: g_settings.show_latex = false; break;
                case 3: g_settings.wrap_in_dollars = false; break;
            }
        }
    }
}

/* ===== Progress Callback ===== */

/* Store latex string for progress display */
static const char *g_sending_latex = NULL;

static void update_progress(int current, int total)
{
    progress_percent = (current * 100) / total;
    
    /* Redraw full screen to show progress */
    dclear(COL_BG);
    
    /* Header */
    drect(0, 0, SCREEN_W, HEADER_H - 1, COL_HEADER_BG);
    dtext_opt(SCREEN_W / 2, HEADER_H / 2, COL_HEADER_TEXT, C_NONE,
              DTEXT_CENTER, DTEXT_MIDDLE, "Sending to PC...");
    
    /* Show LaTeX with sent/unsent highlighting */
    int latex_y = 50;
    dtext(10, latex_y, COL_TEXT_DIM, "LaTeX:");
    
    if(g_sending_latex && total > 0) {
        /* Draw sent portion in green */
        char sent_part[128];
        char unsent_part[128];
        int len = strlen(g_sending_latex);
        int sent_len = (current < len) ? current : len;
        int unsent_len = len - sent_len;
        
        /* Truncate for display if needed */
        int max_display = 50;
        int sent_display = (sent_len > max_display) ? max_display : sent_len;
        int unsent_display = (unsent_len > (max_display - sent_display)) ? (max_display - sent_display) : unsent_len;
        
        if(sent_display > 0) {
            strncpy(sent_part, g_sending_latex, sent_display);
            sent_part[sent_display] = '\0';
            dtext(10, latex_y + 18, C_RGB(0, 20, 0), sent_part);  /* Green = sent */
        }
        
        if(unsent_display > 0) {
            strncpy(unsent_part, g_sending_latex + sent_len, unsent_display);
            unsent_part[unsent_display] = '\0';
            /* Draw unsent after sent */
            int sent_width = sent_display * 6;  /* Approximate char width */
            dtext(10 + sent_width, latex_y + 18, COL_TEXT_DIM, unsent_part);  /* Gray = unsent */
        }
        
        if(len > max_display) {
            dtext(10 + max_display * 6, latex_y + 18, COL_TEXT_DIM, "...");
        }
    }
    
    /* Progress bar */
    int bar_w = 300;
    int bar_h = 24;
    int bar_x = (SCREEN_W - bar_w) / 2;
    int bar_y = SCREEN_H / 2 + 20;
    
    int fill_w = (bar_w * progress_percent) / 100;
    
    /* Filled portion (green) */
    if(fill_w > 0) {
        drect(bar_x + 2, bar_y + 2, bar_x + 2 + fill_w, bar_y + bar_h - 2, C_RGB(0, 24, 0));
    }
    /* Border */
    drect_border(bar_x, bar_y, bar_x + bar_w, bar_y + bar_h, C_NONE, 2, COL_HEADER_BG);
    
    /* Percentage text */
    char percent_str[16];
    sprintf(percent_str, "%d%%", progress_percent);
    dtext_opt(SCREEN_W / 2, bar_y + bar_h + 15, COL_TEXT, C_NONE,
              DTEXT_CENTER, DTEXT_MIDDLE, percent_str);
    
    /* Status bar */
    drect(0, SCREEN_H - STATUS_H, SCREEN_W, SCREEN_H, COL_STATUS_BG);
    
    char status[64];
    sprintf(status, "Sent %d / %d characters  |  AC: Cancel", current, total);
    dtext_opt(SCREEN_W / 2, SCREEN_H - STATUS_H/2, COL_STATUS_TEXT, C_NONE,
              DTEXT_CENTER, DTEXT_MIDDLE, status);
    
    dupdate();
}

/* ===== Drawing Functions ===== */

static void draw_header(math_expr2_t *expr)
{
    drect(0, 0, SCREEN_W, HEADER_H - 1, COL_HEADER_BG);
    
    dtext(8, (HEADER_H - 11) / 2, COL_HEADER_TEXT, "Math Editor");
    
    /* Mode indicators */
    int x = SCREEN_W - 80;
    if(expr->shift_mode) {
        drect(x, 4, x + 30, HEADER_H - 5, COL_MODE_ON);
        dtext(x + 3, 7, C_BLACK, "SHF");
    }
    x += 35;
    if(g_alpha_lock) {
        /* Alpha lock - different color to indicate locked */
        drect(x, 4, x + 30, HEADER_H - 5, C_RGB(20, 25, 0));
        dtext(x + 3, 7, C_BLACK, "A-L");
    } else if(expr->alpha_mode) {
        drect(x, 4, x + 30, HEADER_H - 5, COL_MODE_ON);
        dtext(x + 3, 7, C_BLACK, "ALP");
    }
}

static void draw_input_area(math_expr2_t *expr)
{
    /* Input box - aligned with LaTeX separator */
    int box_top = HEADER_H + 8;
    int box_bottom = PREVIEW_Y - 12;
    int box_height = box_bottom - box_top;
    
    /* Match width with LaTeX separator (10px margins) */
    drect(10, box_top, SCREEN_W - 10, box_bottom, COL_BG);
    drect_border(10, box_top, SCREEN_W - 10, box_bottom, C_NONE, 2, COL_BOX_BORDER);
    
    /* Check if expression is empty */
    bool is_empty = (expr->root && expr->root->data.seq.first == NULL);
    
    if(is_empty) {
        /* Show placeholder text */
        dtext_opt(SCREEN_W / 2, box_top + box_height / 2, COL_TEXT_GREY, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, "Enter expression...");
    } else {
        /* Get expression dimensions */
        int expr_h = math2_get_height(expr);
        
        /* Horizontal: start from left side with padding */
        int x = 25;
        
        /* Vertical: center expression in box
         * math2_draw expects y to be the top of the expression */
        int y = box_top + (box_height - expr_h) / 2;
        
        math2_draw(expr, x, y);
    }
}

static void draw_latex_preview(math_expr2_t *expr)
{
    if(!g_settings.show_latex) return;
    
    /* Generate LaTeX */
    math2_to_latex(expr);
    
    int preview_top = PREVIEW_Y;
    
    /* LaTeX label in grey */
    dtext(12, preview_top, COL_TEXT_GREY, "LaTeX:");
    
    /* Separator line - matches input box width */
    int sep_y = preview_top + 16;
    drect(10, sep_y, SCREEN_W - 10, sep_y + 1, COL_SEPARATOR);
    
    /* Preview text in black */
    char display[65];
    if(strlen(expr->latex) > 58) {
        strncpy(display, expr->latex, 55);
        display[55] = '\0';
        strcat(display, "...");
    } else if(strlen(expr->latex) == 0) {
        strcpy(display, "(empty)");
    } else {
        strcpy(display, expr->latex);
    }
    
    dtext(12, sep_y + 8, COL_PREVIEW_TEXT, display);
}

static void draw_status_bar(math_expr2_t *expr)
{
    /* Separator line on top */
    drect(0, SCREEN_H - STATUS_H - 1, SCREEN_W, SCREEN_H - STATUS_H, COL_SEPARATOR);
    
    /* Status bar background */
    drect(0, SCREEN_H - STATUS_H, SCREEN_W, SCREEN_H, COL_STATUS_BG);
    
    const char *hint;
    
    /* Context-sensitive hints */
    expr_node_t *seq = expr->cursor.sequence;
    if(seq && seq->parent) {
        node_type_t pt = seq->parent->type;
        if(pt == NODE_FRACTION) {
            if(seq == seq->parent->data.frac.numer)
                hint = "Numerator | DOWN:Denom | EXE:Exit";
            else
                hint = "Denominator | UP:Numer | EXE:Exit";
        } else if(pt == NODE_EXPONENT) {
            hint = "Exponent | EXE:Exit";
        } else if(pt == NODE_ROOT) {
            hint = "Root | EXE:Exit";
        } else {
            hint = "EXE:Exit | Arrows:Nav";
        }
    } else {
        hint = "EXE:Send | F1:Numpad | DEL:Delete";
    }
    
    dtext_opt(SCREEN_W / 2, SCREEN_H - STATUS_H/2, COL_STATUS_TEXT, C_NONE,
              DTEXT_CENTER, DTEXT_MIDDLE, hint);
}

static void draw_editor_ui(math_expr2_t *expr)
{
    dclear(COL_BG);
    draw_header(expr);
    draw_input_area(expr);
    draw_latex_preview(expr);
    draw_status_bar(expr);
    dupdate();
}

static void show_sending(math_expr2_t *expr)
{
    dclear(COL_BG);
    
    /* Header - match main UI */
    drect(0, 0, SCREEN_W, HEADER_H - 1, COL_HEADER_BG);
    dtext_opt(SCREEN_W / 2, HEADER_H / 2, COL_HEADER_TEXT, C_NONE,
              DTEXT_CENTER, DTEXT_MIDDLE, "Sending to PC...");
    
    /* Show LaTeX being sent */
    int latex_y = 50;
    dtext(10, latex_y, COL_TEXT_DIM, "LaTeX:");
    
    /* Display latex text (truncated if needed) */
    char display_latex[80];
    if(strlen(expr->latex) > 60) {
        strncpy(display_latex, expr->latex, 57);
        display_latex[57] = '.';
        display_latex[58] = '.';
        display_latex[59] = '.';
        display_latex[60] = '\0';
    } else {
        strcpy(display_latex, expr->latex);
    }
    dtext(10, latex_y + 18, COL_TEXT, display_latex);
    
    /* Progress bar - match status bar colors */
    int bar_w = 300;
    int bar_h = 24;
    int bar_x = (SCREEN_W - bar_w) / 2;
    int bar_y = SCREEN_H / 2 + 20;
    
    drect_border(bar_x, bar_y, bar_x + bar_w, bar_y + bar_h, COL_BG, 2, COL_HEADER_BG);
    dtext_opt(SCREEN_W / 2, bar_y + bar_h + 15, COL_TEXT_DIM, C_NONE,
              DTEXT_CENTER, DTEXT_MIDDLE, "0%");
    
    /* Status bar */
    drect(0, SCREEN_H - STATUS_H, SCREEN_W, SCREEN_H, COL_STATUS_BG);
    dtext_opt(SCREEN_W / 2, SCREEN_H - STATUS_H/2, COL_STATUS_TEXT, C_NONE,
              DTEXT_CENTER, DTEXT_MIDDLE, "Transmitting...");
    
    dupdate();
}

/* ===== Mode Selection Screen ===== */

static int show_mode_selection(void)
{
    while(1) {
        dclear(COL_BG);
        
        /* Header */
        drect(0, 0, SCREEN_W, HEADER_H - 1, COL_HEADER_BG);
        dtext(8, (HEADER_H - 11) / 2, COL_HEADER_TEXT, "CGType");
        
        /* Mode boxes */
        int box_w = 140;
        int box_h = 80;
        int center_y = SCREEN_H / 2 - 10;
        int numpad_x = SCREEN_W / 2 - box_w - 20;
        int latex_x = SCREEN_W / 2 + 20;
        
        /* Numpad mode box */
        drect_border(numpad_x, center_y - box_h/2, numpad_x + box_w, center_y + box_h/2,
                     C_RGB(28, 30, 28), 2, COL_HEADER_BG);
        dtext_opt(numpad_x + box_w / 2, center_y - 10, COL_TEXT, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, "Numpad");
        dtext_opt(numpad_x + box_w / 2, center_y + 10, COL_TEXT_DIM, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, "[F1]");
        
        /* LaTeX mode box */
        drect_border(latex_x, center_y - box_h/2, latex_x + box_w, center_y + box_h/2,
                     C_RGB(28, 28, 30), 2, COL_HEADER_BG);
        dtext_opt(latex_x + box_w / 2, center_y - 10, COL_TEXT, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, "LaTeX");
        dtext_opt(latex_x + box_w / 2, center_y + 10, COL_TEXT_DIM, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, "[F2]");
        
        /* Status bar */
        drect(0, SCREEN_H - STATUS_H, SCREEN_W, SCREEN_H, COL_STATUS_BG);
        dtext_opt(SCREEN_W / 2, SCREEN_H - STATUS_H/2, COL_STATUS_TEXT, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, "Press F1 or F2 to select | EXIT to quit");
        
        dupdate();
        
        key_event_t ev = getkey();
        if(ev.key == KEY_F1) return MODE_NUMPAD;
        if(ev.key == KEY_F2) return MODE_LATEX;
        if(ev.key == KEY_EXIT) return -1;
    }
}

/* ===== Numpad Mode ===== */

/* External brightness control (from r61524 driver) */
extern void r61525_brightness_set(int level);

/* Brightness levels: 1 (dimmest) to 5 (brightest) */
#define BRIGHTNESS_DIM    1
#define BRIGHTNESS_NORMAL 5

static void show_timeout_error(void)
{
    dclear(COL_BG);
    drect(0, 0, SCREEN_W, HEADER_H - 1, C_RGB(20, 0, 0));
    dtext_opt(SCREEN_W / 2, HEADER_H / 2, C_WHITE, C_NONE,
              DTEXT_CENTER, DTEXT_MIDDLE, "Timeout!");
    dtext_opt(SCREEN_W / 2, SCREEN_H / 2 - 10, COL_TEXT, C_NONE,
              DTEXT_CENTER, DTEXT_MIDDLE, "USB not responding");
    dtext_opt(SCREEN_W / 2, SCREEN_H / 2 + 20, COL_TEXT_DIM, C_NONE,
              DTEXT_CENTER, DTEXT_MIDDLE, "Check cable and press any key");
    dupdate();
    getkey();
}

static void draw_numpad_ui(bool is_dimmed)
{
    dclear(C_RGB(28, 30, 28));
    
    dtext_opt(SCREEN_W / 2, SCREEN_H / 2 - 20, COL_TEXT, C_NONE,
              DTEXT_CENTER, DTEXT_MIDDLE, "NUMPAD MODE");
    
    if(is_dimmed) {
        dtext_opt(SCREEN_W / 2, SCREEN_H / 2 + 10, COL_TEXT_DIM, C_NONE,
                  DTEXT_CENTER, DTEXT_MIDDLE, "(Press any key to wake)");
    }
    
    drect(0, SCREEN_H - STATUS_H, SCREEN_W, SCREEN_H, COL_STATUS_BG);
    dtext_opt(SCREEN_W / 2, SCREEN_H - STATUS_H/2, COL_STATUS_TEXT, C_NONE,
              DTEXT_CENTER, DTEXT_MIDDLE, "F1:LaTeX");
    
    dupdate();
}

static bool run_numpad_mode(void)
{
    uint32_t idle_loops = 0;
    bool is_dimmed = false;
    /* Roughly 2 seconds of polling loops (each loop ~10ms) */
    const uint32_t DIM_LOOP_COUNT = 80;
    
    while(1) {
        if(!is_dimmed) {
            draw_numpad_ui(false);
        }
        
        /* Use pollevent for non-blocking check */
        key_event_t ev = pollevent();
        
        if(ev.type == KEYEV_NONE) {
            /* No key pressed - increment idle counter */
            idle_loops++;
            
            /* Check for timeout and dim screen */
            if(!is_dimmed && idle_loops >= DIM_LOOP_COUNT) {
                r61525_brightness_set(BRIGHTNESS_DIM);
                is_dimmed = true;
            }
            
            /* Small delay */
            for(volatile int i = 0; i < 10000; i++);
            continue;
        }
        
        if(ev.type != KEYEV_DOWN) continue;
        
        /* Key pressed - reset idle counter */
        idle_loops = 0;
        
        /* Only F1 wakes from dim (to switch modes) */
        if(ev.key == KEY_F1) {
            r61525_brightness_set(BRIGHTNESS_NORMAL);
            return true;  /* Switch to LaTeX */
        }
        
        if(ev.key == KEY_MENU) {
            r61525_brightness_set(BRIGHTNESS_NORMAL);
            /* OS menu (can return to app) */
            usb_close();
            gint_osmenu();
            usb_open(interfaces, GINT_CALL_NULL);
            usb_open_wait();
            continue;
        }
        
        /* Process keys even while dimmed - no need to see screen */
        
        /* Backspace */
        if(ev.key == KEY_DEL) {
            int rc = usb_hid_kbd_press_timeout(0, HID_KEY_BACKSPACE, USB_TIMEOUT_TICKS);
            if(rc == -3) show_timeout_error();
            continue;
        }
        
        /* Arrow keys */
        if(ev.key == KEY_UP) {
            int rc = usb_hid_kbd_press_timeout(0, HID_KEY_UP, USB_TIMEOUT_TICKS);
            if(rc == -3) show_timeout_error();
            continue;
        }
        if(ev.key == KEY_DOWN) {
            int rc = usb_hid_kbd_press_timeout(0, HID_KEY_DOWN, USB_TIMEOUT_TICKS);
            if(rc == -3) show_timeout_error();
            continue;
        }
        if(ev.key == KEY_LEFT) {
            int rc = usb_hid_kbd_press_timeout(0, HID_KEY_LEFT, USB_TIMEOUT_TICKS);
            if(rc == -3) show_timeout_error();
            continue;
        }
        if(ev.key == KEY_RIGHT) {
            int rc = usb_hid_kbd_press_timeout(0, HID_KEY_RIGHT, USB_TIMEOUT_TICKS);
            if(rc == -3) show_timeout_error();
            continue;
        }
        
        /* Numbers - with timeout handling */
        int rc = 0;
        if(ev.key == KEY_0) rc = usb_hid_kbd_press_timeout(0, HID_KEY_0, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_1) rc = usb_hid_kbd_press_timeout(0, HID_KEY_1, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_2) rc = usb_hid_kbd_press_timeout(0, HID_KEY_2, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_3) rc = usb_hid_kbd_press_timeout(0, HID_KEY_3, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_4) rc = usb_hid_kbd_press_timeout(0, HID_KEY_4, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_5) rc = usb_hid_kbd_press_timeout(0, HID_KEY_5, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_6) rc = usb_hid_kbd_press_timeout(0, HID_KEY_6, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_7) rc = usb_hid_kbd_press_timeout(0, HID_KEY_7, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_8) rc = usb_hid_kbd_press_timeout(0, HID_KEY_8, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_9) rc = usb_hid_kbd_press_timeout(0, HID_KEY_9, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_DOT) rc = usb_hid_kbd_press_timeout(0, HID_KEY_DOT, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_ADD) rc = usb_hid_kbd_press_timeout(HID_MOD_LSHIFT, HID_KEY_EQUAL, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_SUB) rc = usb_hid_kbd_press_timeout(0, HID_KEY_MINUS, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_MUL) rc = usb_hid_kbd_press_timeout(HID_MOD_LSHIFT, HID_KEY_8, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_DIV) rc = usb_hid_kbd_press_timeout(0, HID_KEY_SLASH, USB_TIMEOUT_TICKS);
        else if(ev.key == KEY_EXE) rc = usb_hid_kbd_press_timeout(0, HID_KEY_ENTER, USB_TIMEOUT_TICKS);
        
        if(rc == -3) show_timeout_error();
    }
}

/* ===== LaTeX Mode ===== */

/* Static storage to avoid stack overflow (struct is ~25KB) */
static math_expr2_t g_expr;

static bool run_latex_mode(void)
{
    math_expr2_t *expr = &g_expr;
    math2_init(expr);
    
    int cursor_timer = 0;
    const int CURSOR_FLASH_RATE = 8;  /* Toggle every N iterations */
    
    while(1) {
        /* Toggle cursor flash */
        cursor_timer++;
        if(cursor_timer >= CURSOR_FLASH_RATE) {
            cursor_timer = 0;
            g_cursor_visible = !g_cursor_visible;
        }
        
        draw_editor_ui(expr);
        
        /* Use pollevent for non-blocking check */
        key_event_t ev = pollevent();
        if(ev.type == KEYEV_NONE) {
            /* No key - small delay and continue for cursor flash */
            for(volatile int i = 0; i < 10000; i++);
            continue;
        }
        if(ev.type != KEYEV_DOWN) continue;
        
        /* Reset cursor to visible on any keypress */
        g_cursor_visible = true;
        cursor_timer = 0;
        
        bool shift = expr->shift_mode;
        bool alpha = expr->alpha_mode;
        
        /* Mode switch */
        if(ev.key == KEY_F1) {
            return true;  /* Switch to numpad */
        }
        
        /* Exit handling - EXIT never exits the app, only clears modes/exits containers */
        if(ev.key == KEY_EXIT) {
            /* If in nested slot, exit to parent */
            if(expr->cursor.sequence != expr->root) {
                cursor_exit(expr);
                math2_clear_modes(expr);
                g_alpha_lock = false;
            } else if(expr->shift_mode || expr->alpha_mode || g_alpha_lock) {
                math2_clear_modes(expr);
                g_alpha_lock = false;
            }
            /* Don't exit app - just clear modes */
            continue;
        }
        
        /* Menu */
        if(ev.key == KEY_MENU) {
            if(shift) {
                /* SHIFT+MENU: Open settings */
                math2_clear_modes(expr);
                g_alpha_lock = false;
                show_settings_menu();
            } else {
                /* Normal MENU: OS menu (can return to app) */
                usb_close();
                gint_osmenu();
                usb_open(interfaces, GINT_CALL_NULL);
                usb_open_wait();
            }
            continue;
        }
        
        /* SHIFT toggle */
        if(ev.key == KEY_SHIFT) {
            math2_toggle_shift(expr);
            continue;
        }
        
        /* ALPHA toggle / variable menu / alpha lock */
        if(ev.key == KEY_ALPHA) {
            if(shift) {
                /* SHIFT+ALPHA: Toggle alpha lock */
                g_alpha_lock = !g_alpha_lock;
                expr->alpha_mode = g_alpha_lock;
                expr->shift_mode = false;
            } else if(g_alpha_lock) {
                /* Alpha lock is on - turn it off */
                g_alpha_lock = false;
                expr->alpha_mode = false;
            } else if(alpha) {
                /* Already in single alpha mode - show variable menu */
                char var = show_variable_menu();
                if(var) {
                    char s[2] = {var, 0};
                    math2_insert_text(expr, TEXT_VARIABLE, s);
                }
                math2_clear_modes(expr);
            } else {
                /* Single alpha press - enable alpha mode */
                math2_toggle_alpha(expr);
            }
            continue;
        }
        
        /* Send LaTeX */
        if(ev.key == KEY_EXE) {
            /* If in nested slot, exit first */
            if(expr->cursor.sequence != expr->root) {
                cursor_exit(expr);
            } else {
                /* Send LaTeX */
                math2_to_latex(expr);
                if(strlen(expr->latex) > 0) {
                    /* Build string to send - optionally wrap in dollars */
                    static char send_buf[MAX_LATEX + 4];
                    if(g_settings.wrap_in_dollars) {
                        snprintf(send_buf, sizeof(send_buf), "$%s$", expr->latex);
                    } else {
                        strncpy(send_buf, expr->latex, sizeof(send_buf) - 1);
                        send_buf[sizeof(send_buf) - 1] = '\0';
                    }
                    
                    g_sending_latex = send_buf;  /* Set for progress display */
                    g_cancel_send = false;  /* Reset cancel flag */
                    show_sending(expr);
                    
                    int result = usb_hid_kbd_type_string_cancellable(
                        send_buf, 
                        update_progress,
                        check_cancel_send,
                        USB_TIMEOUT_TICKS
                    );
                    
                    g_sending_latex = NULL;
                    
                    if(result == -2) {
                        /* Cancelled - show message */
                        dclear(COL_BG);
                        drect(0, 0, SCREEN_W, HEADER_H - 1, COL_HEADER_BG);
                        dtext_opt(SCREEN_W / 2, HEADER_H / 2, COL_HEADER_TEXT, C_NONE,
                                  DTEXT_CENTER, DTEXT_MIDDLE, "Cancelled");
                        dtext_opt(SCREEN_W / 2, SCREEN_H / 2, C_RGB(25, 15, 0), C_NONE,
                                  DTEXT_CENTER, DTEXT_MIDDLE, "Sending cancelled");
                        dupdate();
                        for(volatile int i = 0; i < 50000; i++);
                    } else if(result == -3) {
                        /* Timeout - show error */
                        dclear(COL_BG);
                        drect(0, 0, SCREEN_W, HEADER_H - 1, C_RGB(20, 0, 0));
                        dtext_opt(SCREEN_W / 2, HEADER_H / 2, C_WHITE, C_NONE,
                                  DTEXT_CENTER, DTEXT_MIDDLE, "Timeout!");
                        dtext_opt(SCREEN_W / 2, SCREEN_H / 2 - 10, COL_TEXT, C_NONE,
                                  DTEXT_CENTER, DTEXT_MIDDLE, "USB connection lost or not responding");
                        dtext_opt(SCREEN_W / 2, SCREEN_H / 2 + 20, COL_TEXT_DIM, C_NONE,
                                  DTEXT_CENTER, DTEXT_MIDDLE, "Check cable and try again");
                        dupdate();
                        getkey();  /* Wait for any key */
                    } else {
                        /* Success - brief pause to show completion */
                        for(volatile int i = 0; i < 20000; i++);
                        
                        /* Clear if setting enabled */
                        if(g_settings.clear_on_send) {
                            math2_clear(expr);
                        }
                    }
                }
            }
            continue;
        }
        
        /* Navigation */
        if(ev.key == KEY_LEFT) {
            /* Try move left within sequence */
            if(cursor_left(expr)) {
                /* Moved left - now try to enter the node we just passed (from the right) */
                cursor_enter_left(expr);  /* This enters if it's a container, does nothing otherwise */
            } else {
                /* At start of sequence - exit left from container */
                cursor_exit_left(expr);
            }
            continue;
        }
        if(ev.key == KEY_RIGHT) {
            /* Try to enter the node ahead (from the left) */
            if(cursor_enter_right(expr)) {
                /* Entered a container */
            } else if(cursor_right(expr)) {
                /* Moved right past a non-container */
            } else {
                /* At end of sequence - exit right from container */
                cursor_exit_right(expr);
            }
            continue;
        }
        if(ev.key == KEY_UP) {
            cursor_prev_slot(expr);
            continue;
        }
        if(ev.key == KEY_DOWN) {
            cursor_next_slot(expr);
            continue;
        }
        
        /* Delete */
        if(ev.key == KEY_DEL) {
            math2_delete(expr);
            continue;
        }
        
        /* Clear all */
        if(ev.key == KEY_ACON) {
            if(shift) {
                /* SHIFT+AC: Turn off (exit app) */
                return false;
            }
            math2_clear(expr);
            continue;
        }
        
        /* Delete / Undo */
        if(ev.key == KEY_DEL) {
            /* TODO: SHIFT+DEL could be undo in the future */
            math2_delete(expr);
            continue;
        }
        
        /* ===== ALPHA mode - letters A-Z ===== */
        if(alpha || g_alpha_lock) {
            bool handled = true;
            
            /* Row: X,θ,T LOG LN SIN COS TAN */
            if(ev.key == KEY_XOT)       math2_insert_text(expr, TEXT_VARIABLE, "A");
            else if(ev.key == KEY_LOG)  math2_insert_text(expr, TEXT_VARIABLE, "B");
            else if(ev.key == KEY_LN)   math2_insert_text(expr, TEXT_VARIABLE, "C");
            else if(ev.key == KEY_SIN)  math2_insert_text(expr, TEXT_VARIABLE, "D");
            else if(ev.key == KEY_COS)  math2_insert_text(expr, TEXT_VARIABLE, "E");
            else if(ev.key == KEY_TAN)  math2_insert_text(expr, TEXT_VARIABLE, "F");
            
            /* Row: FRAC S<>D ( ) , STO */
            else if(ev.key == KEY_FRAC) math2_insert_text(expr, TEXT_VARIABLE, "G");
            else if(ev.key == KEY_FD)   math2_insert_text(expr, TEXT_VARIABLE, "H");
            else if(ev.key == KEY_LEFTP)  math2_insert_text(expr, TEXT_VARIABLE, "I");
            else if(ev.key == KEY_RIGHTP) math2_insert_text(expr, TEXT_VARIABLE, "J");
            else if(ev.key == KEY_COMMA)  math2_insert_text(expr, TEXT_VARIABLE, "K");
            else if(ev.key == KEY_ARROW)  math2_insert_text(expr, TEXT_VARIABLE, "L");
            
            /* Row: ^ 7 8 9 DEL */
            else if(ev.key == KEY_POWER) math2_insert_text(expr, TEXT_VARIABLE, "θ");
            else if(ev.key == KEY_7)     math2_insert_text(expr, TEXT_VARIABLE, "M");
            else if(ev.key == KEY_8)     math2_insert_text(expr, TEXT_VARIABLE, "N");
            else if(ev.key == KEY_9)     math2_insert_text(expr, TEXT_VARIABLE, "O");
            
            /* Row: 4 5 6 × ÷ */
            else if(ev.key == KEY_4)   math2_insert_text(expr, TEXT_VARIABLE, "P");
            else if(ev.key == KEY_5)   math2_insert_text(expr, TEXT_VARIABLE, "Q");
            else if(ev.key == KEY_6)   math2_insert_text(expr, TEXT_VARIABLE, "R");
            else if(ev.key == KEY_MUL) math2_insert_text(expr, TEXT_VARIABLE, "S");
            else if(ev.key == KEY_DIV) math2_insert_text(expr, TEXT_VARIABLE, "T");
            
            /* Row: 1 2 3 + - */
            else if(ev.key == KEY_1)   math2_insert_text(expr, TEXT_VARIABLE, "U");
            else if(ev.key == KEY_2)   math2_insert_text(expr, TEXT_VARIABLE, "V");
            else if(ev.key == KEY_3)   math2_insert_text(expr, TEXT_VARIABLE, "W");
            else if(ev.key == KEY_ADD) math2_insert_text(expr, TEXT_VARIABLE, "X");
            else if(ev.key == KEY_SUB) math2_insert_text(expr, TEXT_VARIABLE, "Y");
            
            /* Row: 0 . EXP (-) EXE */
            else if(ev.key == KEY_0)   math2_insert_text(expr, TEXT_VARIABLE, "Z");
            else if(ev.key == KEY_DOT) math2_insert_text(expr, TEXT_VARIABLE, " ");  /* Space */
            else if(ev.key == KEY_EXP) math2_insert_text(expr, TEXT_VARIABLE, "\"");
            
            else handled = false;
            
            if(handled) {
                /* Only clear modes if not in alpha lock */
                if(!g_alpha_lock) {
                    math2_clear_modes(expr);
                }
                continue;
            }
        }
        
        /* ===== SHIFT modifiers ===== */
        
        /* Numbers - no shift modifiers but check alpha first */
        if(!alpha) {
            if(ev.key == KEY_0) {
                if(shift) {
                    math2_insert_text(expr, TEXT_VARIABLE, "i");  /* Imaginary unit */
                    math2_clear_modes(expr);
                } else {
                    math2_insert_text(expr, TEXT_NUMBER, "0");
                }
                continue;
            }
            if(ev.key == KEY_1) { math2_insert_text(expr, TEXT_NUMBER, "1"); continue; }
            if(ev.key == KEY_2) { math2_insert_text(expr, TEXT_NUMBER, "2"); continue; }
            if(ev.key == KEY_3) { math2_insert_text(expr, TEXT_NUMBER, "3"); continue; }
            if(ev.key == KEY_4) { math2_insert_text(expr, TEXT_NUMBER, "4"); continue; }
            if(ev.key == KEY_5) { math2_insert_text(expr, TEXT_NUMBER, "5"); continue; }
            if(ev.key == KEY_6) { math2_insert_text(expr, TEXT_NUMBER, "6"); continue; }
            if(ev.key == KEY_7) { math2_insert_text(expr, TEXT_NUMBER, "7"); continue; }
            if(ev.key == KEY_8) { math2_insert_text(expr, TEXT_NUMBER, "8"); continue; }
            if(ev.key == KEY_9) { math2_insert_text(expr, TEXT_NUMBER, "9"); continue; }
        }
        
        /* Decimal point / = (SHIFT) / Space (ALPHA - handled above) */
        if(ev.key == KEY_DOT) {
            if(shift) {
                math2_insert_text(expr, TEXT_OPERATOR, "=");
                math2_clear_modes(expr);
            } else {
                math2_insert_text(expr, TEXT_NUMBER, ".");
            }
            continue;
        }
        
        /* EXP key: x10^x / pi (SHIFT) / " (ALPHA - handled above) */
        if(ev.key == KEY_EXP) {
            if(shift) {
                math2_insert_text(expr, TEXT_PI, "π");
                math2_clear_modes(expr);
            } else {
                /* x10^x - insert ×10^ then exponent */
                math2_insert_text(expr, TEXT_OPERATOR, "×");
                math2_insert_text(expr, TEXT_NUMBER, "10");
                math2_insert_exponent(expr);
            }
            continue;
        }
        
        /* Operators with SHIFT modifiers for brackets */
        if(ev.key == KEY_ADD) {
            if(shift) {
                math2_insert_text(expr, TEXT_VARIABLE, "[");
                math2_clear_modes(expr);
            } else {
                math2_insert_text(expr, TEXT_OPERATOR, "+");
            }
            continue;
        }
        if(ev.key == KEY_SUB) {
            if(shift) {
                math2_insert_text(expr, TEXT_VARIABLE, "]");
                math2_clear_modes(expr);
            } else {
                math2_insert_text(expr, TEXT_OPERATOR, "-");
            }
            continue;
        }
        if(ev.key == KEY_MUL) {
            if(shift) {
                math2_insert_text(expr, TEXT_VARIABLE, "{");
                math2_clear_modes(expr);
            } else {
                math2_insert_text(expr, TEXT_OPERATOR, "×");
            }
            continue;
        }
        if(ev.key == KEY_DIV) {
            if(shift) {
                math2_insert_text(expr, TEXT_VARIABLE, "}");
                math2_clear_modes(expr);
            } else {
                math2_insert_text(expr, TEXT_OPERATOR, "÷");
            }
            continue;
        }
        
        /* Fraction / Mixed Fraction (SHIFT) */
        if(ev.key == KEY_FRAC) {
            if(shift) {
                math2_insert_mixed_frac(expr);
                math2_clear_modes(expr);
            } else {
                math2_insert_fraction(expr);
            }
            continue;
        }
        
        /* Exponent / xth root (SHIFT) */
        if(ev.key == KEY_POWER) {
            if(shift) {
                math2_insert_xthroot(expr);
                math2_clear_modes(expr);
            } else {
                math2_insert_exponent(expr);
            }
            continue;
        }
        
        /* Square (x²) / Square root (SHIFT) */
        if(ev.key == KEY_SQUARE) {
            if(shift) {
                math2_insert_sqrt(expr);
                math2_clear_modes(expr);
            } else {
                math2_insert_exponent(expr);
                math2_insert_text(expr, TEXT_NUMBER, "2");
                cursor_exit(expr);
            }
            continue;
        }
        
        /* OPTN: Absolute value */
        if(ev.key == KEY_OPTN) {
            math2_insert_abs(expr);
            continue;
        }
        
        /* Parentheses: ( / Cube root (SHIFT) */
        if(ev.key == KEY_LEFTP) {
            if(shift) {
                math2_insert_nthroot(expr, 3);  /* Cube root */
                math2_clear_modes(expr);
            } else {
                math2_insert_paren(expr);
            }
            continue;
        }
        
        /* Right paren: ) / x^-1 (SHIFT) */
        if(ev.key == KEY_RIGHTP) {
            if(shift) {
                /* x^-1 */
                math2_insert_exponent(expr);
                math2_insert_text(expr, TEXT_OPERATOR, "-");
                math2_insert_text(expr, TEXT_NUMBER, "1");
                cursor_exit(expr);
                math2_clear_modes(expr);
            } else {
                /* Exit current container */
                if(expr->cursor.sequence != expr->root) {
                    cursor_exit(expr);
                }
            }
            continue;
        }
        
        /* Trig functions with arcsin/arccos/arctan (SHIFT) */
        if(ev.key == KEY_SIN) {
            if(shift) {
                math2_insert_function(expr, "arcsin");
                math2_clear_modes(expr);
            } else {
                math2_insert_function(expr, "sin");
            }
            continue;
        }
        if(ev.key == KEY_COS) {
            if(shift) {
                math2_insert_function(expr, "arccos");
                math2_clear_modes(expr);
            } else {
                math2_insert_function(expr, "cos");
            }
            continue;
        }
        if(ev.key == KEY_TAN) {
            if(shift) {
                math2_insert_function(expr, "arctan");
                math2_clear_modes(expr);
            } else {
                math2_insert_function(expr, "tan");
            }
            continue;
        }
        
        /* Log: log / 10^x (SHIFT) */
        if(ev.key == KEY_LOG) {
            if(shift) {
                /* 10^x */
                math2_insert_text(expr, TEXT_NUMBER, "10");
                math2_insert_exponent(expr);
                math2_clear_modes(expr);
            } else {
                math2_insert_function(expr, "log");
            }
            continue;
        }
        
        /* Ln: ln / e^x (SHIFT) */
        if(ev.key == KEY_LN) {
            if(shift) {
                /* e^x */
                math2_insert_text(expr, TEXT_VARIABLE, "e");
                math2_insert_exponent(expr);
                math2_clear_modes(expr);
            } else {
                math2_insert_function(expr, "ln");
            }
            continue;
        }
        
        /* Variable X */
        if(ev.key == KEY_XOT) {
            math2_insert_text(expr, TEXT_VARIABLE, "x");
            continue;
        }
        
        /* Subscript */
        if(ev.key == KEY_VARS) {
            math2_insert_subscript(expr);
            continue;
        }
        
        /* Comma */
        if(ev.key == KEY_COMMA) {
            math2_insert_text(expr, TEXT_VARIABLE, ",");
            continue;
        }
        
        /* STO (arrow) key */
        if(ev.key == KEY_ARROW) {
            math2_insert_text(expr, TEXT_OPERATOR, "→");
            continue;
        }
        
        /* S<->D (FD) key */
        if(ev.key == KEY_FD) {
            /* Currently no special function */
            continue;
        }
        
        /* NEG key (-) for negative sign */
        if(ev.key == KEY_NEG) {
            math2_insert_text(expr, TEXT_OPERATOR, "-");
            continue;
        }
    }
}

/* ===== Main Entry Point ===== */

int main(void)
{
    /* Initialize USB */
    usb_interface_t const *interfaces[] = { &usb_hid_kbd, NULL };
    usb_open(interfaces, GINT_CALL_NULL);
    
    /* Wait for USB connection (non-blocking check with user-friendly screen) */
    if(!wait_for_usb_connection()) {
        usb_close();
        return 0;
    }
    
    /* Show mode selection */
    current_mode = show_mode_selection();
    if(current_mode < 0) {
        usb_close();
        return 0;
    }
    
    /* Main mode loop */
    while(1) {
        if(current_mode == MODE_NUMPAD) {
            if(run_numpad_mode()) {
                current_mode = MODE_LATEX;
            } else {
                break;
            }
        } else {
            if(run_latex_mode()) {
                current_mode = MODE_NUMPAD;
            } else {
                break;
            }
        }
    }
    
    usb_close();
    return 0;
}
