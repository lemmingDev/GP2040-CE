#if defined(PICO_BOARD)
#include "hardware/pwm.h"
#elif defined(ESP_PLATFORM)
#include "hal_gpio.h"
#include "hal_pwm_s3.h"
#endif
#include "addons/buzzerspeaker.h"
#include "songs.h"
#include "storagemanager.h"
#include "usbdriver.h"
#include "math.h"
#include "helper.h"
#include "config.pb.h"

bool BuzzerSpeakerAddon::available() {
    const BuzzerOptions& options = Storage::getInstance().getAddonOptions().buzzerOptions;
	return options.enabled && isValidPin(options.pin);
}

void BuzzerSpeakerAddon::setup() {
	const BuzzerOptions& options = Storage::getInstance().getAddonOptions().buzzerOptions;
	buzzerPin = options.pin;
#if defined(PICO_BOARD)
	gpio_set_function(buzzerPin, GPIO_FUNC_PWM);
	buzzerPinSlice = pwm_gpio_to_slice_num (buzzerPin);
	buzzerPinChannel = pwm_gpio_to_channel (buzzerPin);
#elif defined(ESP_PLATFORM)
	// S3: no slice/channel setup — halPwmTone binds TIMER_0/CHANNEL_0 per tone.
#endif

    // enable pin is optional so not required to toggle addon
    if (isValidPin(options.pin)) {
        isSpeakerOn = true;
        buzzerEnablePin = options.enablePin;
#if defined(PICO_BOARD)
        gpio_init(buzzerEnablePin);
        gpio_set_dir(buzzerEnablePin, GPIO_OUT);
        gpio_put(buzzerEnablePin, isSpeakerOn);
#elif defined(ESP_PLATFORM)
        hal::gpioInit(buzzerEnablePin);
        hal::gpioSetOutput(buzzerEnablePin);
        hal::gpioPut(buzzerEnablePin, isSpeakerOn);
#endif
    }

	buzzerVolume = options.volume;
	introPlayed = false;
}

void BuzzerSpeakerAddon::process() {
	if (!introPlayed) {
		playIntro();
	}

	processBuzzer();
}

void BuzzerSpeakerAddon::playIntro() {
	if (getMillis() < 1000) {
		return;
	}

	bool isConfigMode = Storage::getInstance().GetConfigMode();

	if (!get_usb_mounted() || isConfigMode) {
		play(&configModeSong);
	} else {
		play(&introSong);
	}
	introPlayed = true;
}

void BuzzerSpeakerAddon::processBuzzer() {
	if (currentSong == NULL) {
		return;
	}

	uint32_t currentTimeSong = getMillis() - startedSongMils;
	uint32_t totalTimeSong = currentSong->song.size() * currentSong->toneDuration;
	uint16_t currentTonePosition = floor((currentTimeSong * currentSong->song.size()) / totalTimeSong);
	Tone currentTone = currentSong->song[currentTonePosition];

	if (currentTonePosition >= currentSong->song.size()) {
		stop();
		return;
	}

	if (currentTone == PAUSE) {
#if defined(PICO_BOARD)
		pwm_set_enabled (buzzerPinSlice, false);
#elif defined(ESP_PLATFORM)
		// Idle the buzzer channel low; halPwmTone re-binds it on the next tone.
		ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
#endif
		return;
	}

#if defined(PICO_BOARD)
	pwmSetFreqDuty(buzzerPinSlice, buzzerPinChannel, currentTone, 0.03 * ((float) buzzerVolume));
	pwm_set_enabled (buzzerPinSlice, true);
#elif defined(ESP_PLATFORM)
	// Same tone frequency and 0.03*volume duty (percent) as Pico; +0.5f rounds
	// to nearest instead of truncating the float->uint8 conversion.
	halPwmTone(buzzerPin, currentTone, (uint8_t)(0.03f * ((float)buzzerVolume) + 0.5f));
#endif
}

void BuzzerSpeakerAddon::play(Song *song) {
	startedSongMils = getMillis();
	currentSong = song;
}

void BuzzerSpeakerAddon::stop() {
#if defined(PICO_BOARD)
	pwm_set_enabled (buzzerPinSlice, false);
#elif defined(ESP_PLATFORM)
	ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
#endif
	currentSong = NULL;
}

#if defined(PICO_BOARD)
uint32_t BuzzerSpeakerAddon::pwmSetFreqDuty(uint slice, uint channel, uint32_t frequency, float duty) {
	uint32_t clock = 125000000;
	uint32_t divider16 = clock / frequency / 4096 +
							(clock % (frequency * 4096) != 0);
	if (divider16 / 16 == 0)
	divider16 = 16;
	uint32_t wrap = clock * 16 / divider16 / frequency - 1;
	pwm_set_clkdiv_int_frac(slice, divider16/16,
										divider16 & 0xF);
	pwm_set_wrap(slice, wrap);
	pwm_set_chan_level(slice, channel, wrap * duty / 100);
	return wrap;
}
#endif

