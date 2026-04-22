/*
 * Text input scene.
 */

#include "../tagtinker_app.h"
#include <stdlib.h>

static uint8_t hex_nib(char c) {
    if(c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if(c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if(c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0xFF;
}

static size_t parse_hex_bytes(const char* str, uint8_t* out, size_t cap) {
    size_t n = 0;
    const char* p = str;
    while(n < cap) {
        while(*p == ' ') p++;
        if(!*p) break;
        uint8_t hi = hex_nib(*p++);
        if(hi == 0xFF) break;
        uint8_t lo = hex_nib(*p);
        if(lo == 0xFF) break;
        p++;
        out[n++] = (uint8_t)((hi << 4) | lo);
    }
    return n;
}

static void text_input_done_cb(void* ctx) {
    TagTinkerApp* app = ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, 0);
}

static void text_input_sanitize_name(char* value) {
    if(!value) return;

    for(char* p = value; *p; p++) {
        if(*p == '|' || *p == '\r' || *p == '\n') {
            *p = ' ';
        }
    }
}

void tagtinker_scene_text_input_on_enter(void* ctx) {
    TagTinkerApp* app = ctx;
    uint32_t mode = scene_manager_get_scene_state(app->scene_manager, TagTinkerSceneTextInput);
    bool rename_target = mode == TagTinkerTextInputRenameTarget;
    bool raw_cmd = mode == TagTinkerTextInputRawCommand;
    bool clear = (mode == TagTinkerTextInputNewText) || raw_cmd;

    if(rename_target) {
        if(app->selected_target >= 0 && app->selected_target < app->target_count) {
            strncpy(
                app->text_input_buf,
                app->targets[app->selected_target].name,
                sizeof(app->text_input_buf) - 1U);
            app->text_input_buf[sizeof(app->text_input_buf) - 1U] = '\0';
        } else {
            memset(app->text_input_buf, 0, sizeof(app->text_input_buf));
        }
    } else if(mode == TagTinkerTextInputNewText) {
        memset(app->text_input_buf, 0, sizeof(app->text_input_buf));
        scene_manager_set_scene_state(
            app->scene_manager, TagTinkerSceneTextInput, TagTinkerTextInputKeepText);
    } else if(raw_cmd) {
        memset(app->text_input_buf, 0, sizeof(app->text_input_buf));
    }

    const char* header = rename_target ? "Target name:" :
                         raw_cmd       ? "Raw cmd (hex):" :
                                         "Text to display:";

    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, header);
    text_input_set_result_callback(
        app->text_input,
        text_input_done_cb,
        app,
        app->text_input_buf,
        sizeof(app->text_input_buf),
        clear && !rename_target);

    view_dispatcher_switch_to_view(app->view_dispatcher, TagTinkerViewTextInput);
}

bool tagtinker_scene_text_input_on_event(void* ctx, SceneManagerEvent event) {
    TagTinkerApp* app = ctx;
    if(event.type != SceneManagerEventTypeCustom) return false;

    uint32_t mode = scene_manager_get_scene_state(app->scene_manager, TagTinkerSceneTextInput);

    if(mode == TagTinkerTextInputRawCommand) {
        if(app->selected_target < 0 || app->selected_target >= app->target_count) {
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, TagTinkerSceneTargetActions);
            return true;
        }

        uint8_t raw[20];
        size_t count = parse_hex_bytes(app->text_input_buf, raw, sizeof(raw));
        if(count == 0) {
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, TagTinkerSceneTargetActions);
            return true;
        }

        TagTinkerTarget* target = &app->targets[app->selected_target];

        app->frame_seq_count = 2;
        app->frame_sequence = malloc(sizeof(uint8_t*) * 2);
        app->frame_lengths  = malloc(sizeof(size_t) * 2);
        app->frame_repeats  = malloc(sizeof(uint16_t) * 2);

        app->frame_sequence[0] = malloc(TAGTINKER_MAX_FRAME_SIZE);
        app->frame_lengths[0]  = tagtinker_make_ping_frame(app->frame_sequence[0], target->plid);
        app->frame_repeats[0]  = 500;

        app->frame_sequence[1] = malloc(TAGTINKER_MAX_FRAME_SIZE);
        app->frame_lengths[1]  = tagtinker_make_addressed_frame(
            app->frame_sequence[1], target->plid, raw, count);
        app->frame_repeats[1]  = 100;

        memcpy(app->frame_buf, app->frame_sequence[0], app->frame_lengths[0]);
        app->frame_len = app->frame_lengths[0];

        app->image_tx_job.mode = TagTinkerTxModeNone;
        app->tx_spam = false;
        scene_manager_next_scene(app->scene_manager, TagTinkerSceneTransmit);
        return true;
    }

    if(mode == TagTinkerTextInputRenameTarget) {
        if(app->selected_target >= 0 && app->selected_target < app->target_count) {
            TagTinkerTarget* target = &app->targets[app->selected_target];
            text_input_sanitize_name(app->text_input_buf);
            if(strlen(app->text_input_buf) == 0U) {
                tagtinker_target_set_default_name(target);
            } else {
                strncpy(target->name, app->text_input_buf, TAGTINKER_TARGET_NAME_LEN);
                target->name[TAGTINKER_TARGET_NAME_LEN] = '\0';
            }
            tagtinker_targets_save(app);
        }

        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, TagTinkerSceneTargetActions);
        return true;
    }

    if(strlen(app->text_input_buf) == 0) {
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, TagTinkerSceneTargetActions);
        return true;
    }

    /* Configure settings (Add Preset flow) */
    scene_manager_next_scene(app->scene_manager, TagTinkerSceneSizePicker);
    return true;
}

void tagtinker_scene_text_input_on_exit(void* ctx) {
    TagTinkerApp* app = ctx;
    text_input_reset(app->text_input);
}
