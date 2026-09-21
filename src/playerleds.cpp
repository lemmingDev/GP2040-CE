#include "playerleds.h"
#if defined(PICO_BOARD)
#include "pico/stdlib.h"
#include <hardware/pwm.h>
#elif defined(ESP_PLATFORM)
// S3: PWM backend is hal_pwm_s3 (LED addon drives pins via halPwmConfig);
// time_reached/absolute_time_t come from the pico/time.h shim.
#include "hal_pwm_s3.h"
#endif

void PlayerLEDs::animate(PLEDAnimationState animationState)
{
	// Reset state and bypass timer check if animation changed
	if (animationState.animation != selectedAnimation)
	{
		reset();
		selectedAnimation = animationState.animation;
	}
	else if (!time_reached(nextAnimationTime))
	{
		return;
	}

	parseState(animationState.state);

	switch (selectedAnimation)
	{
		case PLED_ANIM_BLINK:
			handleBlink(animationState.speed);
			break;

		case PLED_ANIM_CYCLE:
			handleCycle(animationState.speed);
			break;

		case PLED_ANIM_FADE:
			handleFade();
			break;

		case PLED_ANIM_BLINK_CUSTOM:
			handleBlinkCustom(animationState.speedOn, animationState.speedOff);
			break;

		default:
			break;
	}

	for (int i = 0; i < PLED_COUNT; i++)
		ledLevels[i] = PLED_MAX_LEVEL - (currentPledState[i] ? (brightness * brightness) : 0);
}
