#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <gpiod.h>
#include <time.h>

#define GPIO_CHIP "gpiochip0"
const int LED_PINS[8] = {22, 5, 6, 26, 23, 24, 25, 16};

// Function declarations
void* led_thread(void* arg);
void set_leds(struct gpiod_chip *chip, int state);
void set_led(struct gpiod_chip *chip, int led, int state);

int main() {
	pthread_t thread;
	if (pthread_create(&thread, NULL, led_thread, NULL) != 0) {
		perror("Failed to create LED thread");
		return 1;
	}

	// Join thread (never ends in this example)
	pthread_join(thread, NULL);
	return 0;
}

// This is your LED control logic, now in a thread
void* led_thread(void* arg) {
	struct gpiod_chip *chip = gpiod_chip_open_by_name(GPIO_CHIP);
	if (!chip) {
		perror("Failed to open GPIO chip");
		pthread_exit(NULL);
	}

	int i = 0;
	while (1) {
		usleep(70000);

		if (i == 0) {
			set_led(chip, i, 1);
		}
		if (i == 7) {
			set_led(chip, i - 1, 0);
			set_led(chip, i, 1);
			usleep(70000);
			set_led(chip, i, 0);
			i = -1;
		} else {
			set_led(chip, i - 1, 0);
			set_led(chip, i, 1);
		}
		i++;
	}

	gpiod_chip_close(chip);
	pthread_exit(NULL);
}

void set_leds(struct gpiod_chip *chip, int state) {
	for (int i = 0; i < 8; i++) {
		struct gpiod_line *line = gpiod_chip_get_line(chip, LED_PINS[i]);
		gpiod_line_request_output(line, "led_control", 0);
		gpiod_line_set_value(line, state);
		gpiod_line_release(line);
	}
}

void set_led(struct gpiod_chip *chip, int led, int state) {
	if (led < 0 || led > 7) return;
	struct gpiod_line *line = gpiod_chip_get_line(chip, LED_PINS[led]);
	gpiod_line_request_output(line, "led_control", 0);
	gpiod_line_set_value(line, state);
	gpiod_line_release(line);
}
