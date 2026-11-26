#ifndef SETTINGS_H
#define SETTINGS_H

#include "storage/storage.h"
#include <stddef.h>

typedef struct {
    double gravity_force;
    size_t num_planets;
    double launch_interval;
    double grid_speed;
    size_t grid_spacing;
    size_t trail_duration;
    size_t asteroid_speed;
    size_t debri_count;
} Settings;

static Settings DEFAULT_SETTINGS = {
    .gravity_force = 50.0,
    .num_planets = 3,
    .launch_interval = 1.0,
    .grid_speed = 16,
    .grid_spacing = 16,
    .trail_duration = 1,
    .asteroid_speed = 2,
    .debri_count = 10,
};

Settings settings;

// clamp a loaded value to the same limits the settings UI enforces
static size_t settings_clamp_size(size_t value, size_t minv, size_t maxv) {
    if(value < minv) return minv;
    if(value > maxv) return maxv;
    return value;
}

static double settings_clamp_double(double value, double minv, double maxv) {
    if(value < minv) return minv;
    if(value > maxv) return maxv;
    return value;
}

static void settings_load() {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, "/ext/spground.cfg", FSAM_READ, FSOM_OPEN_EXISTING)) {
        int gravity_force = 0;
        int launch_interval = 0;
        int grid_speed = 0;

        char buf[256] = {0};
        storage_file_read(file, buf, sizeof(buf) - 1);
        int parsed = sscanf(
            buf,
            "%d %d %d %d %d %d %d %d",
            &gravity_force,
            &settings.num_planets,
            &launch_interval,
            &grid_speed,
            &settings.grid_spacing,
            &settings.trail_duration,
            &settings.asteroid_speed,
            &settings.debri_count);

        // fields missing from a short/truncated file keep their defaults
        if(parsed >= 1) {
            settings.gravity_force = settings_clamp_double(
                (double)gravity_force / (double)1000.0, (double)1.0, (double)100.0);
        }
        if(parsed >= 3) {
            settings.launch_interval = settings_clamp_double(
                (double)launch_interval / (double)1000.0, (double)0.10, (double)5.0);
        }
        if(parsed >= 4) {
            settings.grid_speed = settings_clamp_double(
                (double)grid_speed / (double)1000.0, (double)1.0, (double)32.0);
        }

        settings.num_planets = settings_clamp_size(settings.num_planets, 1, 8);
        settings.grid_spacing = settings_clamp_size(settings.grid_spacing, 1, 32);
        settings.trail_duration = settings_clamp_size(settings.trail_duration, 0, 5);
        settings.asteroid_speed = settings_clamp_size(settings.asteroid_speed, 1, 5);
        settings.debri_count = settings_clamp_size(settings.debri_count, 0, 50);

        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

void settings_clear() {
    settings.gravity_force = DEFAULT_SETTINGS.gravity_force;
    settings.num_planets = DEFAULT_SETTINGS.num_planets;
    settings.launch_interval = DEFAULT_SETTINGS.launch_interval;
    settings.grid_speed = DEFAULT_SETTINGS.grid_speed;
    settings.grid_spacing = DEFAULT_SETTINGS.grid_spacing;
    settings.trail_duration = DEFAULT_SETTINGS.trail_duration;
    settings.asteroid_speed = DEFAULT_SETTINGS.asteroid_speed;
    settings.debri_count = DEFAULT_SETTINGS.debri_count;
}

void settings_init() {
    settings_clear();
    settings_load();
}

void settings_save() {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, "/ext/spground.cfg", FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[256];
        int n = snprintf(
            buf,
            sizeof(buf),
            "%d %d %d %d %d %d %d %d",
            (int)(settings.gravity_force * (int)1000),
            settings.num_planets,
            (int)(settings.launch_interval * (int)1000),
            (int)(settings.grid_speed * (int)1000),
            settings.grid_spacing,
            settings.trail_duration,
            settings.asteroid_speed,
            settings.debri_count);
        storage_file_write(file, buf, n);
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

#endif
