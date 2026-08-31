// mediakeys_mac.m — macOS backend for mediakeys.h.
// Uses MPRemoteCommandCenter + MPNowPlayingInfoCenter (the sanctioned route
// since macOS 10.12.2): the system routes the keyboard transport keys / AirPods
// taps / Now Playing widget to whichever app registered handlers and reports a
// playback state — no Input Monitoring permission, and it coexists with other
// media apps. Compile with -fobjc-arc.
#ifdef __APPLE__

#import <MediaPlayer/MediaPlayer.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include "mediakeys.h"
#include "art.h"

static _Atomic int g_action = MK_NONE;
static NSMutableDictionary *g_info;   // the dictionary we keep re-publishing
static int    g_state = MK_STOPPED;
static double g_pushed_pos = -1e9;

static NSString *ns(const char *s) { return s && *s ? @(s) : @""; }

void mediakeys_start(void *native_window) {
    (void)native_window;
    MPRemoteCommandCenter *c = [MPRemoteCommandCenter sharedCommandCenter];

    [c.togglePlayPauseCommand addTargetWithHandler:^(MPRemoteCommandEvent *e) {
        (void)e; atomic_store(&g_action, MK_PLAYPAUSE); return MPRemoteCommandHandlerStatusSuccess; }];
    [c.playCommand addTargetWithHandler:^(MPRemoteCommandEvent *e) {
        (void)e; atomic_store(&g_action, MK_PLAY); return MPRemoteCommandHandlerStatusSuccess; }];
    [c.pauseCommand addTargetWithHandler:^(MPRemoteCommandEvent *e) {
        (void)e; atomic_store(&g_action, MK_PAUSE); return MPRemoteCommandHandlerStatusSuccess; }];
    [c.stopCommand addTargetWithHandler:^(MPRemoteCommandEvent *e) {
        (void)e; atomic_store(&g_action, MK_STOP); return MPRemoteCommandHandlerStatusSuccess; }];
    [c.previousTrackCommand addTargetWithHandler:^(MPRemoteCommandEvent *e) {
        (void)e; atomic_store(&g_action, MK_PREV); return MPRemoteCommandHandlerStatusSuccess; }];
    [c.nextTrackCommand addTargetWithHandler:^(MPRemoteCommandEvent *e) {
        (void)e; atomic_store(&g_action, MK_NEXT); return MPRemoteCommandHandlerStatusSuccess; }];

    g_info = [NSMutableDictionary dictionary];
    [MPNowPlayingInfoCenter defaultCenter].playbackState = MPNowPlayingPlaybackStateStopped;
}

void mediakeys_shutdown(void) {
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;
    [MPNowPlayingInfoCenter defaultCenter].playbackState = MPNowPlayingPlaybackStateStopped;
}

int mediakeys_poll(void) {
    return atomic_exchange(&g_action, MK_NONE);
}

void mediakeys_now_playing(const char *title, const char *artist, const char *album,
                           const char *audio_path) {
    if (!g_info) return;
    [g_info removeAllObjects];
    if (!title) {                       // nothing loaded → drop the whole entry
        [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;
        return;
    }
    g_info[MPMediaItemPropertyTitle]      = ns(title);
    g_info[MPMediaItemPropertyArtist]     = ns(artist);
    g_info[MPMediaItemPropertyAlbumTitle] = ns(album);

    unsigned char *blob = NULL; int size = 0;
    if (audio_path && art_load_encoded(audio_path, &blob, &size)) {
        NSImage *img = [[NSImage alloc] initWithData:[NSData dataWithBytes:blob length:(NSUInteger)size]];
        free(blob);
        if (img) g_info[MPMediaItemPropertyArtwork] =
            [[MPMediaItemArtwork alloc] initWithBoundsSize:img.size
                                            requestHandler:^NSImage *(CGSize sz) { (void)sz; return img; }];
    }
    g_info[MPNowPlayingInfoPropertyPlaybackRate] = @(g_state == MK_PLAYING ? 1.0 : 0.0);
    g_pushed_pos = -1e9;
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = g_info;
}

void mediakeys_set_state(int state) {
    if (state == g_state) return;
    g_state = state;
    MPNowPlayingInfoCenter *np = [MPNowPlayingInfoCenter defaultCenter];
    np.playbackState = state == MK_PLAYING ? MPNowPlayingPlaybackStatePlaying
                     : state == MK_PAUSED  ? MPNowPlayingPlaybackStatePaused
                                           : MPNowPlayingPlaybackStateStopped;
    if (g_info.count) {
        g_info[MPNowPlayingInfoPropertyPlaybackRate] = @(state == MK_PLAYING ? 1.0 : 0.0);
        np.nowPlayingInfo = g_info;
    }
}

void mediakeys_set_timeline(double position, double duration) {
    // Republishing the dictionary is not free, so only when the scrubber moved.
    if (duration <= 0 || !g_info.count) return;
    if (fabs(position - g_pushed_pos) < 1.0) return;
    g_pushed_pos = position;
    g_info[MPMediaItemPropertyPlaybackDuration] = @(duration);
    g_info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(position);
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = g_info;
}

#endif // __APPLE__
