#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <gpiod.h>
#include <string.h>

const int LED_PINS[8] = {22, 5, 6, 26, 23, 24, 25, 16}; // BCM numbers
#define CHIPNAME "gpiochip4"  // For GPIOs 0–31 on Raspberry Pi

struct gpiod_line_bulk led_lines;
struct gpiod_chip *chip;

static inline long ms_diff(struct timespec a, struct timespec b) {
    return (a.tv_sec - b.tv_sec) * 1000 + (a.tv_nsec - b.tv_nsec) / 1000000;
}


void cleanup_gpio() {
    gpiod_line_release_bulk(&led_lines);
    gpiod_chip_close(chip);
}

void* led_thread(void* arg) {
    FILE* f = (FILE*)arg;
    FILE* log = fopen("/home/pi/top_gun1_runtime.log", "w");
    if (!log) {
        perror("Cannot open runtime log file");
        return NULL;
    }

    char line[32];
    int delay;
    char bitstr[16];
    char bits[9];
    int values[8];

    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%d %s", &delay, bitstr) != 2) continue;

        // Parse bit string (remove dot)
        int bi = 0;
        for (int i = 0; bitstr[i] != '\0' && bi < 8; ++i) {
            if (bitstr[i] == '.') continue;
            bits[bi++] = bitstr[i];
        }

        for (int i = 0; i < 8; i++) {
            values[i] = (bits[i] == '1') ? 1 : 0;
        }

        // Set GPIOs immediately
        gpiod_line_set_value_bulk(&led_lines, values);

        // Log immediately
        fprintf(log, "%04d %c%c%c%c.%c%c%c%c\n",
            delay,
            bits[0], bits[1], bits[2], bits[3],
            bits[4], bits[5], bits[6], bits[7]);

        // Hold the pattern for 'delay' ms
        usleep(delay * 1000);
    }

    fclose(log);
    return NULL;
}

void* music_thread(void* arg) {
    system("mpg123 ~/top_gun1.mp3 > /dev/null 2>&1");
    return NULL;
}

int main() {
    pthread_t t_led, t_music;
    int ret;

    FILE* sequence_file = fopen("/home/pi/top_gun1.txt", "r");
    if (!sequence_file) {
        perror("Failed to open sequence.txt");
        return 1;
    }

    chip = gpiod_chip_open_by_name(CHIPNAME);
    if (!chip) {
        perror("Open chip failed");
        fclose(sequence_file);
        return 1;
    }

    struct gpiod_line *lines[8];
    for (int i = 0; i < 8; i++) {
        lines[i] = gpiod_chip_get_line(chip, LED_PINS[i]);
        if (!lines[i]) {
            perror("Get line failed");
            cleanup_gpio();
            fclose(sequence_file);
            return 1;
        }
    }

    gpiod_line_bulk_init(&led_lines);
    for (int i = 0; i < 8; i++) {
        gpiod_line_bulk_add(&led_lines, lines[i]);
    }

    ret = gpiod_line_request_bulk_output(&led_lines, "led_music", NULL);
    if (ret < 0) {
        perror("Request lines as output failed");
        cleanup_gpio();
        fclose(sequence_file);
        return 1;
    }

    pthread_create(&t_led, NULL, led_thread, sequence_file);
    pthread_create(&t_music, NULL, music_thread, NULL);

    pthread_join(t_music, NULL);  // Wait for music to finish

    pthread_join(t_led, NULL);    // Wait for LED playback to finish

    int all_off[8] = {0};
    gpiod_line_set_value_bulk(&led_lines, all_off);

    cleanup_gpio();
    fclose(sequence_file);

    return 0;
}

