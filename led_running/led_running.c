#include <stdio.h> // For printf
#include <stdlib.h> // for exit()
#include <unistd.h> // for usleep() (delays)
#include <gpiod.h> // for GPIO control

#define GPIO_CHIP "gpiochip0"

const int LED_PINS[8] = {22, 5, 6, 26, 23, 24, 25, 16};

void set_leds(struct gpiod_chip *chip, int state);
void set_led(struct gpiod_chip *chip, int led, int state);

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
		for(int i = 0; i < 8; i++)
		{
			usleep(70000);
			if (i == 0)
				set_led(chip, i, 1);
			       // usleep(70000);
			if (i == 7)
			{
				set_led(chip, i-1, 0);
				set_led(chip, i, 1);
				usleep(70000);
				set_led(chip, i, 0);
				i = -1;
			       // usleep(70000);
			}
			else
			{
				set_led(chip, i-1, 0);
				set_led(chip, i, 1);
			       // usleep(70000);
			}
		}
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

void set_led(struct gpiod_chip *chip, int led, int state)
{
	struct gpiod_line *line = gpiod_chip_get_line(chip, LED_PINS[led]);
	gpiod_line_request_output(line, "led_control", 0);
	gpiod_line_set_value(line, state);
	gpiod_line_release(line);
}
