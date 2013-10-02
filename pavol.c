#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <pulse/pulseaudio.h>

#define UNUSED __attribute__((unused))

#define DEFAULT_RELATIVE_VOLUME 4

struct pulseaudio_t {
    pa_context *cxt;
    pa_mainloop *mainloop;
    pa_cvolume cvolume;
    pa_volume_t volume; // average
    int muted; // 0 or 1
    char *default_sink;
};

void pulse_server_info_cb(pa_context UNUSED *c, const pa_server_info *i, void *raw) {
    struct pulseaudio_t *pulse = (struct pulseaudio_t *)raw;
    pulse->default_sink = (char*)malloc(strlen(i->default_sink_name)+1);
    strcpy(pulse->default_sink, i->default_sink_name);
}

void pulse_connect_state_cb(pa_context *cxt, void *raw) {
    enum pa_context_state *state = (enum pa_context_state *)raw;
    *state = pa_context_get_state(cxt);
}

void pulse_async_wait(struct pulseaudio_t *pulse, pa_operation *op) {
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
        pa_mainloop_iterate(pulse->mainloop, 1, NULL);
}

void pulse_get_default_sink_volume_cb(pa_context UNUSED *c, const pa_sink_info *i, int eol, void *raw) {
    if (eol)
        return;

    struct pulseaudio_t *pulse = (struct pulseaudio_t*)raw;

    pulse->muted = i->mute;
    pulse->cvolume = i->volume;
    pulse->volume = pa_cvolume_avg(&(i->volume));
}

void pulse_get_default_sink_volume(struct pulseaudio_t *pulse) {
    pa_operation *op;

    op = pa_context_get_sink_info_by_name(pulse->cxt, pulse->default_sink, pulse_get_default_sink_volume_cb, pulse);
    pulse_async_wait(pulse, op);
    pa_operation_unref(op);
}

void pulse_set_default_sink_volume(struct pulseaudio_t *pulse, pa_volume_t volume) {
    pa_operation *op;

    pa_cvolume_set(&(pulse->cvolume), 2, volume);

    op = pa_context_set_sink_volume_by_name(pulse->cxt, pulse->default_sink, &(pulse->cvolume), NULL, pulse);
    pulse_async_wait(pulse, op);
    pa_operation_unref(op);
}

void pulse_set_default_sink_mute(struct pulseaudio_t *pulse, int muted) {
    pa_operation *op;

    pulse->muted = muted;

    op = pa_context_set_sink_mute_by_name(pulse->cxt, pulse->default_sink, pulse->muted, NULL, pulse);
    pulse_async_wait(pulse, op);
    pa_operation_unref(op);
}

void pulse_get_default_sink(struct pulseaudio_t *pulse) {
    pa_operation *op;

    free(pulse->default_sink);

    op = pa_context_get_server_info(pulse->cxt, pulse_server_info_cb, pulse);
    pulse_async_wait(pulse, op);
    pa_operation_unref(op);
}

int pulse_init(struct pulseaudio_t *pulse) {
    enum pa_context_state state = PA_CONTEXT_CONNECTING;

    pulse->mainloop = pa_mainloop_new();
    pulse->cxt = pa_context_new(pa_mainloop_get_api(pulse->mainloop), "pavol");
    pulse->default_sink = NULL;
    pulse->muted = 0;
    pulse->volume = 0;

    pa_context_set_state_callback(pulse->cxt, pulse_connect_state_cb, &state);
    pa_context_connect(pulse->cxt, NULL, PA_CONTEXT_NOFLAGS, NULL);
    while (state != PA_CONTEXT_READY && state != PA_CONTEXT_FAILED)
        pa_mainloop_iterate(pulse->mainloop, 1, NULL);

    if (state != PA_CONTEXT_READY) {
        fprintf(stderr, "failed to connect to pulse daemon: %s\n",
                pa_strerror(pa_context_errno(pulse->cxt)));
        return 1;
    }

    return 0;
}

void pulse_deinit(struct pulseaudio_t *pulse) {
    enum pa_context_state state = PA_CONTEXT_CONNECTING;
    pa_context_set_state_callback(pulse->cxt, pulse_connect_state_cb, &state);

    pa_context_disconnect(pulse->cxt);
    pa_mainloop_free(pulse->mainloop);
    free(pulse->default_sink);
}

/* *relative will determine the direction. The return value is always positive. */
/* if *relative is zero, then the number is absolute. */
double parse_number(const char *numstr, int *relative, int *percentage) {
    *relative = 0;

    /* If it contains a + or -, it's relative */
    if (NULL != strstr(numstr, "+")) {
        *relative = +1;
    } else if (NULL != strstr(numstr, "-")) {
        *relative = -1;
    }

    /* If it contains a radix, it's a percentage */
    if (NULL != strstr(numstr, ".")) {
        *percentage = 1;
    }

    return abs(strtod(numstr, NULL));
}

int main(int argc, char * argv[]) {
    struct pulseaudio_t pulse = {0};
    int relative = 0, percentage = 0;
    double max_vol = 2.*PA_VOLUME_NORM;
    double num = 0;

    int want_percentage = 0;

    pulse_init(&pulse);

    pulse_get_default_sink(&pulse);
    pulse_get_default_sink_volume(&pulse);

    /* Parse command line */
    /* Can be a string of "m*" "u*" "t*" "+/-number" "f" */
    for (int a = 1; a < argc; a ++) {
        if (strncasecmp("h", argv[a], 1) == 0 || strcasecmp("-h", argv[a]) == 0 || strcasecmp("--help", argv[a]) == 0) {
            /* print usage */
            printf("usage: %s ARG ...\n", argv[0]);
            printf("\n");
            printf("ARG can be one of:\n");
            printf("  MUTING\n");
            printf("      m[ute]      : enable mute\n");
            printf("      u[nmute]    : disable mute\n");
            printf("      t[oggle]    : toggle mute\n");
            printf("\n");
            printf("  VOLUME\n");
            printf("      +           : increase the volume by %d%%\n", DEFAULT_RELATIVE_VOLUME);
            printf("      -           : decrease the volume by %d%%\n", DEFAULT_RELATIVE_VOLUME);
            printf("      NUM         : set the volume to an amount\n");
            printf("\n");
            printf("  OTHER\n");
            printf("       f          : double the maximum-settable volume\n");
            printf("       p          : output the mute status and volume as a percent\n");
            printf("\n");
            printf("NUM can be interpreted in different ways:\n");
            printf("    if NUM contains a radix (decimal point, '.'), it is interpreted as a percentage; otherwise it is an absolute value.\n");
            printf("    if NUM contains a sign ('+' or '-'), then it is interpreted as a relative value.\n");
            printf("\n");
            printf("The output is four space separated integers:\n");
            printf("    <muted> <absolute volume> <100%% volume> <maximum settable volume>\n");
            printf("If ARG 'p' is given, then the output is two integers:\n");
            printf("    <muted> <volume as a percent>\n");
            printf("\n");
            printf("EXAMPLES\n");
            printf("    set the volume to 50%%\n");
            printf("        %s 50.\n", argv[0]);
            printf("    increase the volume by 20%%\n");
            printf("        %s +20.\n", argv[0]);
            printf("    toggle mute\n");
            printf("        %s t\n", argv[0]);
            printf("    unmute and decrease volume to 10%%\n");
            printf("        %s u 10.\n", argv[0]);
            printf("    print out the mute status and current volume as a percent\n");
            printf("        %s p\n", argv[0]);
            continue;
        } if (strncasecmp("m", argv[a], 1) == 0) {
            /* Mute on */
            pulse_set_default_sink_mute(&pulse, 1);
            continue;
        } else if (strncasecmp("u", argv[a], 1) == 0) {
            /* Mute off */
            pulse_set_default_sink_mute(&pulse, 0);
            continue;
        } else if (strncasecmp("t", argv[a], 1) == 0) {
            /* Mute toggle */
            pulse_set_default_sink_mute(&pulse, !pulse.muted);
            continue;

        } else if (strncasecmp("f", argv[a], 1) == 0) {
            /* Enable loud volumes (up to 4 times instead of 2) */
            max_vol *= 2;
            continue;

        } else if (strncasecmp("p", argv[a], 1) == 0) {
            /* Enable loud volumes (up to 4 times instead of 2) */
            want_percentage = 1;
            continue;

        } else if (strcmp("+", argv[a]) == 0) {
            /* Default relative volume increase */
            max_vol *= 2;
            relative = 1;
            num = DEFAULT_RELATIVE_VOLUME;
            percentage = 1;
        } else if (strcmp("-", argv[a]) == 0) {
            /* Default relative volume decrease */
            max_vol *= 2;
            relative = -1;
            num = DEFAULT_RELATIVE_VOLUME;
            percentage = 1;
        } else {
            /* Interpret as volume change */
            num = parse_number(argv[a], &relative, &percentage);
        }
        double v = (double)pulse.volume;
        if (relative) {
            if (percentage) {
                v += relative * num/100. * (double)PA_VOLUME_NORM;
            } else {
                v += relative * num;
            }
        } else {
            if (percentage) {
                v = num/100.*(double)PA_VOLUME_NORM;
            } else {
                v = num;
            }
        }
        if (v < 0) {
            v = 0;
        } else if (v > max_vol) {
            v = max_vol;
        }
        pulse_set_default_sink_volume(&pulse, (pa_volume_t)v);
    }

    pulse_get_default_sink_volume(&pulse);
    if (want_percentage) {
        printf("%d %d\n", pulse.muted, pulse.volume * 100 / PA_VOLUME_NORM);
    } else {
        printf("%d %d %d %d\n", pulse.muted, pulse.volume, PA_VOLUME_NORM, (int)max_vol);
    }

    pulse_deinit(&pulse);
    return 0;
}
