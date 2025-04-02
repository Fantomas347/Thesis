#include <stdio.h> // For printf
#include <stdlib.h> // for exit()
#include <unistd.h> // for usleep() (delays)
#include <gpiod.h> // for GPIO control

#define GPIO_CHIP "gpiochip0"

const int LED_PINS[8] = {2, 3, 4, 17, 27, 22, 10, 9};

void set_leds(struct gpiod_chip *chip, int state);

int main()
{
	struct gpiod_chip *chip = gpiod_chip_open_by_name(GPIO_CHIP);
	if (!chip)
	{
		perror("Failed to open GPIO chip");
		return 1;
	}

	while(1)
	{
		set_leds(chip, 1);
		usleep(500000);

		set_leds(chip, 0);
		usleep(500000);
	}

	gpiod_chip_close(chip);
	return 0;
}







void set_leds(struct gpiod_chip *chip, int state)
{
	for (int i = 0; i < 8; i++)
	{
		struct gpiod_line *line = gpiod_chip_get_line(chip, LED_PINS[i]);
		gpiod_line_request_output(line, "led_control", 0);
		gpiod_line_set_value(line, state);
		gpiod_line_release(line);
	}
}


