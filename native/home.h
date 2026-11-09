// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_HOME_H
#define MINPOCKET_HOME_H
#include "theme.h"
#define MP_HOME_SECONDARY_TITLE "Recently added"
/* Rendering actions shared by Home and game details. The runtime library
 * supplies immutable records; painters do not scan storage or start services. */
enum mp_home_view { MP_HOME, MP_HOME_LIBRARY, MP_HOME_GAME };
enum mp_home_action { MP_HOME_NONE, MP_HOME_OPEN_GAME, MP_HOME_OPEN_LIBRARY,
                      MP_HOME_OPEN_DISPLAY, MP_HOME_OPEN_POWER,
                      MP_HOME_OPEN_SEARCH, MP_HOME_OPEN_FAVORITES,
                      MP_HOME_OPEN_COLLECTIONS, MP_HOME_OPEN_DISCOVERY,
                      MP_HOME_OPEN_SETTINGS };
unsigned mp_home_focus_move(unsigned focus, int horizontal, int direction);
enum mp_home_action mp_home_activate(unsigned focus);
void mp_home_draw(const struct mp_font_surface *surface, const struct mp_theme *theme,
                  enum mp_home_view view, unsigned focus);
struct mp_game_art;
void mp_home_game_draw(const struct mp_font_surface *,const struct mp_theme *,
                      const char *title,const char *metadata,const char *description,const struct mp_game_art *);
#endif
