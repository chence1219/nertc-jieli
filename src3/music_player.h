#ifndef SRC3_MUSIC_PLAYER_H
#define SRC3_MUSIC_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

void music_player_init(void);
void music_player_deinit(void);

int music_player_update_song_list(const char *json_text);
int music_player_maybe_play_first(void);
void music_player_clear_song_list(void);
int music_player_is_playing(void);

int music_player_play_index(int index);
int music_player_stop(void);
int music_player_next(void);
int music_player_previous(void);

int music_player_set_volume(int volume);
int music_player_get_volume(void);

#ifdef __cplusplus
}
#endif

#endif
