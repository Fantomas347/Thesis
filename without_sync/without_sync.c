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

void cleanup_gpio() {
    gpiod_line_release_bulk(&led_lines);
    gpiod_chip_close(chip);
}

void* led_thread(void* arg) {
    FILE* f = (FILE*)arg;
    char line[32];

    while (fgets(line, sizeof(line), f)) {
        int delay;
        char bitstr[16];

        if (sscanf(line, "%d %s", &delay, bitstr) != 2) continue;

        // Remove the dot from 8-bit binary string
        char bits[9] = {0};
        int bi = 0;
        for (int i = 0; bitstr[i] != '\0' && bi < 8; ++i) {
            if (bitstr[i] == '.') continue;
            bits[bi++] = bitstr[i];
        }

        // Convert bit string to int array
        int values[8];
        for (int i = 0; i < 8; i++) {
            values[i] = (bits[i] == '1') ? 1 : 0;
        }

        gpiod_line_set_value_bulk(&led_lines, values);
        usleep(delay * 1000);
    }

    return NULL;
}

void* music_thread(void* arg) {
    system("mpg123 ~/country-drive-265942.mp3 > /dev/null 2>&1");
    return NULL;
}

int main() {
    pthread_t t_led, t_music;
    int ret;

    FILE* sequence_file = fopen("/home/pi/sequence.txt", "r");
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

