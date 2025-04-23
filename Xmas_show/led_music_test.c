#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <time.h>
#include <alsa/asoundlib.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <gpiod.h>

#include <sys/mman.h>

#define GPIO_BASE_ADDR 0x20200000  // For Pi 1
#define GPIO_LEN       0xB4        // Enough to cover all GPIO registers

volatile uint32_t *gpio = NULL;

#define GPFSEL0 (gpio + 0x00 / 4)
#define GPFSEL1 (gpio + 0x04 / 4)
#define GPFSEL2 (gpio + 0x08 / 4)
#define GPSET0  (gpio + 0x1C / 4)
#define GPCLR0  (gpio + 0x28 / 4)


#define AUDIO_PERIOD_FRAMES 441
#define AUDIO_THREAD_PERIOD_MS 30
#define WAV_HEADER_SIZE 44
#define FILENAME "top_gun1.wav"
#define LED_LOG_FILE "led_log.csv"
#define AUDIO_LOG_FILE "audio_log.csv"
#define MAX_RUNS 60000

#define CONSUMER "led_seq"
const unsigned int led_lines[8] = {22, 5, 6, 26, 23, 24, 25, 16};

typedef struct {
    int duration_ms;
    uint8_t pattern;
} Pattern;

Pattern *patterns = NULL;
int pattern_count = 0;

snd_pcm_t *pcm;
int16_t *audio_data = NULL;
size_t audio_frames = 0;
long runtimes_us[MAX_RUNS];
long jitter_us[MAX_RUNS];
long wake_intervals_us[MAX_RUNS];
size_t runtime_index = 0;
int underrun_count = 0;

long time_diff_us(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000000L + (end.tv_nsec - start.tv_nsec) / 1000L;
}

void *audio_thread_fn(void *arg) {
    size_t frame_idx = 0;
    struct timespec next_time;
    clock_gettime(CLOCK_MONOTONIC, &next_time);

    static struct timespec prev_wake_time = {0};

    while (frame_idx + AUDIO_PERIOD_FRAMES * 3 <= audio_frames && runtime_index < MAX_RUNS) {
        // Wait for the next release time
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_time, NULL);

        struct timespec start_time, end_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        long wake_us = 0;
        if (prev_wake_time.tv_sec != 0)
            wake_us = time_diff_us(prev_wake_time, start_time);
        prev_wake_time = start_time;

        long total_runtime_us = 0;
        for (int i = 0; i < 3; ++i) {
            struct timespec call_start, call_end;
            clock_gettime(CLOCK_MONOTONIC, &call_start);

            snd_pcm_sframes_t written = snd_pcm_writei(pcm, &audio_data[frame_idx * 2], AUDIO_PERIOD_FRAMES);
            if (written < 0) {
                underrun_count++;
                if (underrun_count <= 10 || underrun_count % 50 == 0) {
                    fprintf(stderr, "Underrun #%d: %s\n", underrun_count, snd_strerror(written));
                }
                snd_pcm_prepare(pcm);
                continue;
            }

            clock_gettime(CLOCK_MONOTONIC, &call_end);
            total_runtime_us += time_diff_us(call_start, call_end);
            frame_idx += AUDIO_PERIOD_FRAMES;
        }

        clock_gettime(CLOCK_MONOTONIC, &end_time);
        long jitter = time_diff_us(next_time, start_time);
        if (jitter < 0)
            fprintf(stderr, "⚠️ Deadline miss at cycle %zu by %ld us\n", runtime_index, -jitter);

        runtimes_us[runtime_index] = total_runtime_us;
        wake_intervals_us[runtime_index] = wake_us;
        jitter_us[runtime_index] = jitter;

        if (runtime_index % 100 == 0) {
            snd_pcm_sframes_t delay;
            if (snd_pcm_delay(pcm, &delay) == 0) {
                fprintf(stderr, "[Cycle %zu] ALSA delay: %ld frames (%0.2f ms)\n",
                        runtime_index, delay, (delay * 1000.0) / 44100.0);
            }
        }

        runtime_index++;

        // Advance next_time by period
        next_time.tv_nsec += AUDIO_THREAD_PERIOD_MS * 1000000;
        while (next_time.tv_nsec >= 1000000000) {
            next_time.tv_sec++;
            next_time.tv_nsec -= 1000000000;
        }
    }

    return NULL;
}


void *led_thread_fn(void *arg) {
    struct sched_param sp = {.sched_priority = 80};
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);

    struct gpiod_chip *chip = gpiod_chip_open_by_number(4);
    struct gpiod_line_bulk bulk;
    gpiod_chip_get_lines(chip, (unsigned int *) led_lines, 8, &bulk);
    gpiod_line_request_bulk_output(&bulk, CONSUMER, NULL);

    FILE *log = fopen(LED_LOG_FILE, "w");
    fprintf(log, "tick,time_us,write_time_us\n");

    int current_index = 0, tick_count = 0, ticks_for_current = 0;
    struct timespec start, next_time;
    clock_gettime(CLOCK_MONOTONIC, &start);
    next_time = start;

    int tick = 0;
    while (current_index < pattern_count) {
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_time, NULL);

        struct timespec tick_start, write_start, write_end;
        clock_gettime(CLOCK_MONOTONIC, &tick_start);

        if (tick_count == 0) {
            int values[8];
            for (int j = 0; j < 8; ++j)
                values[j] = (patterns[current_index].pattern >> (7 - j)) & 1;

            uint32_t set_mask = 0, clr_mask = 0;
            for (int j = 0; j < 8; ++j) {
                int pin = led_lines[j];
                if (values[j]) set_mask |= (1 << pin);
                else clr_mask |= (1 << pin);
            }

            clock_gettime(CLOCK_MONOTONIC, &write_start);
            *GPSET0 = set_mask;
            *GPCLR0 = clr_mask;
            clock_gettime(CLOCK_MONOTONIC, &write_end);

            int duration = patterns[current_index].duration_ms;
            if (duration < 70) duration = 70;
            duration = ((duration + 5) / 10) * 10;

            ticks_for_current = duration / 10;
            tick_count = ticks_for_current;

            fprintf(log, "%d,%ld,%ld\n", tick, time_diff_us(start, tick_start), time_diff_us(write_start, write_end));
        }

        tick_count--;
        if (tick_count == 0) current_index++;
        tick++;

        next_time.tv_nsec += 10 * 1000000;
        while (next_time.tv_nsec >= 1000000000) {
            next_time.tv_sec++;
            next_time.tv_nsec -= 1000000000;
        }
    }

    fclose(log);
    gpiod_line_release_bulk(&bulk);
    gpiod_chip_close(chip);
    return NULL;
}

void setup_alsa(unsigned int sample_rate, unsigned int channels) {
    snd_pcm_hw_params_t *params;
    snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    snd_pcm_hw_params_malloc(&params);
    snd_pcm_hw_params_any(pcm, params);
    snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, params, channels);
    snd_pcm_hw_params_set_rate(pcm, params, sample_rate, 0);

    snd_pcm_uframes_t buffer_size = AUDIO_PERIOD_FRAMES * 12;
    snd_pcm_uframes_t period_size = AUDIO_PERIOD_FRAMES;
    snd_pcm_hw_params_set_period_size_near(pcm, params, &period_size, 0);
    snd_pcm_hw_params_set_buffer_size_near(pcm, params, &buffer_size);
    snd_pcm_hw_params(pcm, params);
    snd_pcm_hw_params_free(params);
    snd_pcm_prepare(pcm);
}

void load_wav(const char *filename, uint32_t *sample_rate, uint16_t *channels) {
    FILE *f = fopen(filename, "rb");
    if (!f) { perror("fopen"); exit(1); }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, WAV_HEADER_SIZE, SEEK_SET);
    long data_size = size - WAV_HEADER_SIZE;

    fseek(f, 24, SEEK_SET); fread(sample_rate, sizeof(uint32_t), 1, f);
    fseek(f, 22, SEEK_SET); fread(channels, sizeof(uint16_t), 1, f);

    audio_frames = data_size / (*channels * sizeof(int16_t));
    audio_data = malloc(data_size);
    fread(audio_data, 1, data_size, f);
    fclose(f);
}

void load_patterns(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) { perror("pattern file"); exit(1); }

    char line[64];
    int count = 0;
    patterns = malloc(sizeof(Pattern) * 1024);

    while (fgets(line, sizeof(line), f)) {
        int dur;
        char bits[10];
        if (sscanf(line, "%d %9s", &dur, bits) == 2) {
            if (dur < 70) dur = 70;
            dur = ((dur + 5) / 10) * 10;

            uint8_t p = 0;
            for (int i = 0, j = 0; i < 8 && bits[j]; ++j) {
                if (bits[j] == '.') continue;
                p = (p << 1) | (bits[j] == '1' ? 1 : 0);
                ++i;
            }
            patterns[count++] = (Pattern){.duration_ms = dur, .pattern = p};
        }
    }

    pattern_count = count;
    fclose(f);
}

void save_runtime_log(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) { perror("log fopen"); return; }

    fprintf(f, "index,runtime_us,wake_interval_us,jitter_us\n");
    long sum = 0, max = 0;
    for (size_t i = 0; i < runtime_index; ++i) {
        fprintf(f, "%zu,%ld,%ld,%ld\n", i, runtimes_us[i], wake_intervals_us[i], jitter_us[i]);
        sum += runtimes_us[i];
        if (runtimes_us[i] > max) max = runtimes_us[i];
    }

    double avg = (double)sum / runtime_index;
    fprintf(f, "\nAverage (us),%lf\nMax (us),%ld\n", avg, max);
    fprintf(f, "Total underruns,%d\n", underrun_count);
    fclose(f);
}

int main() {

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); exit(1); }

    gpio = (volatile uint32_t *) mmap(NULL, GPIO_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPIO_BASE_ADDR);
    if (gpio == MAP_FAILED) { perror("mmap"); exit(1); }

    // Set all 8 lines to output (safe, bit-by-bit for clarity)
    for (int i = 0; i < 8; ++i) {
        int gpio_num = led_lines[i];
        volatile uint32_t *fsel = gpio + (gpio_num / 10);
        int shift = (gpio_num % 10) * 3;
        *fsel = (*fsel & ~(7 << shift)) | (1 << shift);  // set to 001 (output)
    }





    pthread_t audio_thread, led_thread;

    struct sched_param audio_param = {.sched_priority = 75};  // Lower priority
    struct sched_param led_param   = {.sched_priority = 80};  // Higher priority

    pthread_attr_t audio_attr, led_attr;
    pthread_attr_init(&audio_attr);
    pthread_attr_setinheritsched(&audio_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&audio_attr, SCHED_FIFO);
    pthread_attr_setschedparam(&audio_attr, &audio_param);

    pthread_attr_init(&led_attr);
    pthread_attr_setinheritsched(&led_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&led_attr, SCHED_FIFO);
    pthread_attr_setschedparam(&led_attr, &led_param);

    uint32_t sample_rate;
    uint16_t channels;
    load_wav(FILENAME, &sample_rate, &channels);
    setup_alsa(sample_rate, channels);
    load_patterns("top_gun1.txt");

    pthread_create(&led_thread, &led_attr, led_thread_fn, NULL);
    pthread_create(&audio_thread, &audio_attr, audio_thread_fn, NULL);

    pthread_join(audio_thread, NULL);
    pthread_join(led_thread, NULL);

    save_runtime_log(AUDIO_LOG_FILE);
    return 0;
}

