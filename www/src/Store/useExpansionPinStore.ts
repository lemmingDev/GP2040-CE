import { create } from 'zustand';

import WebApi from '../Services/WebApi';
import { PinActionValues, PinDirectionValues } from '../Data/Pins';

type State = {
	pins: {
		[key: string]: [
			{
				[key: string]: {
					option: PinActionValues;
					direction: PinDirectionValues;
					pull: number;
					inverted: boolean;
				};
			},
		];
	};
	loadingPins: boolean;
};

type Actions = {
	fetchPins: () => void;
	setPinAction: (
		expansion: string,
		index: number,
		pin: string,
		action: PinActionValues,
	) => void;
	setPinDirection: (
		expansion: string,
		index: number,
		pin: string,
		dir: PinDirectionValues,
	) => void;
	setPinPull: (expansion: string, index: number, pin: string, pull: number) => void;
	setPinInverted: (
		expansion: string,
		index: number,
		pin: string,
		inverted: boolean,
	) => void;
	setPinField: (
		expansion: string,
		index: number,
		pin: string,
		field: string,
		value: number | boolean,
	) => void;
	savePins: () => Promise<object>;
};

const GPLINK_PIN_COUNT = 64;

const makeGplinkPins = () => {
	const pins = {};
	for (let i = 0; i < GPLINK_PIN_COUNT; i++) {
		pins[`pin${String(i).padStart(2, '0')}`] = {
			option: -10,
			direction: 0,
			pull: 1,
			inverted: false,
		};
	}
	return pins;
};

const INITIAL_STATE: State = {
	pins: {
		pcf8575: [
			{
				pin00: { option: -10, direction: 0 },
				pin01: { option: -10, direction: 0 },
				pin02: { option: -10, direction: 0 },
				pin03: { option: -10, direction: 0 },
				pin04: { option: -10, direction: 0 },
				pin05: { option: -10, direction: 0 },
				pin06: { option: -10, direction: 0 },
				pin07: { option: -10, direction: 0 },
				pin08: { option: -10, direction: 0 },
				pin09: { option: -10, direction: 0 },
				pin10: { option: -10, direction: 0 },
				pin11: { option: -10, direction: 0 },
				pin12: { option: -10, direction: 0 },
				pin13: { option: -10, direction: 0 },
				pin14: { option: -10, direction: 0 },
				pin15: { option: -10, direction: 0 },
			},
		],
		gplink: [makeGplinkPins()],
	},
	loadingPins: false,
};

const useExpansionPinStore = create<State & Actions>()((set, get) => ({
	...INITIAL_STATE,
	fetchPins: async () => {
		set({ loadingPins: true });
		const pins = await WebApi.getExpansionPins();
		set((state) => ({
			...state,
			...pins,
			loadingPins: false,
		}));
	},
	setPinAction: (expansion, index, pin, action) => {
		set((state) => {
			const newPins = { ...state.pins };

			if (
				newPins[expansion] &&
				newPins[expansion][index] &&
				newPins[expansion][index][pin]
			) {
				newPins[expansion][index] = {
					...newPins[expansion][index],
					[pin]: {
						...newPins[expansion][index][pin],
						option: action,
					},
				};
			}

			return {
				...state,
				pins: newPins,
			};
		});
	},
	setPinDirection: (expansion, index, pin, dir) => {
		set((state) => {
			const newPins = { ...state.pins };

			if (
				newPins[expansion] &&
				newPins[expansion][index] &&
				newPins[expansion][index][pin]
			) {
				newPins[expansion][index] = {
					...newPins[expansion][index],
					[pin]: {
						...newPins[expansion][index][pin],
						direction: dir,
					},
				};
			}

			return {
				...state,
				pins: newPins,
			};
		});
	},
	setPinField: (expansion, index, pin, field, value) => {
		set((state) => {
			const newPins = { ...state.pins };

			if (
				newPins[expansion] &&
				newPins[expansion][index] &&
				newPins[expansion][index][pin]
			) {
				newPins[expansion][index] = {
					...newPins[expansion][index],
					[pin]: {
						...newPins[expansion][index][pin],
						[field]: value,
					},
				};
			}

			return {
				...state,
				pins: newPins,
			};
		});
	},
	setPinPull: (expansion, index, pin, pull) => {
		get().setPinField(expansion, index, pin, 'pull', pull);
	},
	setPinInverted: (expansion, index, pin, inverted) => {
		get().setPinField(expansion, index, pin, 'inverted', inverted);
	},
	savePins: async () => WebApi.setExpansionPins(get()),
}));

export default useExpansionPinStore;
