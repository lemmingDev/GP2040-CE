import { useContext, useEffect, useState } from 'react';
import { AppContext } from '../Contexts/AppContext';
import { useTranslation } from 'react-i18next';

import useBoardDefinition from '../Store/useBoardDefinitionStore';

const AnalogPinOptions = () => {
	const { usedPins } = useContext(AppContext);
	const { t } = useTranslation();

	const { boardDefinition, getBoardDefinition } = useBoardDefinition();

	useEffect(() => {
		getBoardDefinition();
	}, []);

	const ANALOG_PINS = boardDefinition.analogPins;
	const ADC2_PINS = boardDefinition.adc2Pins ?? [];

	return (
		<>
			<option value={-1}>
				{t('AddonsConfig:analog-available-pins-option-not-set')}
			</option>
			{ANALOG_PINS.map((i) => (
				<option key={`analogPins-option-${i}`} value={i}>
					{i}
				</option>
			))}
			{ADC2_PINS.map((i) => (
				<option key={`adc2Pins-option-${i}`} value={i}>
					{i} (ADC2)
				</option>
			))}
		</>
	);
};

export default AnalogPinOptions;
