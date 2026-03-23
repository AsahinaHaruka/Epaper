#ifndef ___SCREEN_INK_H__
#define ___SCREEN_INK_H__

int si_calendar_status();
void si_calendar();

int si_screen_status();
void si_screen();
void si_screen_display_only(); // Render only (assumes si_calendar() already
                               // called)

void si_warning(const char *str);

// Partial clock-only refresh (no WiFi needed)
void updateClockOnly();

#endif