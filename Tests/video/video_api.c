#include <grove/api/grove_api.h>

void video_c_start(void) {
    grove_document* doc = grove_current_document();
    grove_element* clip = grove_find_by_id(doc, "clip");
    grove_element* shot = grove_find_by_id(doc, "shot");
    uint32_t w = 1;
    uint32_t h = 1;
    int ok = !grove_video_is_fullscreen(doc, clip);
    ok = ok && grove_video_set_fullscreen(doc, clip, true) && grove_video_is_fullscreen(doc, clip);
    ok = ok && grove_video_set_fullscreen(doc, clip, false) && !grove_video_is_fullscreen(doc, clip);
    ok = ok && grove_video_snapshot(doc, NULL, NULL, 0, &w, &h) == 0 && w == 0 && h == 0;
    ok = ok && !grove_video_snapshot_to_canvas(doc, clip, NULL) && !grove_video_snapshot_to_canvas(doc, shot, clip);
    ok = ok && grove_media_set_volume(doc, clip, 0.8) && !grove_media_is_playing(doc, clip);
    grove_set_text_by_id("c", ok ? "c ok" : "c failed");
}
