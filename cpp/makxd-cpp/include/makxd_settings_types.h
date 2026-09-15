#pragma once
#include <stdint.h>
/* Values are percentages, milliseconds and named channels, not wire packets.
 * Controller behaviors: right stick, left stick, LT, RT.
 * Translation: left stick/WASD, right stick/mouse, LT, RT. */
enum { MAKXD_SETTINGS_CONTROLLER=1, MAKXD_SETTINGS_TRANSLATION=2, MAKXD_SETTINGS_MOUSE=4 };
enum { MAKXD_TUNE_RIGHT_STICK=0, MAKXD_TUNE_LEFT_STICK=1, MAKXD_TUNE_LEFT_TRIGGER=2, MAKXD_TUNE_RIGHT_TRIGGER=3 };
enum { MAKXD_INTERPOLATION_OFF=0, MAKXD_INTERPOLATION_FIXED=1, MAKXD_INTERPOLATION_AUTO=2 };
typedef struct { uint8_t input_percent,output_percent; } makxd_curve_point_t;
typedef struct {
    uint8_t center_deadzone_percent,anti_deadzone_percent,change_deadband_percent;
    makxd_curve_point_t points[5];
} makxd_controller_curve_t;
typedef struct {
    uint8_t enabled,strength_present,strength_percent,curve_enabled,advanced_enabled;
    uint8_t inertia_percent,micro_percent,limit_percent,magnitude_variance_percent,angle_variance_percent;
    char name[25];
    makxd_controller_curve_t curves[4];
} makxd_controller_behavior_t;
typedef struct {
    uint8_t interpolation,buffer_ms,timing_variance_percent,curve_enabled;
    uint8_t legacy_strengths[3],selected_profile,profile_count;
    makxd_controller_behavior_t behaviors[4];
} makxd_controller_settings_t;
typedef struct { uint8_t enabled; uint16_t scale,timeout_ms; } makxd_controller_translation_t;
typedef struct {
    makxd_controller_settings_t controller;
    makxd_controller_translation_t translation[4];
    uint8_t mouse_spread_percent;
} makxd_device_settings_t;
typedef struct { uint8_t sections,kinds,save_state;uint32_t revision; } makxd_settings_info_t;
typedef struct { makxd_settings_info_t info;makxd_device_settings_t settings; } makxd_settings_snapshot_t;

typedef struct { makxd_controller_settings_t controller;makxd_controller_translation_t translation[4]; } makxd_controller_preset_t;
